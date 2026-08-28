#include "AgentIntegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>
#include <sqlite3.h>

namespace {

QString effectiveHome(const QString &overrideHome) {
  return overrideHome.isEmpty() ? QDir::homePath()
                                : QFileInfo(overrideHome).absoluteFilePath();
}

QStringList skillTargets(const QString &home) {
  return {
      QDir(home).filePath(QStringLiteral(".agents/skills/synchro")),
      QDir(home).filePath(QStringLiteral(".claude/skills/synchro")),
      QDir(home).filePath(QStringLiteral(".codex/skills/synchro")),
      QDir(home).filePath(QStringLiteral(".pi/agent/skills/synchro")),
  };
}

QString canonicalOrAbsolute(const QString &path) {
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath()
                                             : canonical);
}

AgentSkillStatus skillStatus(const QString &homeOverride,
                             const QString &sourceOverride, bool install) {
  AgentSkillStatus status;
  status.sourceDir = sourceOverride.isEmpty()
                         ? AgentIntegration::skillSourceDir()
                         : QFileInfo(sourceOverride).absoluteFilePath();
  const QString skillFile =
      QDir(status.sourceDir).filePath(QStringLiteral("SKILL.md"));
  if (!QFileInfo::exists(skillFile)) {
    status.errors.append(
        QStringLiteral("Synchro agent skill source is missing: %1")
            .arg(skillFile));
    return status;
  }

  const QString source = canonicalOrAbsolute(status.sourceDir);
  for (const QString &target : skillTargets(effectiveHome(homeOverride))) {
    QFileInfo targetInfo(target);
    if (targetInfo.isSymLink()) {
      if (canonicalOrAbsolute(targetInfo.symLinkTarget()) == source)
        status.present.append(target);
      else
        status.conflicts.append(target);
      continue;
    }
    if (targetInfo.exists()) {
      status.conflicts.append(target);
      continue;
    }
    if (!install)
      continue;
    if (!QDir().mkpath(targetInfo.absolutePath())) {
      status.errors.append(
          QStringLiteral("Could not create %1").arg(targetInfo.absolutePath()));
      continue;
    }
    if (!QFile::link(source, target)) {
      status.errors.append(
          QStringLiteral("Could not link %1 to %2").arg(target, source));
      continue;
    }
    status.installed.append(target);
  }
  return status;
}

QJsonArray strings(const QStringList &values) {
  return QJsonArray::fromStringList(values);
}

QString configuredAgent(const QString &home) {
  QFile file(
      QDir(home).filePath(QStringLiteral(".config/omarchy/defaults/agent")));
  if (file.open(QIODevice::ReadOnly))
    return QString::fromUtf8(file.read(4096)).trimmed();

  const QString helper =
      QStandardPaths::findExecutable(QStringLiteral("omarchy-default-agent"));
  if (helper.isEmpty())
    return {};
  QProcess process;
  process.start(helper, {});
  if (!process.waitForFinished(2000)) {
    process.kill();
    process.waitForFinished();
    return {};
  }
  return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString catalogDirectory() {
  const QString override = QString::fromLocal8Bit(qgetenv("SYNCHRO_HOME"));
  return override.isEmpty()
             ? QDir::home().filePath(QStringLiteral(".local/share/synchro"))
             : QFileInfo(override).absoluteFilePath();
}

QString omaflowExecutable(const QString &home = QDir::homePath()) {
  const QString override =
      QString::fromLocal8Bit(qgetenv("SYNCHRO_OMAFLOW_BIN"));
  if (!override.isEmpty()) {
    const QFileInfo info(override);
    if (info.isFile() && info.isExecutable())
      return info.absoluteFilePath();
  }
  const QString fromPath =
      QStandardPaths::findExecutable(QStringLiteral("omaflow"));
  if (!fromPath.isEmpty())
    return fromPath;

  QString configHome = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
  if (configHome.isEmpty())
    configHome = QDir(home).filePath(QStringLiteral(".config"));
  QString dataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
  if (dataHome.isEmpty())
    dataHome = QDir(home).filePath(QStringLiteral(".local/share"));

  for (const QString &root :
       {QDir(configHome).filePath(QStringLiteral("omarchy/plugins")),
        QDir(dataHome).filePath(QStringLiteral("omarchy/plugins"))}) {
    const QDir plugins(root);
    for (const QString &entry :
         plugins.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
      const QString dir = plugins.filePath(entry);
      QFile manifestFile(QDir(dir).filePath(QStringLiteral("manifest.json")));
      if (!manifestFile.open(QIODevice::ReadOnly))
        continue;
      const QJsonObject manifest =
          QJsonDocument::fromJson(manifestFile.readAll()).object();
      const QString id = manifest.value(QStringLiteral("id")).toString();
      const QString name = manifest.value(QStringLiteral("name")).toString();
      if (!id.contains(QStringLiteral("omaflow"), Qt::CaseInsensitive) &&
          name.compare(QStringLiteral("Omaflow"), Qt::CaseInsensitive) != 0)
        continue;
      const QFileInfo candidate(
          QDir(dir).filePath(QStringLiteral("bin/omaflow")));
      if (candidate.isFile() && candidate.isExecutable())
        return candidate.absoluteFilePath();
    }
  }
  return {};
}

QString quotedCommand(QString value) {
  value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
  return QLatin1Char('"') + value + QLatin1Char('"');
}

QString compactProcessOutput(QByteArray bytes) {
  if (bytes.size() > 64 * 1024)
    bytes = bytes.right(64 * 1024);
  QString text = QString::fromUtf8(bytes);
  static const QRegularExpression ansi(
      QStringLiteral("\\x1b(?:\\[[0-?]*[ -/]*[@-~]|\\][^\\x07]*(?:\\x07|\\x1b\\\\))"));
  text.remove(ansi);
  text = text.trimmed();
  if (text.size() > 1200)
    text = text.right(1199).prepend(QChar(0x2026));
  return text;
}

QByteArray jsonObjectAfterMarker(const QByteArray &bytes) {
  const QByteArray marker("SYNCHRO_RESULT");
  const qsizetype markerAt = bytes.lastIndexOf(marker);
  if (markerAt < 0)
    return {};
  const qsizetype start = bytes.indexOf('{', markerAt + marker.size());
  if (start < 0)
    return {};
  int depth = 0;
  bool quoted = false;
  bool escaped = false;
  for (qsizetype i = start; i < bytes.size(); ++i) {
    const char c = bytes.at(i);
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c == '{')
      ++depth;
    else if (c == '}' && --depth == 0)
      return bytes.mid(start, i - start + 1);
  }
  return {};
}

QStringList cachedThumbnailSample(quint32 salt) {
  struct Candidate {
    quint32 score = 0;
    QString url;
  };
  QVector<Candidate> candidates;
  const QString home = qEnvironmentVariableIsSet("SYNCHRO_HOME")
                           ? qEnvironmentVariable("SYNCHRO_HOME")
                           : QDir::home().filePath(QStringLiteral(".synchro"));
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(
          QFile::encodeName(QDir(home).filePath(QStringLiteral("thumbs.sqlite")))
              .constData(),
          &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) !=
      SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return {};
  }
  sqlite3_busy_timeout(db, 1200);
  sqlite3_stmt *statement = nullptr;
  static const char *sql =
      "SELECT key,path FROM thumbs "
      "WHERE path NOT LIKE '%#mosaic-v%' "
      "AND path NOT LIKE '%#query-mosaic-v%' "
      "ORDER BY accessed DESC LIMIT 2400;";
  if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK) {
    QSet<QString> seenSources;
    while (sqlite3_step(statement) == SQLITE_ROW) {
      const auto *keyText = sqlite3_column_text(statement, 0);
      const auto *pathText = sqlite3_column_text(statement, 1);
      if (!keyText || !pathText)
        continue;
      const QString key =
          QString::fromUtf8(reinterpret_cast<const char *>(keyText));
      const QString identity =
          QString::fromUtf8(reinterpret_cast<const char *>(pathText));
      QString sourcePath = identity;
      const int cardSuffix = sourcePath.lastIndexOf(QStringLiteral("#card-v"));
      if (cardSuffix > 0)
        sourcePath.truncate(cardSuffix);
      // Folder and SQL-group mosaics are cache identities, never source files.
      // The QFileInfo check also rejects stale entries and virtual locations.
      if (seenSources.contains(sourcePath) || !QFileInfo(sourcePath).isFile())
        continue;
      seenSources.insert(sourcePath);
      candidates.append({static_cast<quint32>(qHash(key, salt)),
                         QStringLiteral("image://synchrothumb/") + key});
    }
  }
  if (statement)
    sqlite3_finalize(statement);
  sqlite3_close(db);
  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &a, const Candidate &b) {
              return a.score < b.score;
            });
  QStringList urls;
  const int count = qMin(96, candidates.size());
  urls.reserve(count);
  for (int i = 0; i < count; ++i)
    urls.append(candidates.at(i).url);
  return urls;
}

QJsonObject catalogContract(const QString &binary) {
  const QString command = quotedCommand(binary);
  const QJsonArray commonFields{
      QStringLiteral("path"),
      QStringLiteral("parent"),
      QStringLiteral("name"),
      QStringLiteral("extension"),
      QStringLiteral("kind"),
      QStringLiteral("stem"),
      QStringLiteral("size"),
      QStringLiteral("kb"),
      QStringLiteral("mb"),
      QStringLiteral("gb"),
      QStringLiteral("size_bucket"),
      QStringLiteral("mtime"),
      QStringLiteral("modified_date"),
      QStringLiteral("modified_month"),
      QStringLiteral("age_days"),
      QStringLiteral("age_bucket"),
      QStringLiteral("hidden"),
      QStringLiteral("is_hidden"),
      QStringLiteral("is_dir"),
      QStringLiteral("is_symlink"),
      QStringLiteral("depth"),
      QStringLiteral("root"),
      QStringLiteral("mime"),
      QStringLiteral("file_id"),
      QStringLiteral("device"),
      QStringLiteral("inode"),
  };
  QJsonArray factFields = commonFields;
  for (const QString &field :
       {QStringLiteral("analyzer"), QStringLiteral("analyzer_version"),
        QStringLiteral("key"), QStringLiteral("text_value"),
        QStringLiteral("numeric_value"), QStringLiteral("updated_at")})
    factFields.append(field);
  QJsonArray imageFields = commonFields;
  for (const QString &field :
       {QStringLiteral("width"), QStringLiteral("height"),
        QStringLiteral("aspect_ratio"), QStringLiteral("orientation"),
        QStringLiteral("dominant_color"), QStringLiteral("average_color"),
        QStringLiteral("palette_0"), QStringLiteral("palette_1"),
        QStringLiteral("palette_2"), QStringLiteral("palette_weight_0"),
        QStringLiteral("palette_weight_1"), QStringLiteral("palette_weight_2"),
        QStringLiteral("color_family"), QStringLiteral("brightness"),
        QStringLiteral("saturation"), QStringLiteral("blue_share"),
        QStringLiteral("chromatic_share"), QStringLiteral("visual_hash")})
    imageFields.append(field);
  return {
      {QStringLiteral("required"), true},
      {QStringLiteral("readOnly"), true},
      {QStringLiteral("queryCommand"),
       command + QStringLiteral(" agent query --sql <SELECT> --cwd <folder>")},
      {QStringLiteral("showCommand"),
       command + QStringLiteral(" agent show --sql <SELECT> --cwd <folder> "
                                "--label <name>")},
      {QStringLiteral("relations"),
       QJsonArray{QStringLiteral("files"), QStringLiteral("here"),
                  QStringLiteral("tree"), QStringLiteral("selection"),
                  QStringLiteral("facts"), QStringLiteral("image_facts"),
                  QStringLiteral("projects")}},
      {QStringLiteral("relationFields"),
       QJsonObject{
           {QStringLiteral("files"), commonFields},
           {QStringLiteral("here"), commonFields},
           {QStringLiteral("tree"), commonFields},
           {QStringLiteral("selection"), commonFields},
           {QStringLiteral("facts"), factFields},
           {QStringLiteral("image_facts"), imageFields},
           {QStringLiteral("projects"),
            QJsonArray{QStringLiteral("path"), QStringLiteral("name"),
                       QStringLiteral("is_dir"), QStringLiteral("size"),
                       QStringLiteral("mtime"), QStringLiteral("project_type"),
                       QStringLiteral("markers")}}}},
      {QStringLiteral("examples"),
       QJsonArray{
           command +
               QStringLiteral(" agent query --cwd \"$SYNCHRO_CWD\" --sql "
                              "\"select name,path,extension,kind,mb "
                              "from tree where not is_dir and extension "
                              "in ('jpg','jpeg','png','gif','webp','avif',"
                              "'bmp','tif','tiff','heic','heif','svg') "
                              "order by size desc limit 20\""),
           command + QStringLiteral(" agent query --cwd \"$SYNCHRO_CWD\" "
                                    "--sql "
                                    "\"select * from selection order by "
                                    "path\""),
           command + QStringLiteral(" agent query --cwd \"$SYNCHRO_CWD\" "
                                    "--sql "
                                    "\"select name,project_type,markers,path "
                                    "from projects order by mtime desc\"")}},
      {QStringLiteral("resultRules"),
       QJsonArray{
           QStringLiteral("Output is JSON."),
           QStringLiteral("Only SELECT and WITH queries are accepted."),
           QStringLiteral("Use agent query rather than query during an agent "
                          "handoff; its narrow Omarchy user-service broker can "
                          "read the catalog without broadening the agent "
                          "sandbox."),
           QStringLiteral("The context already lists relation fields; do not "
                          "probe schemas with SELECT * before querying."),
           QStringLiteral("On multi-million-row catalogs, filter images by "
                          "extension before sorting; kind is derived and more "
                          "expensive."),
           QStringLiteral("Use tree for the recursive cwd scope, here for one "
                          "directory, selection for explicit items, and files "
                          "only for a deliberately catalog-wide query."),
           QStringLiteral("Check catalog.coverageComplete and truncated before "
                          "calling a result exhaustive."),
           QStringLiteral(
               "Prefer deterministic metadata before inspecting file "
               "contents.")}},
  };
}

} // namespace

bool AgentSkillStatus::ok() const {
  return errors.isEmpty() && conflicts.isEmpty() &&
         installed.size() + present.size() == 4;
}

QJsonObject AgentSkillStatus::toJson() const {
  return {{QStringLiteral("ok"), ok()},
          {QStringLiteral("source"), sourceDir},
          {QStringLiteral("installed"), strings(installed)},
          {QStringLiteral("present"), strings(present)},
          {QStringLiteral("conflicts"), strings(conflicts)},
          {QStringLiteral("errors"), strings(errors)}};
}

QString AgentIntegration::skillSourceDir() {
  const QString override =
      QString::fromLocal8Bit(qgetenv("SYNCHRO_AGENT_SKILL_DIR"));
  if (!override.isEmpty())
    return QFileInfo(override).absoluteFilePath();

  const QString installed =
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("../share/synchro/agent-skills/synchro"));
  if (QFileInfo::exists(QDir(installed).filePath(QStringLiteral("SKILL.md"))))
    return QDir::cleanPath(installed);

#ifdef SYNCHRO_AGENT_SKILL_SOURCE
  return QStringLiteral(SYNCHRO_AGENT_SKILL_SOURCE);
#else
  return {};
#endif
}

AgentSkillStatus AgentIntegration::installSkill(const QString &home,
                                                const QString &sourceDir) {
  return skillStatus(home, sourceDir, true);
}

AgentSkillStatus AgentIntegration::inspectSkill(const QString &home,
                                                const QString &sourceDir) {
  return skillStatus(home, sourceDir, false);
}

QJsonObject AgentIntegration::context(const QString &manifestOverride,
                                      const QString &cwdOverride) {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString binary = env.value(QStringLiteral("SYNCHRO_BIN"));
  if (binary.isEmpty())
    binary = QCoreApplication::applicationFilePath();
  const QString manifestPath =
      manifestOverride.isEmpty()
          ? env.value(QStringLiteral("SYNCHRO_SELECTION"))
          : manifestOverride;
  QJsonObject manifest;
  QStringList warnings;
  if (!manifestPath.isEmpty()) {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
      warnings.append(QStringLiteral("Could not read selection manifest: %1")
                          .arg(manifestPath));
    } else if (file.size() > 4 * 1024 * 1024) {
      warnings.append(QStringLiteral("Selection manifest exceeds 4 MiB."));
    } else {
      QJsonParseError error;
      const QJsonDocument document =
          QJsonDocument::fromJson(file.readAll(), &error);
      if (!document.isObject())
        warnings.append(QStringLiteral("Invalid selection manifest: %1")
                            .arg(error.errorString()));
      else
        manifest = document.object();
    }
  }

  QString cwd = cwdOverride;
  if (cwd.isEmpty())
    cwd = manifest.value(QStringLiteral("cwd")).toString();
  if (cwd.isEmpty())
    cwd = env.value(QStringLiteral("SYNCHRO_CWD"));
  if (cwd.isEmpty())
    cwd = QDir::currentPath();
  cwd = QDir::cleanPath(QFileInfo(cwd).absoluteFilePath());

  const QJsonArray selection =
      manifest.value(QStringLiteral("items")).toArray();
  const bool omaflowAvailable = !omaflowExecutable().isEmpty();
  QJsonObject capabilities{
      {QStringLiteral("catalog"), catalogContract(binary)},
      {QStringLiteral("omaflow"),
       QJsonObject{{QStringLiteral("available"), omaflowAvailable},
                   {QStringLiteral("authoring"),
                    QStringLiteral("Omaflow authoring is isolated and "
                                   "validated; use Synchro's Flow UI for "
                                   "draft review and installation.")}}},
      {QStringLiteral("mcp"),
       QJsonObject{
           {QStringLiteral("required"), false},
           {QStringLiteral("command"), QStringLiteral("synchro mcp --stdio")},
           {QStringLiteral("note"),
            QStringLiteral("Optional typed convenience only; every "
                           "required catalog operation is available "
                           "through the CLI contract above.")}}}};

  return {
      {QStringLiteral("ok"), warnings.isEmpty()},
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("application"), QStringLiteral("Synchro")},
      {QStringLiteral("binary"), binary},
      {QStringLiteral("cwd"), cwd},
      {QStringLiteral("selectionManifest"), manifestPath},
      {QStringLiteral("selectionCount"), selection.size()},
      {QStringLiteral("selection"), selection},
      {QStringLiteral("capabilities"), capabilities},
      {QStringLiteral("instructions"),
       QJsonArray{
           QStringLiteral("Treat cwd and selection as the user's explicit "
                          "working context."),
           QStringLiteral(
               "Use synchro query for indexed discovery and "
               "deterministic metadata before walking the file tree."),
           QStringLiteral(
               "Use synchro agent show when a result should become a "
               "navigable Synchro pseudo-folder."),
           QStringLiteral("Do not require or attempt to configure MCP.")}},
      {QStringLiteral("warnings"), strings(warnings)},
  };
}

QJsonObject AgentIntegration::doctor(const QString &homeOverride,
                                     const QString &sourceDir) {
  const QString home = effectiveHome(homeOverride);
  const QString omarchy =
      QStandardPaths::findExecutable(QStringLiteral("omarchy"));
  const QString agent = configuredAgent(home);
  const QString agentExecutable =
      agent.isEmpty() ? QString() : QStandardPaths::findExecutable(agent);
  const AgentSkillStatus skill = inspectSkill(home, sourceDir);
  const bool ready = !omarchy.isEmpty() && !agent.isEmpty() &&
                     !agentExecutable.isEmpty() && skill.ok();

  return {
      {QStringLiteral("ok"), ready},
      {QStringLiteral("omarchy"),
       QJsonObject{{QStringLiteral("available"), !omarchy.isEmpty()},
                   {QStringLiteral("executable"), omarchy}}},
      {QStringLiteral("defaultAgent"),
       QJsonObject{{QStringLiteral("name"), agent},
                   {QStringLiteral("available"), !agentExecutable.isEmpty()},
                   {QStringLiteral("executable"), agentExecutable}}},
      {QStringLiteral("skill"), skill.toJson()},
      {QStringLiteral("catalogCli"),
       QJsonObject{{QStringLiteral("available"), true},
                   {QStringLiteral("executable"),
                    QCoreApplication::applicationFilePath()},
                   {QStringLiteral("onPath"),
                    !QStandardPaths::findExecutable(QStringLiteral("synchro"))
                         .isEmpty()},
                   {QStringLiteral("command"),
                    QStringLiteral("$SYNCHRO_BIN agent query --help")}}},
      {QStringLiteral("mcp"),
       QJsonObject{
           {QStringLiteral("required"), false},
           {QStringLiteral("command"), QStringLiteral("synchro mcp --stdio")}}},
      {QStringLiteral("omaflow"),
       QJsonObject{
           {QStringLiteral("required"), false},
           {QStringLiteral("available"), !omaflowExecutable(home).isEmpty()}}},
  };
}

AgentSearchBridge::AgentSearchBridge(QObject *parent) : QObject(parent) {}

AgentSearchBridge::~AgentSearchBridge() {
  cancel();
  clearResultFile();
}

QString AgentSearchBridge::promptFor(const QString &request,
                                     const QString &cwd,
                                     const QString &binary) {
  QJsonObject context = AgentIntegration::context({}, cwd);
  context.insert(QStringLiteral("binary"), binary);
  const QString compact = QString::fromUtf8(
      QJsonDocument(context).toJson(QJsonDocument::Compact));
  return QStringLiteral(
             "You are Synchro's embedded file-query translator. Turn the "
             "user's request into one useful, navigable, read-only SQL "
             "pseudo-folder. The embedded contract below is authoritative and "
             "complete: do not run shell commands, load additional context, "
             "query the catalog, or inspect the filesystem. Synchro will "
             "validate and execute your SQL after you return it.\n\n"
             "The final SQL must be SELECT/WITH only. File result queries "
             "should include path and is_dir so Synchro can navigate them. Do "
             "not invoke `agent show`; this embedded caller opens the result in "
             "the current Synchro window.\n\n"
             "Embedded context:\n%2\n\n"
             "User request:\n%3\n\n"
             "End with exactly this marker and one compact JSON object, with no "
             "text after it:\nSYNCHRO_RESULT\n"
             "{\"label\":\"short location name\",\"sql\":\"select ...\","
             "\"cwd\":\"%1\"}")
      .arg(cwd, compact, request);
}

QVariantMap AgentSearchBridge::parseResponse(const QByteArray &response,
                                              QString *error) {
  const QByteArray objectBytes = jsonObjectAfterMarker(response);
  if (objectBytes.isEmpty()) {
    if (error)
      *error = QStringLiteral("Agent did not return a Synchro query.");
    return {};
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(objectBytes, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error)
      *error = QStringLiteral("Agent returned invalid query JSON: %1")
                   .arg(parseError.errorString());
    return {};
  }
  QVariantMap result = document.object().toVariantMap();
  QString sql = result.value(QStringLiteral("sql")).toString().trimmed();
  static const QRegularExpression readOnly(
      QStringLiteral("^(?:select|with)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  if (!readOnly.match(sql).hasMatch()) {
    if (error)
      *error = QStringLiteral("Agent result was not a read-only SELECT query.");
    return {};
  }
  QString label = result.value(QStringLiteral("label")).toString().trimmed();
  if (label.isEmpty())
    label = QStringLiteral("agent result");
  result.insert(QStringLiteral("label"), label.left(80));
  result.insert(QStringLiteral("sql"), sql);
  return result;
}

void AgentSearchBridge::loadThumbnails() {
  const quint32 salt = QRandomGenerator::global()->generate();
  const QPointer<AgentSearchBridge> self(this);
  (void)QtConcurrent::run([self, salt] {
    const QStringList found = cachedThumbnailSample(salt);
    if (!self)
      return;
    QMetaObject::invokeMethod(
        self,
        [self, found] {
          if (!self || self->m_thumbnails == found)
            return;
          self->m_thumbnails = found;
          emit self->thumbnailsChanged();
        },
        Qt::QueuedConnection);
  });
}

void AgentSearchBridge::clearResultFile() {
  if (m_resultPath.isEmpty())
    return;
  QFile::remove(m_resultPath);
  m_resultPath.clear();
}

bool AgentSearchBridge::start(const QString &requestText,
                              const QString &cwdText) {
  if (running())
    return false;
  const QString cleanRequest = requestText.trimmed();
  if (cleanRequest.isEmpty()) {
    m_error = QStringLiteral("Describe the files you want to find.");
    emit changed();
    return false;
  }
  if (cleanRequest.size() > 4000) {
    m_error = QStringLiteral("Request is too long (4,000 characters max).");
    emit changed();
    return false;
  }

  m_request = cleanRequest;
  m_cwd = QDir::cleanPath(QFileInfo(cwdText.isEmpty() ? QDir::homePath()
                                                       : cwdText)
                               .absoluteFilePath());
  m_agent = qEnvironmentVariable("SYNCHRO_AGENT_SEARCH_AGENT");
  if (m_agent.isEmpty())
    m_agent = configuredAgent(QDir::homePath());
  m_error.clear();
  m_output.clear();
  m_thumbnails.clear();
  emit thumbnailsChanged();
  loadThumbnails();

  const QByteArray fake = qgetenv("SYNCHRO_AGENT_SEARCH_FAKE_RESPONSE");
  if (!fake.isEmpty()) {
    m_fakePending = true;
    emit changed();
    QTimer::singleShot(0, this, [this, fake] {
      if (!m_fakePending)
        return;
      m_fakePending = false;
      m_output = fake;
      finish(true);
    });
    return true;
  }

  if (m_agent.isEmpty()) {
    m_error = QStringLiteral(
        "No Omarchy default agent is configured. Run `omarchy default agent`. ");
    emit changed();
    return false;
  }
  QString program = qEnvironmentVariable("SYNCHRO_AGENT_SEARCH_PROGRAM");
  if (program.isEmpty())
    program = QStandardPaths::findExecutable(m_agent);
  if (program.isEmpty()) {
    m_error = QStringLiteral("Omarchy's %1 agent is not installed.").arg(m_agent);
    emit changed();
    return false;
  }

  const QString binary = QCoreApplication::applicationFilePath();
  const QString prompt = promptFor(cleanRequest, m_cwd, binary);
  QStringList args;
  if (m_agent == QLatin1String("codex")) {
    QTemporaryFile resultFile(
        QDir::temp().filePath(QStringLiteral("synchro-agent-result-XXXXXX.txt")));
    resultFile.setAutoRemove(false);
    if (resultFile.open()) {
      m_resultPath = resultFile.fileName();
      resultFile.close();
    }
    args = {QStringLiteral("exec"), QStringLiteral("--skip-git-repo-check"),
            QStringLiteral("--sandbox"), QStringLiteral("read-only"),
            QStringLiteral("-c"),
            QStringLiteral("approval_policy=\"never\""),
            QStringLiteral("-c"),
            QStringLiteral("model_reasoning_effort=\"low\""),
            QStringLiteral("--ephemeral"), QStringLiteral("--color"),
            QStringLiteral("never"), QStringLiteral("-C"), m_cwd,
            QStringLiteral("--add-dir"),
            catalogDirectory()};
    if (!m_resultPath.isEmpty())
      args << QStringLiteral("--output-last-message") << m_resultPath;
    args << prompt;
  } else if (m_agent == QLatin1String("claude")) {
    args = {QStringLiteral("--print"), QStringLiteral("--permission-mode"),
            QStringLiteral("auto"), QStringLiteral("--output-format"),
            QStringLiteral("text"), QStringLiteral("--no-session-persistence"),
            prompt};
  } else if (m_agent == QLatin1String("gemini")) {
    args = {QStringLiteral("--yolo"), QStringLiteral("--prompt"), prompt};
  } else if (m_agent == QLatin1String("opencode")) {
    args = {QStringLiteral("run"), prompt};
  } else if (m_agent == QLatin1String("crush")) {
    args = {QStringLiteral("run"), prompt};
  } else if (m_agent == QLatin1String("copilot")) {
    args = {QStringLiteral("--allow-all"), QStringLiteral("--prompt"), prompt};
  } else if (m_agent == QLatin1String("grok")) {
    args = {QStringLiteral("--permission-mode"),
            QStringLiteral("bypassPermissions"), QStringLiteral("--print"),
            prompt};
  } else if (m_agent == QLatin1String("omp")) {
    args = {QStringLiteral("--auto-approve"), QStringLiteral("--print"),
            prompt};
  } else if (m_agent == QLatin1String("pi")) {
    args = {QStringLiteral("--print"), prompt};
  } else {
    m_error = QStringLiteral("Unsupported Omarchy agent: %1").arg(m_agent);
    emit changed();
    return false;
  }

  QProcess *process = new QProcess(this);
  m_process = process;
  process->setProgram(program);
  process->setArguments(args);
  process->setWorkingDirectory(m_cwd);
  process->setProcessChannelMode(QProcess::MergedChannels);
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("SYNCHRO_BIN"), binary);
  environment.insert(QStringLiteral("SYNCHRO_CWD"), m_cwd);
  process->setProcessEnvironment(environment);
  connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
    if (m_process != process)
      return;
    m_output.append(process->readAllStandardOutput());
    if (m_output.size() > 1024 * 1024)
      m_output = m_output.right(1024 * 1024);
  });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (m_process == process && error == QProcess::FailedToStart) {
              m_error = process->errorString();
              finish(false);
            }
          });
  connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this, process](int code, QProcess::ExitStatus status) {
            if (m_process != process)
              return;
            m_output.append(process->readAllStandardOutput());
            finish(status == QProcess::NormalExit && code == 0);
          });
  emit changed();
  process->start();
  // `codex exec` appends piped stdin to an argument prompt. QProcess owns a
  // writable pipe by default, so leaving it open makes Codex wait forever for
  // EOF before it even starts the request.
  process->closeWriteChannel();
  QTimer::singleShot(120000, process, [this, process] {
    if (m_process != process)
      return;
    m_error = QStringLiteral("Agent search timed out after two minutes.");
    process->kill();
  });
  return true;
}

void AgentSearchBridge::finish(bool processOk) {
  QProcess *process = m_process;
  m_process = nullptr;
  if (process)
    process->deleteLater();

  QByteArray response;
  if (!m_resultPath.isEmpty()) {
    QFile resultFile(m_resultPath);
    if (resultFile.open(QIODevice::ReadOnly))
      response = resultFile.readAll();
  }
  if (response.isEmpty())
    response = m_output;
  clearResultFile();

  QString parseError;
  const QVariantMap result = parseResponse(response, &parseError);
  if (!result.isEmpty()) {
    m_error.clear();
    emit changed();
    emit resultReady(
        result.value(QStringLiteral("label")).toString(),
        result.value(QStringLiteral("sql")).toString(),
        result.value(QStringLiteral("cwd"), m_cwd).toString());
    return;
  }
  if (m_error.isEmpty()) {
    const QString detail = compactProcessOutput(m_output);
    m_error = processOk ? parseError
                        : QStringLiteral("%1%2%3")
                              .arg(QStringLiteral("Agent search failed"),
                                   detail.isEmpty() ? QString()
                                                    : QStringLiteral(": "),
                                   detail);
  }
  emit changed();
}

void AgentSearchBridge::cancel() {
  m_fakePending = false;
  if (m_process) {
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->kill();
    process->deleteLater();
  }
  clearResultFile();
  m_error.clear();
  emit changed();
}
