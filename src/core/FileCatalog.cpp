#include "FileCatalog.h"

#include "DirectoryModel.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QThread>

#include <limits>
#include <sqlite3.h>

#include <utility>

namespace {

constexpr int kDefaultMaxRows = 200;

QString sqlString(QString value) {
  value.replace(QLatin1Char('\''), QLatin1String("''"));
  return QLatin1Char('\'') + value + QLatin1Char('\'');
}

QString normalizedPath(const QString &raw) {
  if (raw.isEmpty() || DirectoryModel::isVirtualPath(raw))
    return {};
  return QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
}

bool execSql(sqlite3 *db, const char *sql) {
  if (!db || !sql)
    return false;
  char *error = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (error)
    sqlite3_free(error);
  return rc == SQLITE_OK;
}

sqlite3 *openCatalog(QString *error = nullptr) {
  const QString path = FileCatalog::dbPath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  sqlite3 *db = nullptr;
  const int rc = sqlite3_open_v2(
      QFile::encodeName(path).constData(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (rc != SQLITE_OK || !db) {
    if (error)
      *error = db ? QString::fromUtf8(sqlite3_errmsg(db))
                  : QStringLiteral("unable to open catalog");
    if (db)
      sqlite3_close(db);
    return nullptr;
  }
  sqlite3_busy_timeout(db, 5000);
  execSql(db, "PRAGMA journal_mode=WAL;");
  execSql(db, "PRAGMA synchronous=NORMAL;");
  const bool ok = execSql(
      db,
      "CREATE TABLE IF NOT EXISTS files ("
      " path TEXT PRIMARY KEY,"
      " parent TEXT NOT NULL,"
      " name TEXT NOT NULL,"
      " extension TEXT NOT NULL DEFAULT '',"
      " is_dir INTEGER NOT NULL DEFAULT 0,"
      " size INTEGER NOT NULL DEFAULT 0,"
      " mtime INTEGER NOT NULL DEFAULT 0,"
      " mime TEXT NOT NULL DEFAULT '',"
      " is_hidden INTEGER NOT NULL DEFAULT 0,"
      " is_symlink INTEGER NOT NULL DEFAULT 0,"
      " seen_at INTEGER NOT NULL DEFAULT 0,"
      " scan_root TEXT NOT NULL DEFAULT ''"
      ");"
      "CREATE INDEX IF NOT EXISTS files_parent ON files(parent);"
      "CREATE INDEX IF NOT EXISTS files_name ON files(name COLLATE NOCASE);"
      "CREATE INDEX IF NOT EXISTS files_extension ON files(extension);"
      "CREATE INDEX IF NOT EXISTS files_size ON files(size);"
      "CREATE INDEX IF NOT EXISTS files_mtime ON files(mtime);"
      "CREATE TABLE IF NOT EXISTS scan_state ("
      " root TEXT PRIMARY KEY,"
      " row_count INTEGER NOT NULL DEFAULT 0,"
      " complete INTEGER NOT NULL DEFAULT 0,"
      " completed_at INTEGER NOT NULL DEFAULT 0"
      ");");
  if (!ok) {
    if (error)
      *error = QString::fromUtf8(sqlite3_errmsg(db));
    sqlite3_close(db);
    return nullptr;
  }
  QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
  return db;
}

struct CatalogRow {
  QString path;
  QString parent;
  QString name;
  QString extension;
  QString mime;
  qint64 size = 0;
  qint64 mtime = 0;
  bool isDir = false;
  bool isHidden = false;
  bool isSymlink = false;
};

CatalogRow rowForInfo(const QFileInfo &fi) {
  CatalogRow row;
  if (!fi.exists() && !fi.isSymLink())
    return row;
  row.path = QDir::cleanPath(fi.absoluteFilePath());
  row.parent = QDir::cleanPath(fi.absolutePath());
  row.name = fi.fileName();
  row.extension = fi.suffix().toLower();
  row.isDir = fi.isDir();
  row.isHidden = fi.isHidden() || row.name.startsWith(QLatin1Char('.'));
  row.isSymlink = fi.isSymLink();
  row.size = row.isDir ? 0 : fi.size();
  row.mtime = fi.lastModified().toMSecsSinceEpoch();
  return row;
}

CatalogRow rowForPath(const QString &raw) {
  return rowForInfo(QFileInfo(raw));
}

void bindText(sqlite3_stmt *st, int index, const QString &value) {
  const QByteArray utf8 = value.toUtf8();
  sqlite3_bind_text(st, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

bool upsertRows(sqlite3 *db, const QVector<CatalogRow> &rows,
                const QString &scanRoot = QString(), qint64 seenAt = 0) {
  if (!db || rows.isEmpty())
    return true;
  static const char *sql =
      "INSERT INTO files(path,parent,name,extension,is_dir,size,mtime,mime,"
      "is_hidden,is_symlink,seen_at,scan_root) VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(path) DO UPDATE SET parent=excluded.parent,"
      "name=excluded.name,extension=excluded.extension,is_dir=excluded.is_dir,"
      "size=excluded.size,mtime=excluded.mtime,"
      "mime=CASE WHEN excluded.mime='' THEN files.mime ELSE excluded.mime END,"
      "is_hidden=excluded.is_hidden,is_symlink=excluded.is_symlink,"
      "seen_at=excluded.seen_at,"
      "scan_root=CASE WHEN excluded.scan_root='' THEN files.scan_root "
      "ELSE excluded.scan_root END;";
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    return false;
  const qint64 now = seenAt > 0 ? seenAt : QDateTime::currentMSecsSinceEpoch();
  bool ok = true;
  execSql(db, "BEGIN IMMEDIATE;");
  for (const CatalogRow &row : rows) {
    if (row.path.isEmpty())
      continue;
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    bindText(st, 1, row.path);
    bindText(st, 2, row.parent);
    bindText(st, 3, row.name);
    bindText(st, 4, row.extension);
    sqlite3_bind_int(st, 5, row.isDir ? 1 : 0);
    sqlite3_bind_int64(st, 6, row.size);
    sqlite3_bind_int64(st, 7, row.mtime);
    bindText(st, 8, row.mime);
    sqlite3_bind_int(st, 9, row.isHidden ? 1 : 0);
    sqlite3_bind_int(st, 10, row.isSymlink ? 1 : 0);
    sqlite3_bind_int64(st, 11, now);
    bindText(st, 12, scanRoot);
    if (sqlite3_step(st) != SQLITE_DONE) {
      ok = false;
      break;
    }
  }
  sqlite3_finalize(st);
  execSql(db, ok ? "COMMIT;" : "ROLLBACK;");
  return ok;
}

bool upsertChangedRows(sqlite3 *db, const QVector<CatalogRow> &rows,
                       const QString &scanRoot) {
  if (!db || rows.isEmpty())
    return true;
  static const char *sql =
      "INSERT INTO files(path,parent,name,extension,is_dir,size,mtime,mime,"
      "is_hidden,is_symlink,seen_at,scan_root) VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(path) DO UPDATE SET parent=excluded.parent,"
      "name=excluded.name,extension=excluded.extension,is_dir=excluded.is_dir,"
      "size=excluded.size,mtime=excluded.mtime,"
      "mime=CASE WHEN excluded.mime='' THEN files.mime ELSE excluded.mime END,"
      "is_hidden=excluded.is_hidden,is_symlink=excluded.is_symlink,"
      "scan_root=CASE WHEN files.scan_root='' THEN excluded.scan_root "
      "ELSE files.scan_root END "
      "WHERE files.parent<>excluded.parent OR files.name<>excluded.name OR "
      "files.extension<>excluded.extension OR files.is_dir<>excluded.is_dir OR "
      "files.size<>excluded.size OR files.mtime<>excluded.mtime OR "
      "files.is_hidden<>excluded.is_hidden OR "
      "files.is_symlink<>excluded.is_symlink OR "
      "(files.mime='' AND excluded.mime<>'') OR "
      "(files.scan_root='' AND excluded.scan_root<>'');";
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    return false;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  bool ok = true;
  execSql(db, "BEGIN IMMEDIATE;");
  for (const CatalogRow &row : rows) {
    if (row.path.isEmpty())
      continue;
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    bindText(st, 1, row.path);
    bindText(st, 2, row.parent);
    bindText(st, 3, row.name);
    bindText(st, 4, row.extension);
    sqlite3_bind_int(st, 5, row.isDir ? 1 : 0);
    sqlite3_bind_int64(st, 6, row.size);
    sqlite3_bind_int64(st, 7, row.mtime);
    bindText(st, 8, row.mime);
    sqlite3_bind_int(st, 9, row.isHidden ? 1 : 0);
    sqlite3_bind_int(st, 10, row.isSymlink ? 1 : 0);
    sqlite3_bind_int64(st, 11, now);
    bindText(st, 12, scanRoot);
    if (sqlite3_step(st) != SQLITE_DONE) {
      ok = false;
      break;
    }
  }
  sqlite3_finalize(st);
  execSql(db, ok ? "COMMIT;" : "ROLLBACK;");
  return ok;
}

int deleteCatalogPath(sqlite3 *db, const QString &path, bool isDir) {
  if (!db || path.isEmpty())
    return 0;
  const char *sql =
      isDir
          ? "WITH RECURSIVE doomed(path) AS ("
            " SELECT path FROM files WHERE path=?"
            " UNION ALL"
            " SELECT files.path FROM files JOIN doomed"
            " ON files.parent=doomed.path"
            ") DELETE FROM files WHERE path IN (SELECT path FROM doomed);"
          : "DELETE FROM files WHERE path=?;";
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    return 0;
  bindText(st, 1, path);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok ? sqlite3_changes(db) : 0;
}

int catalogCountBelow(sqlite3 *db, const QString &root) {
  if (!db || root.isEmpty())
    return 0;
  const QString lower = root == QLatin1String("/") ? root
                                                    : root + QLatin1Char('/');
  const QString upper = root == QLatin1String("/") ? QStringLiteral("0")
                                                    : root + QLatin1Char('0');
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "SELECT count(*) FROM files WHERE path=? OR (path>=? AND path<?);",
          -1, &st, nullptr) != SQLITE_OK)
    return 0;
  bindText(st, 1, root);
  bindText(st, 2, lower);
  bindText(st, 3, upper);
  const int count = sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0)
                                                    : 0;
  sqlite3_finalize(st);
  return count;
}

bool persistScanState(sqlite3 *db, const QString &root, int rowCount,
                      bool complete) {
  if (!db || root.isEmpty())
    return false;
  static const char *sql =
      "INSERT INTO scan_state(root,row_count,complete,completed_at) "
      "VALUES(?,?,?,?) ON CONFLICT(root) DO UPDATE SET "
      "row_count=CASE WHEN excluded.complete=1 THEN excluded.row_count "
      "ELSE scan_state.row_count END,"
      "complete=CASE WHEN scan_state.complete=1 THEN 1 "
      "ELSE excluded.complete END,"
      "completed_at=CASE WHEN excluded.complete=1 THEN excluded.completed_at "
      "ELSE scan_state.completed_at END;";
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
    return false;
  bindText(st, 1, root);
  sqlite3_bind_int(st, 2, qMax(0, rowCount));
  sqlite3_bind_int(st, 3, complete ? 1 : 0);
  sqlite3_bind_int64(st, 4,
                     complete ? QDateTime::currentMSecsSinceEpoch() : 0);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  return ok;
}

QVariant jsonValue(const QJsonValue &value) {
  if (value.isNull() || value.isUndefined())
    return {};
  if (value.isBool())
    return value.toBool();
  if (value.isDouble())
    return value.toDouble();
  if (value.isString())
    return value.toString();
  if (value.isArray())
    return value.toArray().toVariantList();
  if (value.isObject())
    return value.toObject().toVariantMap();
  return value.toVariant();
}

QStringList jsonObjectKeyOrder(const QByteArray &json) {
  QStringList keys;
  qsizetype pos = json.indexOf('{');
  if (pos < 0)
    return keys;
  ++pos;
  auto skipSpace = [&] {
    while (pos < json.size() &&
           (json.at(pos) == ' ' || json.at(pos) == '\t' ||
            json.at(pos) == '\r' || json.at(pos) == '\n' ||
            json.at(pos) == ','))
      ++pos;
  };
  while (pos < json.size()) {
    skipSpace();
    if (pos >= json.size() || json.at(pos) == '}')
      break;
    if (json.at(pos) != '"')
      return {};
    const qsizetype keyStart = pos++;
    bool escaped = false;
    while (pos < json.size()) {
      const char c = json.at(pos++);
      if (!escaped && c == '"')
        break;
      if (!escaped && c == '\\')
        escaped = true;
      else
        escaped = false;
    }
    const QByteArray literal = json.mid(keyStart, pos - keyStart);
    const QJsonDocument decoded =
        QJsonDocument::fromJson(QByteArrayLiteral("[") + literal + ']');
    if (!decoded.isArray() || decoded.array().isEmpty())
      return {};
    keys.append(decoded.array().first().toString());
    while (pos < json.size() &&
           (json.at(pos) == ' ' || json.at(pos) == '\t' ||
            json.at(pos) == '\r' || json.at(pos) == '\n'))
      ++pos;
    if (pos >= json.size() || json.at(pos++) != ':')
      return {};

    int nested = 0;
    bool inString = false;
    escaped = false;
    for (; pos < json.size(); ++pos) {
      const char c = json.at(pos);
      if (inString) {
        if (!escaped && c == '"')
          inString = false;
        if (!escaped && c == '\\')
          escaped = true;
        else
          escaped = false;
        continue;
      }
      if (c == '"') {
        inString = true;
      } else if (c == '{' || c == '[') {
        ++nested;
      } else if (c == '}' || c == ']') {
        if (nested == 0)
          return keys;
        --nested;
      } else if (c == ',' && nested == 0) {
        ++pos;
        break;
      }
    }
  }
  return keys;
}

QString scrubSql(const QString &sql) {
  QString out = sql;
  enum class State { Plain, Single, Double, LineComment, BlockComment };
  State state = State::Plain;
  for (int i = 0; i < out.size(); ++i) {
    const QChar c = out.at(i);
    const QChar n = i + 1 < out.size() ? out.at(i + 1) : QChar();
    if (state == State::Plain) {
      if (c == QLatin1Char('\'')) {
        state = State::Single;
        out[i] = QLatin1Char(' ');
      } else if (c == QLatin1Char('"')) {
        state = State::Double;
        out[i] = QLatin1Char(' ');
      } else if (c == QLatin1Char('-') && n == QLatin1Char('-')) {
        state = State::LineComment;
        out[i] = QLatin1Char(' ');
        ++i;
        out[i] = QLatin1Char(' ');
      } else if (c == QLatin1Char('/') && n == QLatin1Char('*')) {
        state = State::BlockComment;
        out[i] = QLatin1Char(' ');
        ++i;
        out[i] = QLatin1Char(' ');
      }
    } else if (state == State::Single) {
      out[i] = QLatin1Char(' ');
      if (c == QLatin1Char('\'') && n == QLatin1Char('\''))
        out[++i] = QLatin1Char(' ');
      else if (c == QLatin1Char('\''))
        state = State::Plain;
    } else if (state == State::Double) {
      out[i] = QLatin1Char(' ');
      if (c == QLatin1Char('"') && n == QLatin1Char('"'))
        out[++i] = QLatin1Char(' ');
      else if (c == QLatin1Char('"'))
        state = State::Plain;
    } else if (state == State::LineComment) {
      out[i] = QLatin1Char(' ');
      if (c == QLatin1Char('\n'))
        state = State::Plain;
    } else {
      out[i] = QLatin1Char(' ');
      if (c == QLatin1Char('*') && n == QLatin1Char('/')) {
        out[++i] = QLatin1Char(' ');
        state = State::Plain;
      }
    }
  }
  return out;
}

QString querySourceRelation(const QString &sql) {
  static const QRegularExpression re(
      QStringLiteral("\\bfrom\\s+(here|tree|selection)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = re.match(scrubSql(sql));
  return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QStringList queryGroupKeys(const QString &sql) {
  static const QSet<QString> catalogColumns = {
      QStringLiteral("path"),       QStringLiteral("parent"),
      QStringLiteral("name"),       QStringLiteral("extension"),
      QStringLiteral("is_dir"),     QStringLiteral("size"),
      QStringLiteral("kb"),         QStringLiteral("mb"),
      QStringLiteral("gb"),         QStringLiteral("mtime"),
      QStringLiteral("mime"),       QStringLiteral("hidden"),
      QStringLiteral("is_hidden"),  QStringLiteral("is_symlink")};
  static const QRegularExpression groupRe(
      QStringLiteral("\\bgroup\\s+by\\s+(.+?)(?=\\border\\s+by\\b|"
                     "\\bhaving\\b|\\blimit\\b|$)"),
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression identifier(
      QStringLiteral("^(?:[A-Za-z_][A-Za-z0-9_]*\\.)?"
                     "([A-Za-z_][A-Za-z0-9_]*)$"));
  const auto match = groupRe.match(scrubSql(sql));
  if (!match.hasMatch())
    return {};
  QStringList keys;
  for (QString part : match.captured(1).split(QLatin1Char(','))) {
    part = part.trimmed();
    const auto id = identifier.match(part);
    if (!id.hasMatch())
      return {};
    const QString key = id.captured(1).toLower();
    if (!catalogColumns.contains(key))
      return {};
    keys.append(key);
  }
  return keys;
}

QString duckLiteral(const QJsonValue &value) {
  if (value.isNull() || value.isUndefined())
    return QStringLiteral("NULL");
  if (value.isBool())
    return value.toBool() ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
  if (value.isDouble())
    return QString::number(value.toDouble(), 'g', 17);
  return sqlString(value.toVariant().toString());
}

QByteArray groupSignature(const QJsonObject &row,
                          const QStringList &groupKeys) {
  QJsonArray values;
  for (const QString &key : groupKeys) {
    if (!row.contains(key))
      return {};
    values.append(row.value(key));
  }
  return QJsonDocument(values).toJson(QJsonDocument::Compact);
}

QString sqliteGroupExpression(const QString &key) {
  if (key == QLatin1String("hidden"))
    return QStringLiteral("is_hidden");
  if (key == QLatin1String("kb"))
    return QStringLiteral("round(CAST(size AS REAL)/1024.0,2)");
  if (key == QLatin1String("mb"))
    return QStringLiteral("round(CAST(size AS REAL)/1048576.0,2)");
  if (key == QLatin1String("gb"))
    return QStringLiteral("round(CAST(size AS REAL)/1073741824.0,2)");
  static const QSet<QString> direct = {
      QStringLiteral("path"),       QStringLiteral("parent"),
      QStringLiteral("name"),       QStringLiteral("extension"),
      QStringLiteral("is_dir"),     QStringLiteral("size"),
      QStringLiteral("mtime"),      QStringLiteral("mime"),
      QStringLiteral("is_hidden"),  QStringLiteral("is_symlink")};
  return direct.contains(key) ? key : QString();
}

QString likePrefix(QString path) {
  path.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
  path.replace(QLatin1Char('%'), QLatin1String("\\%"));
  path.replace(QLatin1Char('_'), QLatin1String("\\_"));
  return path == QLatin1String("/") ? QStringLiteral("/%")
                                     : path + QStringLiteral("/%");
}

void bindVariant(sqlite3_stmt *st, int index, const QVariant &value) {
  if (!value.isValid() || value.isNull()) {
    sqlite3_bind_null(st, index);
  } else if (value.metaType().id() == QMetaType::Bool) {
    sqlite3_bind_int(st, index, value.toBool() ? 1 : 0);
  } else if (value.canConvert<double>() &&
             value.metaType().id() != QMetaType::QString) {
    sqlite3_bind_double(st, index, value.toDouble());
  } else {
    bindText(st, index, value.toString());
  }
}

QVariantList sqliteGroupPreviews(sqlite3 *db, const QString &relation,
                                 const QString &cwd,
                                 const QStringList &selection,
                                 const QStringList &groupKeys,
                                 const QJsonObject &row) {
  if (!db || !groupKeys.contains(QStringLiteral("extension")))
    return {};

  QStringList where;
  QVariantList bindings;
  if (relation == QLatin1String("tree")) {
    where.append(QStringLiteral("(path=? OR path LIKE ? ESCAPE '\\')"));
    bindings.append(cwd);
    bindings.append(likePrefix(cwd));
  } else if (relation == QLatin1String("here")) {
    where.append(QStringLiteral("parent=?"));
    bindings.append(cwd);
  } else if (relation == QLatin1String("selection")) {
    if (selection.isEmpty())
      return {};
    where.append(QStringLiteral("path IN (%1)")
                     .arg(QStringList(selection.size(), QStringLiteral("?"))
                              .join(QLatin1Char(','))));
    for (const QString &path : selection)
      bindings.append(normalizedPath(path));
  }

  for (const QString &key : groupKeys) {
    const QString expression = sqliteGroupExpression(key);
    if (expression.isEmpty() || !row.contains(key))
      return {};
    const QJsonValue value = row.value(key);
    if (value.isNull() || value.isUndefined()) {
      where.append(expression + QStringLiteral(" IS NULL"));
    } else {
      where.append(expression + QLatin1String("=?"));
      bindings.append(jsonValue(value));
    }
  }

  const QString sql =
      QStringLiteral("SELECT path FROM files WHERE %1 ORDER BY rowid LIMIT 32")
          .arg(where.join(QStringLiteral(" AND ")));
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &st, nullptr) !=
      SQLITE_OK)
    return {};
  for (int i = 0; i < bindings.size(); ++i)
    bindVariant(st, i + 1, bindings.at(i));
  QVariantList paths;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const auto *text = sqlite3_column_text(st, 0);
    if (text)
      paths.append(QString::fromUtf8(reinterpret_cast<const char *>(text)));
  }
  sqlite3_finalize(st);
  return paths;
}

QString drillSql(const QString &relation, const QStringList &groupKeys,
                 const QJsonObject &row) {
  if (relation.isEmpty())
    return {};
  QStringList where;
  for (const QString &key : groupKeys) {
    if (!row.contains(key))
      return {};
    QString quoted = key;
    quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
    const QJsonValue value = row.value(key);
    if (value.isNull() || value.isUndefined())
      where.append(QStringLiteral("%1 IS NULL").arg(quoted));
    else
      where.append(QStringLiteral("%1 = %2")
                       .arg(quoted, duckLiteral(value)));
  }
  QString out =
      QStringLiteral("select name, extension, size, kb, mb, gb, mtime, path, "
                     "is_dir, hidden\n"
                     "from %1")
          .arg(relation);
  if (!where.isEmpty())
    out += QStringLiteral("\nwhere ") + where.join(QStringLiteral(" and "));
  out += QStringLiteral("\norder by is_dir desc, name");
  return out;
}

bool validateReadOnlyQuery(QString *sql, QString *error) {
  if (!sql)
    return false;
  *sql = sql->trimmed();
  while (sql->endsWith(QLatin1Char(';')))
    sql->chop(1);
  *sql = sql->trimmed();
  if (sql->isEmpty()) {
    if (error)
      *error = QStringLiteral("write a SELECT query");
    return false;
  }
  if (sql->size() > 32768) {
    if (error)
      *error = QStringLiteral("query is too large");
    return false;
  }
  const QString scrubbed = scrubSql(*sql).trimmed();
  static const QRegularExpression start(
      QStringLiteral("^(select|with)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  if (!start.match(scrubbed).hasMatch()) {
    if (error)
      *error = QStringLiteral("SQL workbench is read-only: SELECT or WITH only");
    return false;
  }
  static const QRegularExpression forbidden(
      QStringLiteral("\\b(insert|update|delete|merge|drop|create|alter|copy|"
                     "attach|detach|install|load|call|pragma|set|vacuum|"
                     "checkpoint|export|import)\\b|;"),
      QRegularExpression::CaseInsensitiveOption);
  if (forbidden.match(scrubbed).hasMatch()) {
    if (error)
      *error = QStringLiteral("catalog SQL is read-only");
    return false;
  }
  return true;
}

QVariantMap runDuckQuery(QString sql, const QString &cwd,
                         const QStringList &selection, int maxRows) {
  QVariantMap out;
  QString validationError;
  if (!validateReadOnlyQuery(&sql, &validationError)) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), validationError);
    return out;
  }
  const QString duck = QStandardPaths::findExecutable(QStringLiteral("duckdb"));
  if (duck.isEmpty()) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"),
               QStringLiteral("duckdb is not installed"));
    return out;
  }
  QString dbError;
  if (sqlite3 *db = openCatalog(&dbError))
    sqlite3_close(db);
  else {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), dbError);
    return out;
  }

  const QString cleanCwd = normalizedPath(cwd);
  QString preamble =
      QStringLiteral("LOAD sqlite; ATTACH %1 AS catalog (TYPE sqlite, "
                     "READ_ONLY); SET autoinstall_known_extensions=false; "
                     "SET autoload_known_extensions=false; "
                     "CREATE TEMP TABLE _synchro_context(cwd VARCHAR); "
                     "INSERT INTO _synchro_context VALUES (%2); "
                     "CREATE TEMP TABLE _synchro_selection(path VARCHAR);")
          .arg(sqlString(FileCatalog::dbPath()), sqlString(cleanCwd));
  for (const QString &path : selection)
    preamble += QStringLiteral("INSERT INTO _synchro_selection VALUES (%1);")
                    .arg(sqlString(normalizedPath(path)));
  preamble += QStringLiteral(
      "CREATE TEMP VIEW files AS SELECT path,parent,name,extension,"
      "CAST(is_dir AS BOOLEAN) AS is_dir,size,"
      "round(CAST(size AS DOUBLE)/1024.0,2) AS kb,"
      "round(CAST(size AS DOUBLE)/1048576.0,2) AS mb,"
      "round(CAST(size AS DOUBLE)/1073741824.0,2) AS gb,"
      "mtime,mime,"
      "CAST(is_hidden AS BOOLEAN) AS hidden,"
      "CAST(is_hidden AS BOOLEAN) AS is_hidden,"
      "CAST(is_symlink AS BOOLEAN) AS is_symlink FROM catalog.files;"
      "CREATE TEMP VIEW here AS SELECT f.* FROM files f, _synchro_context c "
      "WHERE f.parent=c.cwd;"
      "CREATE TEMP VIEW tree AS SELECT f.* FROM files f, _synchro_context c "
      "WHERE f.path=c.cwd OR starts_with(f.path,rtrim(c.cwd,'/') || '/');"
      "CREATE TEMP VIEW selection AS SELECT f.* FROM files f JOIN "
      "_synchro_selection s USING(path);");
  const int limit = qBound(1, maxRows <= 0 ? kDefaultMaxRows : maxRows, 500);
  const QString command =
      preamble + QStringLiteral("SELECT * FROM (%1) AS _synchro_result LIMIT %2")
                     .arg(sql, QString::number(limit + 1));

  QElapsedTimer timer;
  timer.start();
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  proc.start(duck, {QStringLiteral("-json"), QStringLiteral(":memory:"),
                    QStringLiteral("-c"), command});
  if (!proc.waitForFinished(15000)) {
    proc.kill();
    proc.waitForFinished(1000);
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QStringLiteral("query timed out"));
    return out;
  }
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    out.insert(QStringLiteral("ok"), false);
    QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    if (err.isEmpty())
      err = QStringLiteral("duckdb query failed");
    out.insert(QStringLiteral("error"), err);
    return out;
  }
  const QByteArray stdoutBytes = proc.readAllStandardOutput().trimmed();
  const QStringList emittedColumns = jsonObjectKeyOrder(stdoutBytes);
  QJsonParseError parseError;
  const QJsonDocument doc = stdoutBytes.isEmpty()
                                ? QJsonDocument(QJsonArray())
                                : QJsonDocument::fromJson(stdoutBytes,
                                                          &parseError);
  if (!doc.isArray()) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"),
               QStringLiteral("duckdb returned unreadable JSON"));
    return out;
  }

  QJsonArray array = doc.array();
  const bool truncated = array.size() > limit;
  if (truncated)
    array.removeLast();
  QVariantList columns;
  QVariantList rows;
  const QString relation = querySourceRelation(sql);
  const QStringList groupKeys = queryGroupKeys(sql);
  QHash<QByteArray, QVariantList> groupPreviews;
  bool groupShapeOk = !relation.isEmpty() && !groupKeys.isEmpty();
  for (const QJsonValue &value : std::as_const(array)) {
    if (!value.isObject() || groupSignature(value.toObject(), groupKeys).isEmpty()) {
      groupShapeOk = false;
      break;
    }
  }
  if (groupShapeOk) {
    sqlite3 *previewDb = openCatalog();
    if (previewDb) {
      for (const QJsonValue &value : std::as_const(array)) {
        const QJsonObject row = value.toObject();
        const QByteArray signature = groupSignature(row, groupKeys);
        if (signature.isEmpty())
          continue;
        const QVariantList previews = sqliteGroupPreviews(
            previewDb, relation, cleanCwd, selection, groupKeys, row);
        if (!previews.isEmpty())
          groupPreviews.insert(signature, previews);
      }
      sqlite3_close(previewDb);
    }
  }
  if (!array.isEmpty() && array.first().isObject()) {
    const QJsonObject first = array.first().toObject();
    const QStringList names = emittedColumns.isEmpty() ? first.keys()
                                                        : emittedColumns;
    for (const QString &name : names) {
      QVariantMap col;
      col.insert(QStringLiteral("name"), name);
      columns.append(col);
    }
  }
  rows.reserve(array.size());
  for (int rowIndex = 0; rowIndex < array.size(); ++rowIndex) {
    const QJsonValue value = array.at(rowIndex);
    const QJsonObject object = value.toObject();
    QVariantMap row;
    for (auto it = object.begin(); it != object.end(); ++it)
      row.insert(it.key(), jsonValue(it.value()));
    if (!object.contains(QStringLiteral("path"))) {
      const QVariantList previews =
          groupPreviews.value(groupSignature(object, groupKeys));
      if (!previews.isEmpty())
        row.insert(QStringLiteral("_synchro_preview_paths"), previews);
      const QString nextSql = drillSql(relation, groupKeys, object);
      if (!nextSql.isEmpty())
        row.insert(QStringLiteral("_synchro_drill_sql"), nextSql);
      QStringList labels;
      for (const QString &key : groupKeys) {
        const QString valueText = object.value(key).toVariant().toString();
        labels.append(valueText.isEmpty() ? QStringLiteral("(none)")
                                          : valueText);
      }
      if (labels.isEmpty())
        labels.append(relation.isEmpty() ? QStringLiteral("result %1").arg(rowIndex + 1)
                                         : relation);
      row.insert(QStringLiteral("_synchro_label"),
                 labels.join(QStringLiteral(" · ")));
    }
    rows.append(row);
  }
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("engine"), QStringLiteral("duckdb"));
  out.insert(QStringLiteral("columns"), columns);
  out.insert(QStringLiteral("rows"), rows);
  out.insert(QStringLiteral("truncated"), truncated);
  out.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  out.insert(QStringLiteral("cwd"), cleanCwd);
  out.insert(QStringLiteral("sourceRelation"), relation);
  out.insert(QStringLiteral("groupKeys"), groupKeys);
  return out;
}

} // namespace

FileCatalog::FileCatalog(DirectoryModel *model, QObject *parent)
    : QObject(parent), m_model(model) {
  m_scanPool.setMaxThreadCount(1);
  m_scanPool.setExpiryTimeout(-1);
  m_scanPool.setThreadPriority(QThread::LowPriority);
  m_writerPool.setMaxThreadCount(1);
  m_writerPool.setExpiryTimeout(-1);
  m_queryPool.setMaxThreadCount(1);
  m_queryPool.setExpiryTimeout(-1);
  m_snapshotTimer.setSingleShot(true);
  m_snapshotTimer.setInterval(90);
  connect(&m_snapshotTimer, &QTimer::timeout, this,
          &FileCatalog::captureSnapshot);
  if (m_model) {
    connect(m_model, &DirectoryModel::pathChanged, this,
            &FileCatalog::scheduleSnapshot);
    connect(m_model, &DirectoryModel::countChanged, this,
            &FileCatalog::scheduleSnapshot);
    connect(m_model, &DirectoryModel::statsApplied, this,
            &FileCatalog::enqueueUpsert);
    connect(m_model, &DirectoryModel::catalogPathsRemoved, this,
            &FileCatalog::enqueueDelete);
    connect(m_model, &DirectoryModel::listingChanged, this, [this] {
      if (m_model && !m_model->listing())
        scheduleSnapshot();
    });
  }
  restoreScanState();
  scheduleSnapshot();
}

FileCatalog::~FileCatalog() {
  m_snapshotTimer.stop();
  if (m_scanCancel)
    m_scanCancel->store(true);
  m_scanPool.waitForDone();
  m_writerPool.waitForDone();
  m_queryPool.waitForDone();
}

QString FileCatalog::dbPath() {
  const QByteArray home = qgetenv("SYNCHRO_HOME");
  if (!home.isEmpty())
    return QString::fromLocal8Bit(home) + QStringLiteral("/catalog.sqlite");
  return QDir::homePath() +
         QStringLiteral("/.local/share/synchro/catalog.sqlite");
}

QStringList FileCatalog::fields() const {
  // Row-major order for the SQL editor's compact three-column field index.
  return {QStringLiteral("path"),       QStringLiteral("size"),
          QStringLiteral("mime"),       QStringLiteral("parent"),
          QStringLiteral("kb"),         QStringLiteral("hidden"),
          QStringLiteral("name"),       QStringLiteral("mb"),
          QStringLiteral("is_hidden"),  QStringLiteral("extension"),
          QStringLiteral("gb"),         QStringLiteral("is_symlink"),
          QStringLiteral("is_dir"),     QStringLiteral("mtime")};
}

QVariantMap FileCatalog::status() const {
  QVariantMap out;
  out.insert(QStringLiteral("indexing"), m_indexing);
  out.insert(QStringLiteral("root"), m_indexedRoot);
  out.insert(QStringLiteral("count"), m_indexedCount);
  out.insert(QStringLiteral("text"), m_statusText);
  out.insert(QStringLiteral("path"), dbPath());
  return out;
}

void FileCatalog::scheduleSnapshot() { m_snapshotTimer.start(); }

void FileCatalog::refreshCurrent() {
  m_snapshotTimer.stop();
  captureSnapshot();
}

void FileCatalog::captureSnapshot() {
  if (!m_model || DirectoryModel::isVirtualPath(m_model->path()))
    return;
  QStringList paths;
  paths.reserve(m_model->rowCount());
  for (int row = 0; row < m_model->rowCount(); ++row) {
    const QModelIndex index = m_model->index(row, 0);
    const QString path =
        m_model->data(index, DirectoryModel::PathRole).toString();
    if (!path.isEmpty())
      paths.append(path);
  }
  enqueueUpsert(paths);
}

void FileCatalog::enqueueUpsert(const QStringList &rawPaths) {
  QStringList paths = rawPaths;
  paths.removeDuplicates();
  if (paths.isEmpty())
    return;
  m_writerPool.start([paths] {
    QString error;
    sqlite3 *db = openCatalog(&error);
    if (!db)
      return;
    QVector<CatalogRow> rows;
    rows.reserve(paths.size());
    for (const QString &path : paths) {
      CatalogRow row = rowForPath(path);
      if (!row.path.isEmpty())
        rows.append(row);
    }
    upsertRows(db, rows);
    sqlite3_close(db);
  });
}

void FileCatalog::enqueueDelete(const QStringList &rawPaths) {
  QStringList paths = rawPaths;
  paths.removeDuplicates();
  if (paths.isEmpty())
    return;
  m_writerPool.start([paths] {
    sqlite3 *db = openCatalog();
    if (!db)
      return;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(db, "DELETE FROM files WHERE path=?;", -1, &st,
                           nullptr) != SQLITE_OK) {
      sqlite3_close(db);
      return;
    }
    execSql(db, "BEGIN IMMEDIATE;");
    for (const QString &path : paths) {
      sqlite3_reset(st);
      sqlite3_clear_bindings(st);
      bindText(st, 1, normalizedPath(path));
      sqlite3_step(st);
    }
    sqlite3_finalize(st);
    execSql(db, "COMMIT;");
    sqlite3_close(db);
  });
}

quint64 FileCatalog::query(const QString &sql, const QString &cwd,
                           const QStringList &selection, int maxRows) {
  // Capture the browser relation before dispatching. The query worker waits
  // only for this small live-update queue; recursive scans commit through the
  // separate scan pool and never hold a query until the full tree is done.
  refreshCurrent();
  const quint64 request = ++m_nextRequest;
  QPointer<FileCatalog> self(this);
  QThreadPool *writerPool = &m_writerPool;
  m_queryPool.start([self, writerPool, request, sql, cwd, selection, maxRows] {
                      writerPool->waitForDone();
                      const QVariantMap result =
                          runDuckQuery(sql, cwd, selection, maxRows);
                      if (!self)
                        return;
                      QMetaObject::invokeMethod(
                          self,
                          [self, request, result] {
                            if (self)
                              emit self->queryFinished(request, result);
                          },
                          Qt::QueuedConnection);
                    });
  return request;
}

QString FileCatalog::sourceRelation(const QString &sql) const {
  return querySourceRelation(sql);
}

bool FileCatalog::coversTree(const QString &rawRoot) const {
  const QString path = normalizedPath(rawRoot);
  if (path.isEmpty())
    return false;
  for (const QString &root : m_completeRoots) {
    if (path == root || root == QLatin1String("/") ||
        path.startsWith(root + QLatin1Char('/')))
      return true;
  }
  return false;
}

void FileCatalog::restoreScanState() {
  sqlite3 *db = openCatalog();
  if (!db)
    return;

  auto readComplete = [db] {
    QVector<QPair<QString, int>> states;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(
            db,
            "SELECT root,row_count FROM scan_state WHERE complete=1 "
            "ORDER BY completed_at DESC;",
            -1, &st, nullptr) == SQLITE_OK) {
      while (sqlite3_step(st) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(st, 0);
        if (text)
          states.append({QString::fromUtf8(
                             reinterpret_cast<const char *>(text)),
                         sqlite3_column_int(st, 1)});
      }
    }
    if (st)
      sqlite3_finalize(st);
    return states;
  };

  QVector<QPair<QString, int>> states = readComplete();
  if (states.isEmpty()) {
    bool hasStateRow = false;
    sqlite3_stmt *stateProbe = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM scan_state LIMIT 1;", -1,
                           &stateProbe, nullptr) == SQLITE_OK)
      hasStateRow = sqlite3_step(stateProbe) == SQLITE_ROW;
    if (stateProbe)
      sqlite3_finalize(stateProbe);

    // Catalogs written before scan_state existed already contain durable
    // scan_root values. Adopt them once instead of forcing a multi-million-row
    // rebuild merely to reconstruct a few bytes of status metadata.
    if (!hasStateRow) {
      QString legacyRoot;
      sqlite3_stmt *rootProbe = nullptr;
      if (sqlite3_prepare_v2(
              db,
              "SELECT scan_root FROM files WHERE scan_root<>'' LIMIT 1;", -1,
              &rootProbe, nullptr) == SQLITE_OK &&
          sqlite3_step(rootProbe) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(rootProbe, 0);
        if (text)
          legacyRoot = QString::fromUtf8(
              reinterpret_cast<const char *>(text));
      }
      if (rootProbe)
        sqlite3_finalize(rootProbe);
      if (!legacyRoot.isEmpty()) {
        int legacyCount = 0;
        sqlite3_stmt *countProbe = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT count(*) FROM files;", -1,
                               &countProbe, nullptr) == SQLITE_OK &&
            sqlite3_step(countProbe) == SQLITE_ROW)
          legacyCount = sqlite3_column_int(countProbe, 0);
        if (countProbe)
          sqlite3_finalize(countProbe);
        persistScanState(db, legacyRoot, legacyCount, true);
        states = readComplete();
      }
    }
  }
  sqlite3_close(db);

  if (states.isEmpty())
    return;
  for (const auto &state : std::as_const(states))
    rememberCompleteRoot(state.first);
  m_indexedRoot = states.first().first;
  m_indexedCount = states.first().second;
  m_statusText = QStringLiteral("Tree index ready · %L1 persisted rows")
                     .arg(m_indexedCount);
}

void FileCatalog::rememberCompleteRoot(const QString &rawRoot) {
  const QString root = normalizedPath(rawRoot);
  if (root.isEmpty() || m_completeRoots.contains(root))
    return;
  m_completeRoots.append(root);
}

void FileCatalog::scanTree(const QString &rawRoot, int maxEntries) {
  const QString root = normalizedPath(rawRoot);
  if (root.isEmpty() || !QFileInfo(root).isDir()) {
    setStatus(false, root, 0, QStringLiteral("choose a local folder"));
    return;
  }
  if (m_indexing && m_indexedRoot == root)
    return;
  if (m_scanCancel)
    m_scanCancel->store(true);
  const auto cancelled = std::make_shared<std::atomic_bool>(false);
  m_scanCancel = cancelled;
  const quint64 generation = ++m_scanGeneration;
  const int cap = maxEntries > 0 ? maxEntries : std::numeric_limits<int>::max();
  const bool bounded = maxEntries > 0;
  const bool incremental = !bounded && m_completeRoots.contains(root);
  const qint64 scanToken = QDateTime::currentMSecsSinceEpoch();
  setStatus(true, root, incremental ? m_indexedCount : 0,
            incremental ? QStringLiteral("Refreshing tree · finding changes…")
                        : QStringLiteral("Indexing tree · starting…"));
  QPointer<FileCatalog> self(this);
  m_scanPool.start([self, root, cap, bounded, incremental, scanToken,
                    generation, cancelled] {
    sqlite3 *db = openCatalog();
    if (!db) {
      if (self)
        QMetaObject::invokeMethod(
            self,
            [self, root, generation] {
              if (self && self->m_scanGeneration == generation)
                self->setStatus(false, root, 0,
                                QStringLiteral("catalog is unavailable"));
            },
            Qt::QueuedConnection);
      return;
    }

    if (incremental) {
      QElapsedTimer refreshTimer;
      refreshTimer.start();
      struct DirectoryStamp {
        QString path;
        qint64 mtime = 0;
        bool force = false;
      };

      const QString lower = root == QLatin1String("/")
                                ? root
                                : root + QLatin1Char('/');
      const QString upper = root == QLatin1String("/")
                                ? QStringLiteral("0")
                                : root + QLatin1Char('0');
      int checked = 0;
      int changedDirs = 0;
      int removedRows = 0;
      int totalDirs = 0;
      QStringList removedPrefixes;
      QVector<DirectoryStamp> discovered;
      discovered.reserve(256);
      QSet<QString> forcedPaths;

      sqlite3_stmt *countQuery = nullptr;
      if (sqlite3_prepare_v2(
              db,
              "SELECT count(*) FROM files WHERE is_dir=1 AND is_symlink=0 "
              "AND path>=? AND path<?;",
              -1, &countQuery, nullptr) == SQLITE_OK) {
        bindText(countQuery, 1, lower);
        bindText(countQuery, 2, upper);
        if (sqlite3_step(countQuery) == SQLITE_ROW)
          totalDirs = sqlite3_column_int(countQuery, 0);
      }
      if (countQuery)
        sqlite3_finalize(countQuery);
      ++totalDirs; // The scan root itself is not stored as a files row.

      const auto reconcileDirectory =
          [&](const DirectoryStamp &stamp) {
        bool underRemovedTree = false;
        for (const QString &prefix : std::as_const(removedPrefixes)) {
          if (stamp.path == prefix ||
              stamp.path.startsWith(prefix + QLatin1Char('/'))) {
            underRemovedTree = true;
            break;
          }
        }
        if (underRemovedTree)
          return;

        const QFileInfo dirInfo(stamp.path);
        if (!dirInfo.exists() || !dirInfo.isDir()) {
          removedRows += deleteCatalogPath(db, stamp.path, true);
          removedPrefixes.append(stamp.path);
          ++changedDirs;
          return;
        }

        ++checked;
        const qint64 currentMtime =
            dirInfo.lastModified().toMSecsSinceEpoch();
        if (!stamp.force && currentMtime == stamp.mtime) {
          if ((checked & 8191) == 0 && self) {
            QMetaObject::invokeMethod(
                self,
                [self, root, checked, totalDirs, changedDirs, generation] {
                  if (self && self->m_scanGeneration == generation)
                    self->setStatus(
                        true, root, self->m_indexedCount,
                        QStringLiteral(
                            "Refreshing tree · %L1 / %L2 folders · %L3 changed")
                            .arg(checked)
                            .arg(totalDirs)
                            .arg(changedDirs));
                },
                Qt::QueuedConnection);
          }
          return;
        }
        if (!dirInfo.isReadable())
          return;

        QHash<QString, bool> existing;
        sqlite3_stmt *childQuery = nullptr;
        if (sqlite3_prepare_v2(
                db, "SELECT path,is_dir FROM files WHERE parent=?;", -1,
                &childQuery, nullptr) == SQLITE_OK) {
          bindText(childQuery, 1, stamp.path);
          while (sqlite3_step(childQuery) == SQLITE_ROW) {
            const auto *text = sqlite3_column_text(childQuery, 0);
            if (text)
              existing.insert(
                  QString::fromUtf8(reinterpret_cast<const char *>(text)),
                  sqlite3_column_int(childQuery, 1) != 0);
          }
        }
        if (childQuery)
          sqlite3_finalize(childQuery);

        const QFileInfoList entries = QDir(stamp.path).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System |
                QDir::NoDotAndDotDot,
            QDir::NoSort);
        QVector<CatalogRow> rows;
        rows.reserve(entries.size() + 1);
        QSet<QString> currentPaths;
        currentPaths.reserve(entries.size());
        for (const QFileInfo &entry : entries) {
          CatalogRow row = rowForInfo(entry);
          if (row.path.isEmpty())
            continue;
          const bool isNew = !existing.contains(row.path);
          currentPaths.insert(row.path);
          // Keep an existing directory's old mtime until that directory gets
          // its own reconciliation turn. This lets the catalog stream paths
          // in small batches instead of retaining a million stamps in RAM.
          if (!row.isDir || isNew || row.isSymlink)
            rows.append(row);
          if (isNew && row.isDir && !row.isSymlink) {
            discovered.append({row.path, row.mtime, true});
            forcedPaths.insert(row.path);
          }
        }
        if (stamp.path != root)
          rows.append(rowForInfo(dirInfo));

        for (auto it = existing.cbegin(); it != existing.cend(); ++it) {
          if (currentPaths.contains(it.key()))
            continue;
          removedRows += deleteCatalogPath(db, it.key(), it.value());
          if (it.value())
            removedPrefixes.append(it.key());
        }
        upsertChangedRows(db, rows, root);
        ++changedDirs;
      };

      reconcileDirectory({root, 0, true});
      QString after = root;
      while (!cancelled->load()) {
        QVector<DirectoryStamp> batch;
        batch.reserve(4096);
        sqlite3_stmt *dirQuery = nullptr;
        if (sqlite3_prepare_v2(
                db,
                "SELECT path,mtime FROM files WHERE is_dir=1 AND is_symlink=0 "
                "AND path>? AND path<? ORDER BY path LIMIT 4096;",
                -1, &dirQuery, nullptr) != SQLITE_OK)
          break;
        bindText(dirQuery, 1, after);
        bindText(dirQuery, 2, upper);
        while (sqlite3_step(dirQuery) == SQLITE_ROW) {
          const auto *text = sqlite3_column_text(dirQuery, 0);
          if (!text)
            continue;
          const QString path = QString::fromUtf8(
              reinterpret_cast<const char *>(text));
          batch.append({path, sqlite3_column_int64(dirQuery, 1),
                        forcedPaths.remove(path)});
        }
        sqlite3_finalize(dirQuery);
        if (batch.isEmpty())
          break;
        after = batch.constLast().path;
        for (const DirectoryStamp &stamp : std::as_const(batch)) {
          if (cancelled->load())
            break;
          reconcileDirectory(stamp);
        }
      }

      // New subtrees whose names sorted before the keyset cursor were not in
      // the original catalog stream. Walk only those additions now.
      for (qsizetype cursor = 0;
           cursor < discovered.size() && !cancelled->load(); ++cursor) {
        const DirectoryStamp stamp = discovered.at(cursor);
        if (!forcedPaths.remove(stamp.path))
          continue;
        reconcileDirectory(stamp);
      }

      const bool stopped = cancelled->load();
      int count = 0;
      if (!stopped) {
        count = catalogCountBelow(db, root);
        persistScanState(db, root, count, true);
      }
      const qint64 elapsedMs = refreshTimer.elapsed();
      sqlite3_close(db);
      if (!self || stopped)
        return;
      QMetaObject::invokeMethod(
          self,
          [self, root, count, changedDirs, removedRows, elapsedMs, generation] {
            if (self && self->m_scanGeneration == generation) {
              self->rememberCompleteRoot(root);
              const QString elapsed =
                  elapsedMs < 1000
                      ? QStringLiteral("%L1 ms").arg(elapsedMs)
                      : QStringLiteral("%L1 s").arg(elapsedMs / 1000.0, 0,
                                                     'f', 1);
              self->setStatus(
                  false, root, count,
                  QStringLiteral(
                      "Tree refreshed in %1 · %L2 folders changed · %L3 stale rows removed · %L4 rows")
                      .arg(elapsed)
                      .arg(changedDirs)
                      .arg(removedRows)
                      .arg(count));
            }
          },
          Qt::QueuedConnection);
      return;
    }

    // Marks new-schema scans as intentionally incomplete if the process is
    // interrupted. Existing completed state for this root is preserved until
    // a replacement scan finishes.
    persistScanState(db, root, 0, false);
    QQueue<QString> pending;
    pending.enqueue(root);
    QVector<CatalogRow> batch;
    batch.reserve(512);
    int count = 0;
    bool capped = false;
    bool stopped = false;
    while (!pending.isEmpty() && count < cap && !cancelled->load()) {
      const QString dirPath = pending.dequeue();
      const QDir dir(dirPath);
      const QFileInfoList entries = dir.entryInfoList(
          QDir::AllEntries | QDir::Hidden | QDir::System |
              QDir::NoDotAndDotDot,
          QDir::NoSort);
      for (const QFileInfo &fi : entries) {
        if (cancelled->load()) {
          stopped = true;
          break;
        }
        if (count >= cap) {
          capped = true;
          break;
        }
        if (fi.isDir() && !fi.isSymLink())
          pending.enqueue(fi.absoluteFilePath());
        CatalogRow row = rowForInfo(fi);
        if (!row.path.isEmpty()) {
          batch.append(row);
          ++count;
        }
        if (batch.size() >= 512) {
          upsertRows(db, batch, root, scanToken);
          batch.clear();
          if (self)
            QMetaObject::invokeMethod(
                self,
                [self, root, count, generation] {
                  if (self && self->m_scanGeneration == generation)
                    self->setStatus(
                        true, root, count,
                        QStringLiteral("Indexing tree · %L1 rows available")
                            .arg(count));
                },
                Qt::QueuedConnection);
          // Give readers and the small live-update writer a fair chance to
          // acquire SQLite between committed scan batches.
          QThread::yieldCurrentThread();
        }
      }
      if (stopped)
        break;
    }
    if (bounded && !pending.isEmpty() && !cancelled->load())
      capped = true;
    if (!batch.isEmpty() && !cancelled->load())
      upsertRows(db, batch, root, scanToken);
    // A complete scan is authoritative for this root. Capped scans retain the
    // previous tail because absence beyond the cap does not mean deletion.
    if (!capped && !cancelled->load()) {
      sqlite3_stmt *st = nullptr;
      if (sqlite3_prepare_v2(
              db,
              "DELETE FROM files WHERE scan_root=? AND seen_at<>?;", -1, &st,
              nullptr) == SQLITE_OK) {
        bindText(st, 1, root);
        sqlite3_bind_int64(st, 2, scanToken);
        sqlite3_step(st);
      }
      if (st)
        sqlite3_finalize(st);
      persistScanState(db, root, count, true);
    }
    sqlite3_close(db);
    if (!self || cancelled->load())
      return;
    QMetaObject::invokeMethod(
        self,
        [self, root, count, capped, generation] {
          if (self && self->m_scanGeneration == generation) {
            if (!capped)
              self->rememberCompleteRoot(root);
            self->setStatus(
                false, root, count,
                capped
                    ? QStringLiteral(
                          "Tree index incomplete · %L1 rows · limit reached")
                          .arg(count)
                    : QStringLiteral("Tree indexed · %L1 rows").arg(count));
          }
        },
        Qt::QueuedConnection);
  });
}

void FileCatalog::setStatus(bool indexing, const QString &root, int count,
                            const QString &text) {
  m_indexing = indexing;
  m_indexedRoot = root;
  m_indexedCount = count;
  m_statusText = text;
  emit statusChanged();
}
