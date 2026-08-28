#include "OmaflowBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

namespace {

constexpr qsizetype kStateFileLimit = 4 * 1024 * 1024;
constexpr qsizetype kOperationOutputLimit = 64 * 1024;
constexpr qsizetype kAuthorRequestLimit = 2000;
constexpr qsizetype kContextFileLimit = 1024 * 1024;

QString envPath(const char *name, const QString &fallback) {
  const QString value = qEnvironmentVariable(name);
  return value.isEmpty() ? fallback : QDir::cleanPath(value);
}

QByteArray readBounded(const QString &path, qsizetype limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
    return {};
  return file.readAll();
}

QJsonObject readObject(const QString &path,
                       qsizetype limit = kStateFileLimit) {
  QJsonParseError error;
  const QJsonDocument doc =
      QJsonDocument::fromJson(readBounded(path, limit), &error);
  return error.error == QJsonParseError::NoError && doc.isObject()
             ? doc.object()
             : QJsonObject{};
}

QString conciseOutput(const QByteArray &bytes) {
  QString text = QString::fromUtf8(bytes).trimmed();
  if (text.size() > 4000)
    text = text.left(3999) + QChar(0x2026);
  return text;
}

QStringList stringList(const QVariant &value) {
  QStringList out;
  for (const QVariant &entry : value.toList()) {
    const QString text = entry.toString();
    if (!text.isEmpty())
      out.append(text);
  }
  return out;
}

QString globToRegex(const QString &pattern) {
  QString rx = QStringLiteral("^");
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
  return rx + QLatin1Char('$');
}

bool mimeMatches(const QString &mime, const QString &pattern) {
  if (pattern == mime || pattern == QLatin1String("*") ||
      pattern == QLatin1String("*/*"))
    return true;
  if (pattern.endsWith(QLatin1String("/*")))
    return mime.startsWith(pattern.left(pattern.size() - 1));
  return QDir::match(pattern, mime);
}

bool anyMimeMatches(const QString &mime, const QStringList &patterns) {
  for (const QString &pattern : patterns) {
    if (mimeMatches(mime, pattern))
      return true;
  }
  return false;
}

bool anySuffixMatches(const QString &path, const QStringList &suffixes) {
  for (const QString &suffix : suffixes) {
    if (path.endsWith(suffix, Qt::CaseInsensitive))
      return true;
  }
  return false;
}

bool anyPathMatches(const QString &path, const QStringList &patterns) {
  for (const QString &pattern : patterns) {
    const QString target =
        pattern.contains(QLatin1Char('/')) ? path : QFileInfo(path).fileName();
    if (QRegularExpression(globToRegex(pattern)).match(target).hasMatch())
      return true;
  }
  return false;
}

bool anyMarkerExists(const QString &dir, const QStringList &markers) {
  for (const QString &marker : markers) {
    if (QFileInfo::exists(QDir(dir).filePath(marker)))
      return true;
  }
  return false;
}

bool acceptsSelection(const QVariantMap &rule, const QVariantList &selection) {
  if (!rule.value(QStringLiteral("enabled")).toBool() || selection.isEmpty())
    return false;
  const QVariantMap accepts = rule.value(QStringLiteral("accepts")).toMap();
  if (accepts.isEmpty())
    return false;
  const int count = selection.size();
  const int minItems = accepts.value(QStringLiteral("minItems"), 1).toInt();
  const int maxItems = accepts.value(QStringLiteral("maxItems"), 10000).toInt();
  if (count < minItems || count > maxItems)
    return false;

  const QString kind =
      accepts.value(QStringLiteral("kind"), QStringLiteral("any")).toString();
  const QStringList mimes = stringList(accepts.value(QStringLiteral("mime")));
  const QStringList suffixes =
      stringList(accepts.value(QStringLiteral("suffix")));
  const QStringList paths =
      stringList(accepts.value(QStringLiteral("pathGlob")));
  const QStringList markers =
      stringList(accepts.value(QStringLiteral("folderContains")));
  for (const QVariant &value : selection) {
    const QVariantMap item = value.toMap();
    const QString path = item.value(QStringLiteral("path")).toString();
    const QString mime = item.value(QStringLiteral("mime")).toString();
    const bool isDir = item.value(QStringLiteral("isDir")).toBool();
    if (path.isEmpty() || (kind == QLatin1String("files") && isDir) ||
        (kind == QLatin1String("folders") && !isDir))
      return false;
    if (!mimes.isEmpty() && !anyMimeMatches(mime, mimes))
      return false;
    if (!suffixes.isEmpty() && !anySuffixMatches(path, suffixes))
      return false;
    if (!paths.isEmpty() && !anyPathMatches(path, paths))
      return false;
    const QString dir = isDir ? path : QFileInfo(path).absolutePath();
    if (!markers.isEmpty() && !anyMarkerExists(dir, markers))
      return false;
  }
  return true;
}

QString matchReason(const QVariantMap &rule, int count) {
  const QVariantMap accepts = rule.value(QStringLiteral("accepts")).toMap();
  QStringList facts;
  const QString kind = accepts.value(QStringLiteral("kind")).toString();
  facts.append(QStringLiteral("%1 selected %2")
                   .arg(count)
                   .arg(kind == QLatin1String("folders")
                            ? (count == 1 ? QStringLiteral("folder")
                                          : QStringLiteral("folders"))
                            : (count == 1 ? QStringLiteral("file")
                                          : QStringLiteral("files"))));
  const QStringList mimes = stringList(accepts.value(QStringLiteral("mime")));
  const QStringList suffixes =
      stringList(accepts.value(QStringLiteral("suffix")));
  const QStringList markers =
      stringList(accepts.value(QStringLiteral("folderContains")));
  if (!mimes.isEmpty())
    facts.append(mimes.join(QStringLiteral(" or ")));
  else if (!suffixes.isEmpty())
    facts.append(suffixes.join(QStringLiteral(" or ")));
  if (!markers.isEmpty())
    facts.append(
        QStringLiteral("has %1").arg(markers.join(QStringLiteral(", "))));
  return facts.join(QStringLiteral(" · "));
}

} // namespace

OmaflowBridge::OmaflowBridge(QObject *parent) : QObject(parent) {
  const QString configHome = envPath(
      "XDG_CONFIG_HOME", QDir::home().filePath(QStringLiteral(".config")));
  const QString stateHome = envPath(
      "XDG_STATE_HOME", QDir::home().filePath(QStringLiteral(".local/state")));
  m_configDir = envPath("SYNCHRO_OMAFLOW_CONFIG_DIR",
                        QDir(configHome).filePath(QStringLiteral("omaflow")));
  m_stateDir = envPath("SYNCHRO_OMAFLOW_STATE_DIR",
                       QDir(stateHome).filePath(QStringLiteral("omaflow")));

  m_watcher = new QFileSystemWatcher(this);
  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setSingleShot(true);
  m_refreshTimer->setInterval(80);
  m_pollTimer = new QTimer(this);
  m_pollTimer->setInterval(2000);
  m_operationTimer = new QTimer(this);
  m_operationTimer->setSingleShot(true);
  m_operationTimer->setInterval(45000);

  connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
          [this] { scheduleRefresh(); });
  connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
          [this] { scheduleRefresh(); });
  connect(m_refreshTimer, &QTimer::timeout, this, &OmaflowBridge::refresh);
  connect(m_pollTimer, &QTimer::timeout, this, &OmaflowBridge::refresh);
  connect(m_operationTimer, &QTimer::timeout, this, [this] {
    if (!m_process)
      return;
    m_process->kill();
    finishOperation(false, QStringLiteral("Omaflow operation timed out"));
  });

  refresh();
}

OmaflowBridge::~OmaflowBridge() {
  if (!m_contextPath.isEmpty())
    QFile::remove(m_contextPath);
  if (!m_resultPath.isEmpty())
    QFile::remove(m_resultPath);
}

void OmaflowBridge::setActive(bool active) {
  if (m_active == active)
    return;
  m_active = active;
  if (m_active) {
    refresh();
    m_pollTimer->start();
  } else {
    m_pollTimer->stop();
  }
  emit activeChanged();
}

void OmaflowBridge::scheduleRefresh() {
  if (!m_refreshTimer->isActive())
    m_refreshTimer->start();
}

QString OmaflowBridge::discoverFromPluginRoot(const QString &root,
                                              QString *version) const {
  QDir plugins(root);
  if (!plugins.exists())
    return {};
  const QStringList entries =
      plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString &entry : entries) {
    const QString dir = plugins.filePath(entry);
    const QJsonObject manifest =
        readObject(QDir(dir).filePath(QStringLiteral("manifest.json")));
    const QString id = manifest.value(QStringLiteral("id")).toString();
    const QString name = manifest.value(QStringLiteral("name")).toString();
    if (!id.contains(QStringLiteral("omaflow"), Qt::CaseInsensitive) &&
        name.compare(QStringLiteral("Omaflow"), Qt::CaseInsensitive) != 0)
      continue;
    const QString candidate = QDir(dir).filePath(QStringLiteral("bin/omaflow"));
    const QFileInfo info(candidate);
    if (!info.isFile() || !info.isExecutable())
      continue;
    if (version)
      *version = manifest.value(QStringLiteral("version")).toString();
    return info.absoluteFilePath();
  }
  return {};
}

void OmaflowBridge::discoverExecutable() {
  QString nextVersion;
  QString next = qEnvironmentVariable("SYNCHRO_OMAFLOW_BIN");
  if (!next.isEmpty()) {
    const QFileInfo info(next);
    next = info.isFile() && info.isExecutable() ? info.absoluteFilePath()
                                                : QString();
  }
  if (next.isEmpty())
    next = QStandardPaths::findExecutable(QStringLiteral("omaflow"));
  if (next.isEmpty()) {
    const QString configHome = envPath(
        "XDG_CONFIG_HOME", QDir::home().filePath(QStringLiteral(".config")));
    next = discoverFromPluginRoot(
        QDir(configHome).filePath(QStringLiteral("omarchy/plugins")),
        &nextVersion);
  }
  if (next.isEmpty()) {
    const QString dataHome = envPath(
        "XDG_DATA_HOME", QDir::home().filePath(QStringLiteral(".local/share")));
    next = discoverFromPluginRoot(
        QDir(dataHome).filePath(QStringLiteral("omarchy/plugins")),
        &nextVersion);
  }
  if (next == m_executable && nextVersion == m_version)
    return;
  m_executable = next;
  m_version = nextVersion;
  emit installedChanged();
}

void OmaflowBridge::reloadState() {
  const QJsonObject index =
      readObject(QDir(m_stateDir).filePath(QStringLiteral("index.json")));
  const QJsonArray indexed = index.value(QStringLiteral("rules")).toArray();
  QVariantList nextRules;
  nextRules.reserve(indexed.size());
  for (const QJsonValue &value : indexed) {
    if (!value.isObject())
      continue;
    const QJsonObject summary = value.toObject();
    const QString id = summary.value(QStringLiteral("id")).toString();
    static const QRegularExpression safeId(
        QStringLiteral("^[a-z0-9][a-z0-9-]{0,80}$"));
    QJsonObject merged = summary;
    if (safeId.match(id).hasMatch()) {
      const QJsonObject detail = readObject(
          QDir(m_configDir).filePath(QStringLiteral("rules/%1.json").arg(id)));
      for (auto it = detail.begin(); it != detail.end(); ++it)
        merged.insert(it.key(), it.value());
      // Runtime index values are more current than the rule file.
      for (const QString &key :
           {QStringLiteral("lastFired"), QStringLiteral("triggerSummary"),
            QStringLiteral("actionsSummary"), QStringLiteral("actionCount"),
            QStringLiteral("conditionCount")}) {
        if (summary.contains(key))
          merged.insert(key, summary.value(key));
      }
    }
    nextRules.append(merged.toVariantMap());
  }
  if (nextRules != m_rules) {
    m_rules = nextRules;
    emit rulesChanged();
  }

  QVariantList nextActivity;
  const QByteArray log = readBounded(
      QDir(m_stateDir).filePath(QStringLiteral("log.jsonl")), kStateFileLimit);
  const QList<QByteArray> lines = log.split('\n');
  for (qsizetype i = lines.size() - 1; i >= 0 && nextActivity.size() < 20;
       --i) {
    if (lines.at(i).trimmed().isEmpty())
      continue;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(lines.at(i), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject())
      nextActivity.append(doc.object().toVariantMap());
  }
  if (nextActivity != m_activity) {
    m_activity = nextActivity;
    emit activityChanged();
  }

  const QVariantMap nextStaging =
      readObject(QDir(m_stateDir).filePath(QStringLiteral("staging.json")))
          .toVariantMap();
  if (nextStaging != m_staging) {
    m_staging = nextStaging;
    emit stagingChanged();
  }
}

void OmaflowBridge::rearmWatcher() {
  const QStringList oldFiles = m_watcher->files();
  const QStringList oldDirs = m_watcher->directories();
  if (!oldFiles.isEmpty())
    m_watcher->removePaths(oldFiles);
  if (!oldDirs.isEmpty())
    m_watcher->removePaths(oldDirs);

  QStringList dirs;
  QStringList files;
  const QString rulesDir = QDir(m_configDir).filePath(QStringLiteral("rules"));
  for (const QString &path :
       {m_stateDir, m_configDir, rulesDir, QFileInfo(m_stateDir).absolutePath(),
        QFileInfo(m_configDir).absolutePath()}) {
    if (QFileInfo(path).isDir() && !dirs.contains(path))
      dirs.append(path);
  }
  for (const QString &name :
       {QStringLiteral("index.json"), QStringLiteral("log.jsonl"),
        QStringLiteral("staging.json")}) {
    const QString path = QDir(m_stateDir).filePath(name);
    if (QFileInfo(path).isFile())
      files.append(path);
  }
  if (!dirs.isEmpty())
    m_watcher->addPaths(dirs);
  if (!files.isEmpty())
    m_watcher->addPaths(files);
}

void OmaflowBridge::refresh() {
  discoverExecutable();
  reloadState();
  rearmWatcher();
}

bool OmaflowBridge::knownRule(const QString &ruleId) const {
  for (const QVariant &value : m_rules) {
    if (value.toMap().value(QStringLiteral("id")).toString() == ruleId)
      return true;
  }
  return false;
}

bool OmaflowBridge::dryRun(const QString &ruleId) {
  return startOperation(ruleId, true);
}

bool OmaflowBridge::run(const QString &ruleId) {
  return startOperation(ruleId, false);
}

QVariantList OmaflowBridge::matchingRules(const QVariantList &selection) const {
  QVariantList out;
  for (const QVariant &value : m_rules) {
    QVariantMap rule = value.toMap();
    if (!acceptsSelection(rule, selection))
      continue;
    rule.insert(QStringLiteral("matchReason"),
                matchReason(rule, selection.size()));
    out.append(rule);
  }
  return out;
}

bool OmaflowBridge::runSelection(const QString &ruleId,
                                 const QVariantList &selection,
                                 const QString &cwd, bool dryRun) {
  return startOperation(ruleId, dryRun, selection, cwd);
}

bool OmaflowBridge::author(const QString &request) {
  const QString text = request.trimmed();
  if (text.isEmpty() || text.size() > kAuthorRequestLimit ||
      text.contains(QChar::Null))
    return false;
  return startCliOperation(QStringLiteral("author"), QString(),
                           {QStringLiteral("author"), text}, 120000);
}

bool OmaflowBridge::acceptStage() {
  if (m_staging.value(QStringLiteral("status")).toString() !=
      QStringLiteral("ready"))
    return false;
  return startCliOperation(QStringLiteral("stage-accept"), QString(),
                           {QStringLiteral("stage"), QStringLiteral("accept")});
}

bool OmaflowBridge::rejectStage() {
  if (m_staging.isEmpty())
    return false;
  return startCliOperation(QStringLiteral("stage-reject"), QString(),
                           {QStringLiteral("stage"), QStringLiteral("reject")});
}

QString OmaflowBridge::writeContext(const QVariantList &selection,
                                    const QString &cwd) {
  QTemporaryFile file(
      QDir::temp().filePath(QStringLiteral("synchro-flow-XXXXXX.json")));
  file.setAutoRemove(false);
  if (!file.open())
    return {};
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QTemporaryFile result(
      QDir::temp().filePath(QStringLiteral("synchro-flow-result-XXXXXX.json")));
  result.setAutoRemove(false);
  if (!result.open()) {
    const QString path = file.fileName();
    file.close();
    QFile::remove(path);
    return {};
  }
  result.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  m_resultPath = result.fileName();
  result.close();
  QJsonArray items;
  for (const QVariant &value : selection) {
    const QVariantMap item = value.toMap();
    QJsonObject row;
    const QString path = item.value(QStringLiteral("path")).toString();
    row.insert(QStringLiteral("path"), path);
    row.insert(QStringLiteral("name"), QFileInfo(path).fileName());
    row.insert(
        QStringLiteral("uri"),
        item.value(QStringLiteral("uri")).toUrl().toString(QUrl::FullyEncoded));
    row.insert(QStringLiteral("mime"),
               item.value(QStringLiteral("mime")).toString());
    row.insert(QStringLiteral("isDir"),
               item.value(QStringLiteral("isDir")).toBool());
    items.append(row);
  }
  QJsonObject context;
  context.insert(QStringLiteral("source"), QStringLiteral("synchro"));
  context.insert(QStringLiteral("cwd"), cwd);
  context.insert(QStringLiteral("count"), selection.size());
  context.insert(QStringLiteral("items"), items);
  context.insert(QStringLiteral("resultFile"), m_resultPath);
  const QByteArray json =
      QJsonDocument(context).toJson(QJsonDocument::Compact) +
      QByteArrayLiteral("\n");
  if (json.size() > kContextFileLimit || file.write(json) != json.size()) {
    const QString path = file.fileName();
    file.close();
    QFile::remove(path);
    QFile::remove(m_resultPath);
    m_resultPath.clear();
    return {};
  }
  const QString path = file.fileName();
  file.close();
  return path;
}

bool OmaflowBridge::startOperation(const QString &ruleId, bool dryRun,
                                   const QVariantList &selection,
                                   const QString &cwd) {
  if (!knownRule(ruleId) || m_process)
    return false;

  QStringList args{QStringLiteral("run"), ruleId};
  if (dryRun)
    args.append(QStringLiteral("--dry-run"));
  if (!selection.isEmpty()) {
    bool matched = false;
    for (const QVariant &value : matchingRules(selection)) {
      if (value.toMap().value(QStringLiteral("id")).toString() == ruleId) {
        matched = true;
        break;
      }
    }
    if (!matched)
      return false;
    m_contextPath = writeContext(selection, cwd);
    if (m_contextPath.isEmpty())
      return false;
    args.append({QStringLiteral("--trigger"),
                 QStringLiteral("selection (synchro)"),
                 QStringLiteral("--context-file"), m_contextPath});
  } else if (!dryRun) {
    args.append(
        {QStringLiteral("--trigger"), QStringLiteral("manual (synchro)")});
  }
  const bool started = startCliOperation(
      dryRun ? QStringLiteral("dry-run") : QStringLiteral("run"), ruleId, args);
  if (!started && !m_contextPath.isEmpty()) {
    QFile::remove(m_contextPath);
    m_contextPath.clear();
  }
  if (!started && !m_resultPath.isEmpty()) {
    QFile::remove(m_resultPath);
    m_resultPath.clear();
  }
  return started;
}

bool OmaflowBridge::startCliOperation(const QString &kind,
                                      const QString &ruleId,
                                      const QStringList &args, int timeoutMs) {
  if (m_process || m_executable.isEmpty() || args.isEmpty())
    return false;

  m_operationRule = ruleId;
  m_operationKind = kind;
  m_operationOutput.clear();
  m_operationResult.clear();
  m_processOutput.clear();
  QProcess *process = new QProcess(this);
  m_process = process;
  process->setProcessChannelMode(QProcess::MergedChannels);
  connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
    if (m_process != process)
      return;
    const QByteArray chunk = process->readAllStandardOutput();
    const qsizetype room = kOperationOutputLimit - m_processOutput.size();
    if (room > 0)
      m_processOutput.append(chunk.left(room));
  });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (m_process == process && error == QProcess::FailedToStart)
              finishOperation(false, QStringLiteral("Could not start Omaflow"));
          });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this, process](int code, QProcess::ExitStatus status) {
            if (m_process != process)
              return;
            finishOperation(status == QProcess::NormalExit && code == 0);
          });

  process->start(m_executable, args);
  m_operationTimer->setInterval(timeoutMs);
  m_operationTimer->start();
  emit operationChanged();
  return true;
}

void OmaflowBridge::finishOperation(bool ok, const QString &fallback) {
  if (!m_process)
    return;
  m_operationTimer->stop();
  const QByteArray tail = m_process->readAllStandardOutput();
  const qsizetype room = kOperationOutputLimit - m_processOutput.size();
  if (room > 0)
    m_processOutput.append(tail.left(room));
  const QString output = conciseOutput(m_processOutput);
  m_operationOutput =
      output.isEmpty()
          ? (fallback.isEmpty() ? (ok ? QStringLiteral("Completed")
                                      : QStringLiteral("Omaflow failed"))
                                : fallback)
          : output;
  if (!m_resultPath.isEmpty()) {
    const QJsonObject result = readObject(m_resultPath, kContextFileLimit);
    if (!result.isEmpty())
      m_operationResult = result.toVariantMap();
  }
  QProcess *process = m_process;
  m_process = nullptr;
  process->deleteLater();
  if (!m_contextPath.isEmpty()) {
    QFile::remove(m_contextPath);
    m_contextPath.clear();
  }
  if (!m_resultPath.isEmpty()) {
    QFile::remove(m_resultPath);
    m_resultPath.clear();
  }
  emit operationChanged();
  emit operationFinished(ok);
  scheduleRefresh();
}

void OmaflowBridge::cancel() {
  if (!m_process)
    return;
  QProcess *process = m_process;
  const bool discardStaging = m_operationKind == QStringLiteral("author");
  disconnect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
             this, nullptr);
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this, process, discardStaging] {
            if (m_process != process)
              return;
            finishOperation(false, QStringLiteral("Cancelled"));
            if (discardStaging)
              startCliOperation(
                  QStringLiteral("stage-reject"), QString(),
                  {QStringLiteral("stage"), QStringLiteral("reject")});
          });
  process->kill();
}
