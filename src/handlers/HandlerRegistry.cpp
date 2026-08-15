#include "HandlerRegistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>

namespace {

bool readableFile(const QString &path) {
  const QFileInfo info(path);
  return info.isFile() && info.isReadable();
}

QStringList jsonStringList(const QJsonValue &v) {
  QStringList out;
  if (!v.isArray())
    return out;
  for (const QJsonValue &item : v.toArray()) {
    if (item.isString())
      out.append(item.toString());
  }
  return out;
}

} // namespace

HandlerRegistry::HandlerRegistry()
    : m_firstPartyDir(defaultFirstPartyDir()),
      m_userDir(defaultUserDir()),
      m_configPath(defaultConfigPath()) {}

QString HandlerRegistry::defaultFirstPartyDir() {
#ifdef SYNCHRO_FIRST_PARTY_HANDLER_DIR
  const QString compiled = QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR);
  if (QFileInfo::exists(compiled))
    return compiled;
#endif
  return QDir::cleanPath(QCoreApplication::applicationDirPath() +
                         QStringLiteral("/../handlers"));
}

QString HandlerRegistry::defaultUserDir() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericConfigLocation))
      .filePath(QStringLiteral("synchro/handlers"));
}

QString HandlerRegistry::defaultConfigPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericConfigLocation))
      .filePath(QStringLiteral("synchro/handlers.json"));
}

void HandlerRegistry::loadConfig() {
  m_disabled.clear();
  m_enabled.clear();
  m_openOverrides.clear();
  QFile file(m_configPath);
  if (!file.open(QIODevice::ReadOnly))
    return;
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    return;
  const QJsonObject obj = doc.object();
  m_disabled = jsonStringList(obj.value(QStringLiteral("disabled")));
  m_enabled = jsonStringList(obj.value(QStringLiteral("enabled")));
  const QJsonObject ov = obj.value(QStringLiteral("openOverrides")).toObject();
  for (auto it = ov.begin(); it != ov.end(); ++it) {
    if (it.value().isString())
      m_openOverrides.insert(it.key(), it.value().toString());
  }
}

bool HandlerRegistry::isEnabled(const QString &id, bool firstParty) const {
  if (m_disabled.contains(id))
    return false;
  if (firstParty)
    return true;
  return m_enabled.contains(id);
}

void HandlerRegistry::scan() {
  m_byId.clear();
  loadConfig();
  scanTree(m_firstPartyDir, true);
  scanTree(m_userDir, false);
  if (m_scanEnv) {
    const QString env = qEnvironmentVariable("SYNCHRO_HANDLER_DIR");
    if (!env.isEmpty()) {
      const QStringList roots =
          env.split(QLatin1Char(':'), Qt::SkipEmptyParts);
      for (const QString &root : roots)
        scanTree(root, false);
    }
  }
}

void HandlerRegistry::scanTree(const QString &root, bool firstParty) {
  if (root.isEmpty())
    return;
  const QDir dir(root);
  if (!dir.exists())
    return;
  if (readableFile(dir.filePath(QStringLiteral("manifest.json"))))
    loadOne(dir.absolutePath(), firstParty);
  const QStringList subs =
      dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
  for (const QString &name : subs) {
    const QString child = dir.filePath(name);
    if (readableFile(QDir(child).filePath(QStringLiteral("manifest.json"))))
      loadOne(child, firstParty);
  }
}

void HandlerRegistry::loadOne(const QString &dir, bool firstParty) {
  const ManifestValidation v = validateManifestDir(dir, firstParty);
  if (!v.ok) {
    std::fprintf(stderr, "synchro: skip handler %s: %s\n", qPrintable(dir),
                 qPrintable(v.errors.join(QStringLiteral("; "))));
    return;
  }
  const QString id = v.manifest.id;
  if (m_byId.contains(id)) {
    if (Manifest::isReservedId(id) && m_byId.value(id).firstParty)
      return;
  }
  Record rec;
  rec.manifest = v.manifest;
  rec.sourceDir = QFileInfo(dir).absoluteFilePath();
  rec.manifest.sourceDir = rec.sourceDir;
  rec.manifest.firstParty = firstParty;
  rec.firstParty = firstParty;
  rec.enabled = isEnabled(id, firstParty);
  m_byId.insert(id, rec);
}

QVector<HandlerRegistry::Record> HandlerRegistry::handlers() const {
  QVector<Record> out;
  out.reserve(m_byId.size());
  for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it)
    out.append(it.value());
  std::sort(out.begin(), out.end(), [](const Record &a, const Record &b) {
    return a.manifest.id < b.manifest.id;
  });
  return out;
}

HandlerRegistry::Record HandlerRegistry::handler(const QString &id) const {
  return m_byId.value(id);
}

QVector<HandlerRegistry::Match>
HandlerRegistry::resolve(const QString &kind,
                         const QVector<Manifest::Item> &items) const {
  QVector<Match> out;
  QString overrideId;
  if (kind == QLatin1String("open") && items.size() == 1)
    overrideId = m_openOverrides.value(items.constFirst().mime);

  for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it) {
    const Record &rec = it.value();
    if (!rec.enabled)
      continue;
    if (!manifestMatches(rec.manifest, kind, items))
      continue;
    const QString tryExec = rec.manifest.tryExec(kind);
    if (!tryExec.isEmpty() &&
        QStandardPaths::findExecutable(tryExec).isEmpty())
      continue;
    Match m;
    m.id = rec.manifest.id;
    m.sourceDir = rec.sourceDir;
    m.manifest = rec.manifest;
    m.priority = rec.manifest.priority;
    m.specificity = manifestSpecificity(rec.manifest, items);
    m.override = !overrideId.isEmpty() && overrideId == rec.manifest.id;
    out.append(m);
  }

  std::sort(out.begin(), out.end(), [](const Match &a, const Match &b) {
    if (a.override != b.override)
      return a.override > b.override;
    if (a.priority != b.priority)
      return a.priority > b.priority;
    if (a.specificity != b.specificity)
      return a.specificity > b.specificity;
    return a.id < b.id;
  });
  return out;
}
