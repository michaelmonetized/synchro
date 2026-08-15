#include "Manifest.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>

namespace {

const QRegularExpression kIdRe(
    QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));

const QStringList kLegalKinds = {QStringLiteral("preview"),
                                 QStringLiteral("open"),
                                 QStringLiteral("folder"),
                                 QStringLiteral("action"),
                                 QStringLiteral("location")};

const QStringList kCoreAdapters = {QStringLiteral("trash"),
                                   QStringLiteral("recent"),
                                   QStringLiteral("search"),
                                   QStringLiteral("semantic")};

QStringList jsonStringList(const QJsonValue &v) {
  QStringList out;
  if (v.isString()) {
    const QString s = v.toString();
    if (!s.isEmpty())
      out.append(s);
    return out;
  }
  if (!v.isArray())
    return out;
  const QJsonArray arr = v.toArray();
  for (const QJsonValue &item : arr) {
    if (item.isString())
      out.append(item.toString());
  }
  return out;
}

QString jsonString(const QJsonObject &obj, const QString &key) {
  return obj.value(key).toString();
}

bool hasSymlinkOutsideGit(const QString &root) {
  QDirIterator it(root,
                  QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden |
                      QDir::System,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    const QFileInfo fi = it.fileInfo();
    const QString rel = QDir(root).relativeFilePath(fi.absoluteFilePath());
    if (rel == QLatin1String(".git") ||
        rel.startsWith(QLatin1String(".git/")))
      continue;
    if (fi.isSymLink())
      return true;
  }
  return false;
}

QString globToRegex(const QString &pattern) {
  QString rx;
  rx += QLatin1Char('^');
  for (int i = 0; i < pattern.size();) {
    if (i + 1 < pattern.size() && pattern.at(i) == QLatin1Char('*') &&
        pattern.at(i + 1) == QLatin1Char('*')) {
      i += 2;
      if (i < pattern.size() && pattern.at(i) == QLatin1Char('/')) {
        rx += QStringLiteral("(?:.*/)?");
        ++i;
      } else {
        rx += QStringLiteral(".*");
      }
      continue;
    }
    const QChar c = pattern.at(i++);
    if (c == QLatin1Char('*'))
      rx += QStringLiteral("[^/]*");
    else if (c == QLatin1Char('?'))
      rx += QStringLiteral("[^/]");
    else
      rx += QRegularExpression::escape(QString(c));
  }
  rx += QLatin1Char('$');
  return rx;
}

bool modeAll(const QString &mode) {
  return mode.isEmpty() || mode == QLatin1String("all");
}

int defaultMinItems(const QString &kind) {
  Q_UNUSED(kind);
  return 1;
}

int defaultMaxItems(const QString &kind) {
  if (kind == QLatin1String("action"))
    return 1000000;
  return 1;
}

QString kindObjectRuntime(const QJsonObject &obj) {
  return obj.value(QStringLiteral("runtime")).toString();
}

void requireEntry(const Manifest &m, const QString &kind, const QString &key,
                  QStringList *errors) {
  if (!m.entryPoints.contains(key) || m.entryPoints.value(key).isEmpty())
    errors->append(QStringLiteral("kind '%1' requires an 'entryPoints.%2' "
                                  "to load")
                       .arg(kind, key));
}

void validateKindRequirements(const Manifest &m, QStringList *errors) {
  for (const QString &kind : m.kinds) {
    if (kind == QLatin1String("preview")) {
      if (kindObjectRuntime(m.preview) == QLatin1String("exec")) {
        if (m.preview.value(QStringLiteral("exec")).toString().isEmpty())
          errors->append(QStringLiteral("preview.runtime exec requires "
                                        "preview.exec"));
      } else {
        requireEntry(m, kind, QStringLiteral("preview"), errors);
      }
    } else if (kind == QLatin1String("open")) {
      const bool hasEp = m.entryPoints.contains(QStringLiteral("open")) &&
                         !m.entryPoints.value(QStringLiteral("open")).isEmpty();
      const bool hasExec =
          !m.open.value(QStringLiteral("exec")).toString().isEmpty();
      if (!hasEp && !hasExec)
        errors->append(QStringLiteral("kind 'open' requires entryPoints.open "
                                      "or open.exec"));
    } else if (kind == QLatin1String("folder")) {
      requireEntry(m, kind, QStringLiteral("folder"), errors);
    } else if (kind == QLatin1String("action")) {
      const bool hasEp = m.entryPoints.contains(QStringLiteral("action")) &&
                         !m.entryPoints.value(QStringLiteral("action")).isEmpty();
      const bool hasExec =
          !m.action.value(QStringLiteral("exec")).toString().isEmpty();
      const bool core =
          kindObjectRuntime(m.action) == QLatin1String("core");
      if (!hasEp && !hasExec && !core)
        errors->append(QStringLiteral("kind 'action' requires "
                                      "entryPoints.action, action.exec, or "
                                      "action.runtime core"));
    } else if (kind == QLatin1String("location")) {
      const QString runtime = kindObjectRuntime(m.location);
      if (runtime == QLatin1String("path")) {
        if (m.location.value(QStringLiteral("path")).toString().isEmpty())
          errors->append(QStringLiteral("location.runtime path requires "
                                        "location.path"));
      } else if (runtime == QLatin1String("core")) {
        const QString adapter =
            m.location.value(QStringLiteral("adapter")).toString();
        if (adapter.isEmpty() || !kCoreAdapters.contains(adapter))
          errors->append(QStringLiteral("location.runtime core requires "
                                        "location.adapter "
                                        "trash|recent|search|semantic"));
      } else if (runtime == QLatin1String("chrome") || runtime.isEmpty()) {
        requireEntry(m, kind, QStringLiteral("location"), errors);
      } else {
        errors->append(QStringLiteral("unknown location.runtime '%1'")
                           .arg(runtime));
      }
    }
  }
}

} // namespace

bool Manifest::isSafeEntryPoint(const QString &value) {
  if (value.isEmpty() || value.startsWith(QLatin1Char('/')))
    return false;
  if (value.contains(QLatin1String("..")))
    return false;
  if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
    return false;
  return true;
}

bool Manifest::isValidId(const QString &id) {
  if (id.isEmpty() || id.contains(QLatin1Char('/')) ||
      id.contains(QLatin1String("..")) || id.startsWith(QLatin1Char('/')))
    return false;
  return kIdRe.match(id).hasMatch();
}

bool Manifest::isReservedId(const QString &id) {
  return id.startsWith(QLatin1String("synchro.")) ||
         id.startsWith(QLatin1String("omarchy."));
}

bool Manifest::matchMime(const QString &mime, const QString &pattern) {
  if (pattern.isEmpty())
    return true;
  if (pattern == mime)
    return true;
  if (pattern == QLatin1String("*") || pattern == QLatin1String("*/*"))
    return true;
  if (pattern.endsWith(QLatin1String("/*"))) {
    const QString prefix = pattern.left(pattern.size() - 1);
    return mime.startsWith(prefix);
  }
  return QDir::match(pattern, mime);
}

bool Manifest::matchPathGlob(const QString &path, const QString &pattern) {
  if (pattern.isEmpty())
    return false;
  if (!pattern.contains(QLatin1Char('/')) &&
      !pattern.contains(QLatin1String("**"))) {
    return QDir::match(pattern, QFileInfo(path).fileName());
  }
  const QRegularExpression rx(globToRegex(pattern));
  return rx.match(path).hasMatch();
}

bool Manifest::matchSuffix(const QString &path, const QString &suffix) {
  if (suffix.isEmpty())
    return false;
  return path.endsWith(suffix, Qt::CaseInsensitive);
}

Manifest Manifest::fromJson(const QJsonObject &obj) {
  Manifest m;
  m.schemaVersion = obj.value(QStringLiteral("schemaVersion")).toInt();
  m.id = jsonString(obj, QStringLiteral("id"));
  m.name = jsonString(obj, QStringLiteral("name"));
  m.version = jsonString(obj, QStringLiteral("version"));
  m.author = jsonString(obj, QStringLiteral("author"));
  m.license = jsonString(obj, QStringLiteral("license"));
  m.description = jsonString(obj, QStringLiteral("description"));
  m.kinds = jsonStringList(obj.value(QStringLiteral("kinds")));
  m.priority = obj.value(QStringLiteral("priority")).toInt(50);
  m.keepLoaded = obj.value(QStringLiteral("keepLoaded")).toBool(false);
  m.permissions = jsonStringList(obj.value(QStringLiteral("permissions")));
  m.open = obj.value(QStringLiteral("open")).toObject();
  m.preview = obj.value(QStringLiteral("preview")).toObject();
  m.folder = obj.value(QStringLiteral("folder")).toObject();
  m.action = obj.value(QStringLiteral("action")).toObject();
  m.location = obj.value(QStringLiteral("location")).toObject();

  const QJsonObject eps = obj.value(QStringLiteral("entryPoints")).toObject();
  for (auto it = eps.begin(); it != eps.end(); ++it) {
    if (it.value().isString())
      m.entryPoints.insert(it.key(), it.value().toString());
  }

  const QJsonObject match = obj.value(QStringLiteral("match")).toObject();
  m.match.mime = jsonStringList(match.value(QStringLiteral("mime")));
  m.match.mimeMode = match.value(QStringLiteral("mimeMode")).toString();
  if (m.match.mimeMode.isEmpty())
    m.match.mimeMode = QStringLiteral("all");
  m.match.suffix = jsonStringList(match.value(QStringLiteral("suffix")));
  m.match.pathGlob = jsonStringList(match.value(QStringLiteral("pathGlob")));
  m.match.pathMode = match.value(QStringLiteral("pathMode")).toString();
  if (m.match.pathMode.isEmpty())
    m.match.pathMode = QStringLiteral("all");
  m.match.folderContains =
      jsonStringList(match.value(QStringLiteral("folderContains")));
  if (match.contains(QStringLiteral("minItems")))
    m.match.minItems = match.value(QStringLiteral("minItems")).toInt();
  if (match.contains(QStringLiteral("maxItems")))
    m.match.maxItems = match.value(QStringLiteral("maxItems")).toInt();
  m.match.host = match.value(QStringLiteral("host")).toString();
  if (m.match.host.isEmpty())
    m.match.host = QStringLiteral("posix-local");
  return m;
}

QVariantMap Manifest::toVariantMap() const {
  QVariantMap ep;
  for (auto it = entryPoints.begin(); it != entryPoints.end(); ++it)
    ep.insert(it.key(), it.value());
  QVariantMap out;
  out.insert(QStringLiteral("schemaVersion"), schemaVersion);
  out.insert(QStringLiteral("id"), id);
  out.insert(QStringLiteral("name"), name);
  out.insert(QStringLiteral("version"), version);
  out.insert(QStringLiteral("kinds"), kinds);
  out.insert(QStringLiteral("priority"), priority);
  out.insert(QStringLiteral("keepLoaded"), keepLoaded);
  out.insert(QStringLiteral("entryPoints"), ep);
  out.insert(QStringLiteral("__sourceDir"), sourceDir);
  out.insert(QStringLiteral("__isFirstParty"), firstParty);
  return out;
}

bool Manifest::hasKind(const QString &kind) const {
  return kinds.contains(kind);
}

QString Manifest::tryExec(const QString &kind) const {
  if (kind == QLatin1String("open"))
    return open.value(QStringLiteral("tryExec")).toString();
  if (kind == QLatin1String("preview"))
    return preview.value(QStringLiteral("tryExec")).toString();
  if (kind == QLatin1String("action"))
    return action.value(QStringLiteral("tryExec")).toString();
  if (kind == QLatin1String("folder"))
    return folder.value(QStringLiteral("tryExec")).toString();
  return {};
}

QString Manifest::execLine(const QString &kind) const {
  if (kind == QLatin1String("open"))
    return open.value(QStringLiteral("exec")).toString();
  if (kind == QLatin1String("preview"))
    return preview.value(QStringLiteral("exec")).toString();
  if (kind == QLatin1String("action"))
    return action.value(QStringLiteral("exec")).toString();
  if (kind == QLatin1String("folder"))
    return folder.value(QStringLiteral("exec")).toString();
  return {};
}

QString Manifest::runtime(const QString &kind) const {
  if (kind == QLatin1String("open"))
    return kindObjectRuntime(open);
  if (kind == QLatin1String("preview"))
    return kindObjectRuntime(preview);
  if (kind == QLatin1String("action"))
    return kindObjectRuntime(action);
  if (kind == QLatin1String("folder"))
    return kindObjectRuntime(folder);
  if (kind == QLatin1String("location"))
    return kindObjectRuntime(location);
  return {};
}

ManifestValidation validateManifestDir(const QString &dir, bool firstParty) {
  ManifestValidation result;
  const QFileInfo dirInfo(dir);
  if (!dirInfo.exists() || !dirInfo.isDir()) {
    result.errors.append(QStringLiteral("handler folder not found: %1").arg(dir));
    return result;
  }
  const QString root = dirInfo.absoluteFilePath();
  const QString path = QDir(root).filePath(QStringLiteral("manifest.json"));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    result.errors.append(QStringLiteral("missing manifest.json in %1").arg(root));
    return result;
  }
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) {
    result.errors.append(QStringLiteral("manifest.json is not valid JSON: %1")
                             .arg(path));
    return result;
  }
  const QJsonObject obj = doc.object();
  const QJsonValue schema = obj.value(QStringLiteral("schemaVersion"));
  if (!schema.isDouble() || schema.toInt() != 1)
    result.errors.append(
        QStringLiteral("unsupported or missing schemaVersion (expected 1)"));

  for (const QString &field : {QStringLiteral("id"), QStringLiteral("name"),
                               QStringLiteral("version"), QStringLiteral("kinds"),
                               QStringLiteral("entryPoints")}) {
    if (!obj.contains(field))
      result.errors.append(
          QStringLiteral("manifest missing required field '%1'").arg(field));
  }

  if (obj.contains(QStringLiteral("entryPoints")) &&
      !obj.value(QStringLiteral("entryPoints")).isObject())
    result.errors.append(QStringLiteral("'entryPoints' must be an object"));
  if (obj.contains(QStringLiteral("kinds")) &&
      !obj.value(QStringLiteral("kinds")).isArray())
    result.errors.append(QStringLiteral("'kinds' must be a non-empty array"));

  Manifest m = Manifest::fromJson(obj);
  m.sourceDir = root;
  m.firstParty = firstParty;

  if (obj.contains(QStringLiteral("id")) && !Manifest::isValidId(m.id))
    result.errors.append(QStringLiteral("invalid plugin id '%1'").arg(m.id));
  if (Manifest::isReservedId(m.id) && !firstParty)
    result.errors.append(
        QStringLiteral("plugin id '%1' uses the reserved synchro.* / "
                       "omarchy.* namespace")
            .arg(m.id));

  if (obj.contains(QStringLiteral("kinds"))) {
    if (m.kinds.isEmpty())
      result.errors.append(QStringLiteral("'kinds' must be a non-empty array"));
    for (const QString &kind : m.kinds) {
      if (!kLegalKinds.contains(kind))
        result.errors.append(QStringLiteral("unknown kind '%1'").arg(kind));
    }
  }

  if (obj.contains(QStringLiteral("entryPoints")) &&
      obj.value(QStringLiteral("entryPoints")).isObject()) {
    const QJsonObject eps = obj.value(QStringLiteral("entryPoints")).toObject();
    for (auto it = eps.begin(); it != eps.end(); ++it) {
      if (!it.value().isString()) {
        result.errors.append(
            QStringLiteral("entry point '%1' must be a string").arg(it.key()));
        continue;
      }
      const QString ep = it.value().toString();
      if (!Manifest::isSafeEntryPoint(ep)) {
        result.errors.append(
            QStringLiteral("unsafe entryPoint '%1'='%2'").arg(it.key(), ep));
        continue;
      }
      const QString resolved =
          QDir::cleanPath(QDir(root).filePath(ep));
      const QString base = QDir::cleanPath(root);
      if (resolved != base &&
          !resolved.startsWith(base + QLatin1Char('/'))) {
        result.errors.append(
            QStringLiteral("entry point escapes sourceDir: %1").arg(resolved));
        continue;
      }
      if (!QFileInfo::exists(resolved) || !QFileInfo(resolved).isFile())
        result.errors.append(
            QStringLiteral("entry point file not found: '%1'").arg(ep));
    }
  }

  if (m.folder.contains(QStringLiteral("replaceListing"))) {
    const QJsonValue v = m.folder.value(QStringLiteral("replaceListing"));
    if (!(v.isBool() && !v.toBool()))
      result.errors.append(
          QStringLiteral("folder.replaceListing is not supported"));
  }

  if (result.errors.isEmpty())
    validateKindRequirements(m, &result.errors);

  if (hasSymlinkOutsideGit(root))
    result.errors.append(
        QStringLiteral("symlinks are not allowed inside a handler folder"));

  result.ok = result.errors.isEmpty();
  result.manifest = m;
  return result;
}

bool manifestMatches(const Manifest &m, const QString &kind,
                     const QVector<Manifest::Item> &items) {
  if (!m.hasKind(kind))
    return false;
  const QString host = m.match.host.isEmpty()
                           ? QStringLiteral("posix-local")
                           : m.match.host;
  if (host != QLatin1String("posix-local"))
    return false;

  const int minItems = m.match.minItems >= 0 ? m.match.minItems
                                             : defaultMinItems(kind);
  const int maxItems = m.match.maxItems >= 0 ? m.match.maxItems
                                             : defaultMaxItems(kind);
  if (items.size() < minItems || items.size() > maxItems)
    return false;

  auto mimeOk = [&](const Manifest::Item &item) {
    if (m.match.mime.isEmpty())
      return true;
    for (const QString &pat : m.match.mime) {
      if (Manifest::matchMime(item.mime, pat))
        return true;
    }
    return false;
  };
  auto pathOk = [&](const Manifest::Item &item) {
    if (m.match.pathGlob.isEmpty())
      return true;
    for (const QString &pat : m.match.pathGlob) {
      if (Manifest::matchPathGlob(item.path, pat))
        return true;
    }
    return false;
  };
  auto suffixOk = [&](const Manifest::Item &item) {
    if (m.match.suffix.isEmpty())
      return true;
    for (const QString &suf : m.match.suffix) {
      if (Manifest::matchSuffix(item.path, suf))
        return true;
    }
    return false;
  };

  if (!m.match.mime.isEmpty()) {
    if (modeAll(m.match.mimeMode)) {
      for (const auto &item : items) {
        if (!mimeOk(item))
          return false;
      }
    } else {
      bool any = false;
      for (const auto &item : items) {
        if (mimeOk(item)) {
          any = true;
          break;
        }
      }
      if (!any)
        return false;
    }
  }

  if (!m.match.pathGlob.isEmpty()) {
    if (modeAll(m.match.pathMode)) {
      for (const auto &item : items) {
        if (!pathOk(item))
          return false;
      }
    } else {
      bool any = false;
      for (const auto &item : items) {
        if (pathOk(item)) {
          any = true;
          break;
        }
      }
      if (!any)
        return false;
    }
  }

  if (!m.match.suffix.isEmpty()) {
    for (const auto &item : items) {
      if (!suffixOk(item))
        return false;
    }
  }

  if (!m.match.folderContains.isEmpty()) {
    for (const auto &item : items) {
      const QString dir =
          item.isDir ? item.path : QFileInfo(item.path).absolutePath();
      for (const QString &name : m.match.folderContains) {
        if (!QFileInfo::exists(QDir(dir).filePath(name)))
          return false;
      }
    }
  }
  return true;
}

int manifestSpecificity(const Manifest &m,
                        const QVector<Manifest::Item> &items) {
  int spec = 0;
  for (const auto &item : items) {
    for (const QString &pat : m.match.mime) {
      if (!Manifest::matchMime(item.mime, pat))
        continue;
      if (pat == item.mime)
        spec = qMax(spec, 3);
      else
        spec = qMax(spec, 2);
    }
    for (const QString &suf : m.match.suffix) {
      if (Manifest::matchSuffix(item.path, suf))
        spec = qMax(spec, 1);
    }
    if (!m.match.pathGlob.isEmpty())
      spec = qMax(spec, 1);
  }
  return spec;
}
