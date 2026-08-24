#include "AgentBridge.h"

#include "Config.h"
#include "FileCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QSet>

#include <cstdio>

namespace {

QString sqlString(QString value) {
  value.replace(QLatin1Char('\''), QLatin1String("''"));
  return QLatin1Char('\'') + value + QLatin1Char('\'');
}

int resultLimit(const QJsonObject &args) {
  return qBound(1, args.value(QStringLiteral("limit")).toInt(100), 500);
}

QString scopePath(const QJsonObject &args) {
  const QString requested = args.value(QStringLiteral("cwd")).toString();
  if (requested.isEmpty())
    return QDir::currentPath();
  return QDir::cleanPath(QFileInfo(requested).absoluteFilePath());
}

bool pathWithin(const QString &root, const QString &path) {
  return root == QLatin1String("/") || path == root ||
         path.startsWith(root + QLatin1Char('/'));
}

QJsonObject schema(const QJsonObject &properties,
                   const QJsonArray &required = {}) {
  QJsonObject out;
  out.insert(QStringLiteral("type"), QStringLiteral("object"));
  out.insert(QStringLiteral("properties"), properties);
  if (!required.isEmpty())
    out.insert(QStringLiteral("required"), required);
  out.insert(QStringLiteral("additionalProperties"), false);
  return out;
}

QJsonObject stringProperty(const QString &description) {
  return {{QStringLiteral("type"), QStringLiteral("string")},
          {QStringLiteral("description"), description}};
}

QJsonObject boolProperty(const QString &description) {
  return {{QStringLiteral("type"), QStringLiteral("boolean")},
          {QStringLiteral("description"), description}};
}

QJsonObject limitProperty() {
  return {{QStringLiteral("type"), QStringLiteral("integer")},
          {QStringLiteral("minimum"), 1},
          {QStringLiteral("maximum"), 500},
          {QStringLiteral("default"), 100},
          {QStringLiteral("description"),
           QStringLiteral("Maximum rows to return.")}};
}

QJsonObject annotations(bool readOnly = true) {
  return {{QStringLiteral("readOnlyHint"), readOnly},
          {QStringLiteral("destructiveHint"), false},
          {QStringLiteral("idempotentHint"), readOnly},
          {QStringLiteral("openWorldHint"), false}};
}

QJsonObject tool(const QString &name, const QString &description,
                 const QJsonObject &inputSchema, bool readOnly = true) {
  return {{QStringLiteral("name"), name},
          {QStringLiteral("description"), description},
          {QStringLiteral("inputSchema"), inputSchema},
          {QStringLiteral("annotations"), annotations(readOnly)}};
}

QJsonArray tools() {
  QJsonArray out;
  out.append(tool(
      QStringLiteral("search_files"),
      QStringLiteral("Search indexed file names and paths below a folder, "
                     "with optional deterministic metadata filters."),
      schema(
          QJsonObject{
              {QStringLiteral("query"),
               stringProperty(
                   QStringLiteral("Text contained in name or path."))},
              {QStringLiteral("cwd"),
               stringProperty(QStringLiteral(
                   "Folder scope; defaults to the server working directory."))},
              {QStringLiteral("kind"), stringProperty(QStringLiteral(
                                           "Optional kind such as image, code, "
                                           "data, archive, or folder."))},
              {QStringLiteral("extension"),
               stringProperty(QStringLiteral(
                   "Optional extension without a leading dot."))},
              {QStringLiteral("hidden"),
               boolProperty(QStringLiteral(
                   "Restrict to hidden or non-hidden entries."))},
              {QStringLiteral("min_size"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("minimum"), 0}}},
              {QStringLiteral("max_size"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                           {QStringLiteral("minimum"), 0}}},
              {QStringLiteral("limit"), limitProperty()}},
          QJsonArray{QStringLiteral("query")})));
  out.append(tool(
      QStringLiteral("query_files"),
      QStringLiteral(
          "Run read-only DuckDB SQL over Synchro relations: files, "
          "here, tree, selection, facts, image_facts, and projects."),
      schema(
          QJsonObject{
              {QStringLiteral("sql"),
               stringProperty(QStringLiteral("A SELECT or WITH query."))},
              {QStringLiteral("cwd"), stringProperty(QStringLiteral(
                                          "Folder bound to here and tree."))},
              {QStringLiteral("selection"),
               QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                           {QStringLiteral("items"),
                            QJsonObject{{QStringLiteral("type"),
                                         QStringLiteral("string")}}},
                           {QStringLiteral("maxItems"), 100}}},
              {QStringLiteral("limit"), limitProperty()}},
          QJsonArray{QStringLiteral("sql")})));
  out.append(tool(
      QStringLiteral("find_projects"),
      QStringLiteral("Find indexed project folders classified from repository "
                     "and build markers."),
      schema(QJsonObject{
          {QStringLiteral("cwd"),
           stringProperty(QStringLiteral("Folder tree to search."))},
          {QStringLiteral("project_type"),
           stringProperty(
               QStringLiteral("Optional type: python, node, rust, go, native, "
                              "application, or build."))},
          {QStringLiteral("limit"), limitProperty()}})));
  out.append(tool(
      QStringLiteral("get_file_facts"),
      QStringLiteral(
          "Get catalog rows plus current deterministic analyzer facts "
          "for explicit local paths."),
      schema(QJsonObject{{QStringLiteral("paths"),
                          QJsonObject{
                              {QStringLiteral("type"), QStringLiteral("array")},
                              {QStringLiteral("items"),
                               QJsonObject{{QStringLiteral("type"),
                                            QStringLiteral("string")}}},
                              {QStringLiteral("minItems"), 1},
                              {QStringLiteral("maxItems"), 100}}}},
             QJsonArray{QStringLiteral("paths")})));
  out.append(tool(QStringLiteral("list_saved_queries"),
                  QStringLiteral("List SQL query bookmarks saved in Synchro."),
                  schema(QJsonObject{})));
  out.append(
      tool(QStringLiteral("run_saved_query"),
           QStringLiteral(
               "Run a Synchro SQL bookmark by id or case-insensitive name."),
           schema(QJsonObject{{QStringLiteral("query"),
                               stringProperty(
                                   QStringLiteral("Saved query id or name."))},
                              {QStringLiteral("limit"), limitProperty()}},
                  QJsonArray{QStringLiteral("query")})));
  out.append(tool(
      QStringLiteral("show_in_synchro"),
      QStringLiteral("Open a validated read-only SQL result as a navigable "
                     "pseudo-folder in a Synchro window."),
      schema(
          QJsonObject{
              {QStringLiteral("sql"),
               stringProperty(QStringLiteral("A SELECT or WITH query."))},
              {QStringLiteral("cwd"), stringProperty(QStringLiteral(
                                          "Folder bound to here and tree."))},
              {QStringLiteral("label"),
               stringProperty(
                   QStringLiteral("Short label for the result folder."))}},
          QJsonArray{QStringLiteral("sql")}),
      false));
  return out;
}

QJsonObject toolResult(const QVariantMap &result) {
  const QJsonObject structured = QJsonObject::fromVariantMap(result);
  const QByteArray text =
      QJsonDocument(structured).toJson(QJsonDocument::Compact);
  QJsonArray content;
  content.append(
      QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                  {QStringLiteral("text"), QString::fromUtf8(text)}});
  QJsonObject out{{QStringLiteral("content"), content},
                  {QStringLiteral("structuredContent"), structured}};
  if (!result.value(QStringLiteral("ok"), true).toBool())
    out.insert(QStringLiteral("isError"), true);
  return out;
}

QVariantMap errorResult(const QString &message) {
  return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}

QStringList jsonPaths(const QJsonValue &value) {
  QStringList out;
  if (!value.isArray())
    return out;
  for (const QJsonValue &entry : value.toArray()) {
    if (!entry.isString() || entry.toString().isEmpty())
      continue;
    out.append(QDir::cleanPath(QFileInfo(entry.toString()).absoluteFilePath()));
    if (out.size() >= 100)
      break;
  }
  out.removeDuplicates();
  return out;
}

QString commonScope(const QStringList &paths) {
  if (paths.isEmpty())
    return QDir::currentPath();
  QString common = QFileInfo(paths.constFirst()).isDir()
                       ? paths.constFirst()
                       : QFileInfo(paths.constFirst()).absolutePath();
  for (const QString &path : paths) {
    const QString candidate =
        QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();
    while (!common.isEmpty() && !pathWithin(common, candidate)) {
      const QString parent = QFileInfo(common).absolutePath();
      if (parent == common)
        break;
      common = parent;
    }
  }
  return common.isEmpty() ? QStringLiteral("/") : common;
}

QVariantMap searchFiles(const QJsonObject &args) {
  const QString query =
      args.value(QStringLiteral("query")).toString().trimmed();
  if (query.isEmpty())
    return errorResult(QStringLiteral("query must not be empty"));
  QStringList where{QStringLiteral("(contains(lower(name),lower(%1)) OR "
                                   "contains(lower(path),lower(%1)))")
                        .arg(sqlString(query))};
  const QString kind = args.value(QStringLiteral("kind")).toString().trimmed();
  if (!kind.isEmpty())
    where.append(QStringLiteral("lower(kind)=lower(%1)").arg(sqlString(kind)));
  QString extension =
      args.value(QStringLiteral("extension")).toString().trimmed();
  if (extension.startsWith(QLatin1Char('.')))
    extension.remove(0, 1);
  if (!extension.isEmpty())
    where.append(
        QStringLiteral("lower(extension)=lower(%1)").arg(sqlString(extension)));
  if (args.contains(QStringLiteral("hidden")))
    where.append(args.value(QStringLiteral("hidden")).toBool()
                     ? QStringLiteral("hidden=true")
                     : QStringLiteral("hidden=false"));
  if (args.contains(QStringLiteral("min_size")))
    where.append(
        QStringLiteral("size>=%1")
            .arg(QString::number(qMax<qint64>(
                0, args.value(QStringLiteral("min_size")).toInteger()))));
  if (args.contains(QStringLiteral("max_size")))
    where.append(
        QStringLiteral("size<=%1")
            .arg(QString::number(qMax<qint64>(
                0, args.value(QStringLiteral("max_size")).toInteger()))));
  const QString sql =
      QStringLiteral("select name,path,parent,extension,kind,size,kb,mb,gb,"
                     "mtime,modified_date,is_dir,hidden from tree where %1 "
                     "order by is_dir desc,name")
          .arg(where.join(QStringLiteral(" and ")));
  return FileCatalog::querySync(sql, scopePath(args), {}, resultLimit(args));
}

QVariantMap queryFiles(const QJsonObject &args) {
  const QString sql = args.value(QStringLiteral("sql")).toString();
  if (sql.trimmed().isEmpty())
    return errorResult(QStringLiteral("sql must not be empty"));
  return FileCatalog::querySync(
      sql, scopePath(args), jsonPaths(args.value(QStringLiteral("selection"))),
      resultLimit(args));
}

QVariantMap findProjects(const QJsonObject &args) {
  QString sql = QStringLiteral(
      "select name,project_type,markers,mtime,path,is_dir from projects");
  const QString type =
      args.value(QStringLiteral("project_type")).toString().trimmed();
  if (!type.isEmpty())
    sql += QStringLiteral(" where lower(project_type)=lower(%1)")
               .arg(sqlString(type));
  sql += QStringLiteral(" order by mtime desc,name");
  return FileCatalog::querySync(sql, scopePath(args), {}, resultLimit(args));
}

QVariantMap getFileFacts(const QJsonObject &args) {
  const QStringList paths = jsonPaths(args.value(QStringLiteral("paths")));
  if (paths.isEmpty())
    return errorResult(QStringLiteral("paths must contain local paths"));
  const QString cwd = commonScope(paths);
  const QVariantMap files = FileCatalog::querySync(
      QStringLiteral("select * from selection order by path"), cwd, paths,
      paths.size());
  if (!files.value(QStringLiteral("ok")).toBool())
    return files;
  QStringList literals;
  for (const QString &path : paths)
    literals.append(sqlString(path));
  const QString in = literals.join(QLatin1Char(','));
  const QVariantMap facts = FileCatalog::querySync(
      QStringLiteral("select path,name,analyzer,analyzer_version,key,"
                     "text_value,numeric_value,updated_at,file_id,size,mtime "
                     "from facts where path in (%1) order by path,analyzer,key")
          .arg(in),
      cwd, paths, 500);
  const QVariantMap imageFacts = FileCatalog::querySync(
      QStringLiteral(
          "select * from image_facts where path in (%1) order by path")
          .arg(in),
      cwd, paths, paths.size());
  QVariantMap out;
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("scope"), files.value(QStringLiteral("scope")));
  out.insert(QStringLiteral("catalog"), files.value(QStringLiteral("catalog")));
  out.insert(QStringLiteral("files"), files.value(QStringLiteral("rows")));
  out.insert(QStringLiteral("facts"), facts.value(QStringLiteral("rows")));
  out.insert(QStringLiteral("imageFacts"),
             imageFacts.value(QStringLiteral("rows")));
  out.insert(QStringLiteral("factErrors"),
             QVariantList{facts.value(QStringLiteral("error")),
                          imageFacts.value(QStringLiteral("error"))});
  return out;
}

QVariantMap listSavedQueries() {
  Config config;
  QVariantMap out;
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("queries"), config.sqlBookmarks());
  out.insert(QStringLiteral("count"), config.sqlBookmarks().size());
  return out;
}

QVariantMap runSavedQuery(const QJsonObject &args) {
  const QString wanted =
      args.value(QStringLiteral("query")).toString().trimmed();
  Config config;
  for (const QVariant &value : config.sqlBookmarks()) {
    const QVariantMap bookmark = value.toMap();
    const bool idMatch =
        bookmark.value(QStringLiteral("id")).toString() == wanted;
    const bool nameMatch = bookmark.value(QStringLiteral("name"))
                               .toString()
                               .compare(wanted, Qt::CaseInsensitive) == 0;
    if (!idMatch && !nameMatch)
      continue;
    QVariantMap result =
        FileCatalog::querySync(bookmark.value(QStringLiteral("sql")).toString(),
                               bookmark.value(QStringLiteral("cwd")).toString(),
                               {}, resultLimit(args));
    result.insert(QStringLiteral("savedQuery"), bookmark);
    return result;
  }
  return errorResult(QStringLiteral("saved query not found: %1").arg(wanted));
}

QVariantMap showInSynchro(const QJsonObject &args) {
  const QString sql = args.value(QStringLiteral("sql")).toString();
  const QString cwd = scopePath(args);
  const QVariantMap validation = FileCatalog::querySync(sql, cwd, {}, 1);
  if (!validation.value(QStringLiteral("ok")).toBool())
    return validation;
  QString label = args.value(QStringLiteral("label")).toString().trimmed();
  if (label.isEmpty())
    label = QStringLiteral("agent result");
  const QString program = QCoreApplication::applicationFilePath();
  const bool launched = QProcess::startDetached(
      program, {QStringLiteral("--sql-query"), sql, QStringLiteral("--sql-cwd"),
                cwd, QStringLiteral("--sql-label"), label});
  if (!launched)
    return errorResult(QStringLiteral("could not launch Synchro"));
  QVariantMap out;
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("launched"), true);
  out.insert(QStringLiteral("cwd"), cwd);
  out.insert(QStringLiteral("label"), label);
  return out;
}

QVariantMap callTool(const QString &name, const QJsonObject &args) {
  if (name == QLatin1String("search_files"))
    return searchFiles(args);
  if (name == QLatin1String("query_files"))
    return queryFiles(args);
  if (name == QLatin1String("find_projects"))
    return findProjects(args);
  if (name == QLatin1String("get_file_facts"))
    return getFileFacts(args);
  if (name == QLatin1String("list_saved_queries"))
    return listSavedQueries();
  if (name == QLatin1String("run_saved_query"))
    return runSavedQuery(args);
  if (name == QLatin1String("show_in_synchro"))
    return showInSynchro(args);
  return errorResult(QStringLiteral("unknown tool: %1").arg(name));
}

QJsonObject rpcError(const QJsonValue &id, int code, const QString &message) {
  return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id},
          {QStringLiteral("error"),
           QJsonObject{{QStringLiteral("code"), code},
                       {QStringLiteral("message"), message}}}};
}

QJsonObject rpcResult(const QJsonValue &id, const QJsonValue &result) {
  return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id},
          {QStringLiteral("result"), result}};
}

} // namespace

QJsonObject AgentBridge::handleRequest(const QJsonObject &request) {
  const QString method = request.value(QStringLiteral("method")).toString();
  const QJsonValue id = request.value(QStringLiteral("id"));
  if (method.startsWith(QLatin1String("notifications/")) ||
      method == QLatin1String("exit"))
    return {};
  if (method == QLatin1String("initialize")) {
    const QJsonObject params =
        request.value(QStringLiteral("params")).toObject();
    QString protocol =
        params.value(QStringLiteral("protocolVersion")).toString();
    if (protocol.isEmpty())
      protocol = QStringLiteral("2025-06-18");
    return rpcResult(
        id,
        QJsonObject{
            {QStringLiteral("protocolVersion"), protocol},
            {QStringLiteral("capabilities"),
             QJsonObject{
                 {QStringLiteral("tools"),
                  QJsonObject{{QStringLiteral("listChanged"), false}}}}},
            {QStringLiteral("serverInfo"),
             QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("synchro")},
                 {QStringLiteral("version"), QStringLiteral(SYNCHRO_VERSION)}}},
            {QStringLiteral("instructions"),
             QStringLiteral(
                 "Use Synchro's persisted, read-only catalog. "
                 "Check catalog.coverageComplete and "
                 "truncated before treating results as exhaustive.")}});
  }
  if (method == QLatin1String("ping") || method == QLatin1String("shutdown"))
    return rpcResult(id, QJsonObject{});
  if (method == QLatin1String("tools/list"))
    return rpcResult(id, QJsonObject{{QStringLiteral("tools"), tools()}});
  if (method == QLatin1String("tools/call")) {
    const QJsonObject params =
        request.value(QStringLiteral("params")).toObject();
    const QString name = params.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
      return rpcError(id, -32602, QStringLiteral("tool name is required"));
    const QVariantMap called =
        callTool(name, params.value(QStringLiteral("arguments")).toObject());
    return rpcResult(id, toolResult(called));
  }
  if (method == QLatin1String("resources/list"))
    return rpcResult(id,
                     QJsonObject{{QStringLiteral("resources"), QJsonArray{}}});
  if (method == QLatin1String("prompts/list"))
    return rpcResult(id,
                     QJsonObject{{QStringLiteral("prompts"), QJsonArray{}}});
  return rpcError(id, -32601,
                  QStringLiteral("method not found: %1").arg(method));
}

int AgentBridge::runStdio() {
  QFile input;
  if (!input.open(stdin, QIODevice::ReadOnly))
    return 1;
  QFile output;
  if (!output.open(stdout, QIODevice::WriteOnly))
    return 1;
  while (true) {
    const QByteArray line = input.readLine();
    if (line.isEmpty())
      break;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    QJsonObject response;
    if (!doc.isObject()) {
      response = rpcError(
          QJsonValue::Null, -32700,
          QStringLiteral("parse error: %1").arg(parseError.errorString()));
    } else {
      response = handleRequest(doc.object());
      if (doc.object().value(QStringLiteral("method")) == QLatin1String("exit"))
        break;
    }
    if (response.isEmpty())
      continue;
    QByteArray encoded = QJsonDocument(response).toJson(QJsonDocument::Compact);
    encoded.append('\n');
    output.write(encoded);
    output.flush();
  }
  return 0;
}
