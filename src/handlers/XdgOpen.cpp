#include "XdgOpen.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

namespace {

constexpr auto kHandlerId = "synchro.open.xdg";

QString manifestPath(const QString &root) {
  return QDir(root).filePath(QStringLiteral("synchro.open.xdg/manifest.json"));
}

bool readableFile(const QString &path) {
  const QFileInfo info(path);
  return info.isFile() && info.isReadable();
}

} // namespace

QString XdgOpen::defaultFirstPartyDir() {
#ifdef SYNCHRO_FIRST_PARTY_HANDLER_DIR
  const QString compiled = QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR);
  if (readableFile(manifestPath(compiled)))
    return compiled;
#endif
  // ninja && ./synchro from the build dir.
  const QString nearby = QDir::cleanPath(
      QCoreApplication::applicationDirPath() +
      QStringLiteral("/../handlers"));
  return nearby;
}

bool XdgOpen::load(const QString &firstPartyDir) {
  m_loaded = false;
  m_error.clear();
  const QString root =
      firstPartyDir.isEmpty() ? defaultFirstPartyDir() : firstPartyDir;
  const QString path = manifestPath(root);
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    m_error = QStringLiteral("missing %1").arg(path);
    return false;
  }
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    m_error = QStringLiteral("invalid manifest %1").arg(path);
    return false;
  }
  const QJsonObject obj = doc.object();
  if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1) {
    m_error = QStringLiteral("unsupported schemaVersion");
    return false;
  }
  m_id = obj.value(QStringLiteral("id")).toString();
  if (m_id != QLatin1String(kHandlerId)) {
    m_error = QStringLiteral("expected id %1").arg(QLatin1String(kHandlerId));
    return false;
  }
  const QJsonObject open = obj.value(QStringLiteral("open")).toObject();
  m_execLine = open.value(QStringLiteral("exec")).toString();
  m_tryExec = open.value(QStringLiteral("tryExec")).toString();
  if (m_execLine.isEmpty()) {
    m_error = QStringLiteral("open.exec missing");
    return false;
  }
  m_sourceDir = QFileInfo(path).absolutePath();
  m_loaded = true;
  return true;
}

bool XdgOpen::open(const QString &path, const QString &mime,
                   const QString &cwd) {
  m_error.clear();
  if (!m_loaded && !load())
    return false;
  if (!m_tryExec.isEmpty() &&
      QStandardPaths::findExecutable(m_tryExec).isEmpty()) {
    m_error = QStringLiteral("tryExec %1 not found").arg(m_tryExec);
    return false;
  }
  if (path.isEmpty()) {
    m_error = QStringLiteral("empty path");
    return false;
  }

  HandlerExec::Request req;
  req.exec = m_execLine;
  req.handlerId = m_id;
  req.handlerDir = m_sourceDir;
  req.cwd = cwd;
  HandlerExec::Item item;
  item.path = path;
  item.uri = QUrl::fromLocalFile(path);
  item.mime = mime;
  item.isDir = false;
  req.items.append(item);

  if (!m_exec.run(req)) {
    m_error = m_exec.lastError();
    return false;
  }
  return true;
}
