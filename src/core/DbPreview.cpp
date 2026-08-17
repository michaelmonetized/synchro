#include "DbPreview.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantList>

#include <sqlite3.h>

namespace {

constexpr int kMaxLimit = 80;

QVariantMap fail(const QString &error) {
  QVariantMap out;
  out.insert(QStringLiteral("ok"), false);
  out.insert(QStringLiteral("error"), error);
  return out;
}

QString localPath(const QString &path) {
  return QFileInfo(path).absoluteFilePath();
}

bool quoteIdent(const QString &name, QString *out) {
  if (name.isEmpty() || !out)
    return false;
  QString q;
  q.reserve(name.size() + 2);
  q += QLatin1Char('"');
  for (const QChar c : name) {
    if (c == QLatin1Char('"'))
      q += QLatin1String("\"\"");
    else if (c.unicode() < 0x20)
      return false;
    else
      q += c;
  }
  q += QLatin1Char('"');
  *out = q;
  return true;
}

QVariant jsonToVariant(const QJsonValue &v) {
  if (v.isNull() || v.isUndefined())
    return QVariant();
  if (v.isBool())
    return v.toBool();
  if (v.isDouble())
    return v.toDouble();
  if (v.isString())
    return v.toString();
  if (v.isArray())
    return QString::fromUtf8(
        QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
  if (v.isObject())
    return QString::fromUtf8(
        QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
  return v.toVariant();
}

struct TableRef {
  QString schema;
  QString name;
  QString type;

  QString display() const {
    if (schema.isEmpty() || schema == QLatin1String("main"))
      return name;
    return schema + QLatin1Char('.') + name;
  }

  QString qualified(QString *error) const {
    QString n;
    if (!quoteIdent(name, &n)) {
      if (error)
        *error = QStringLiteral("bad table name");
      return {};
    }
    if (schema.isEmpty())
      return n;
    QString s;
    if (!quoteIdent(schema, &s)) {
      if (error)
        *error = QStringLiteral("bad schema name");
      return {};
    }
    return s + QLatin1Char('.') + n;
  }
};

TableRef parseTableRef(const QString &raw) {
  TableRef t;
  const int dot = raw.indexOf(QLatin1Char('.'));
  if (dot > 0) {
    t.schema = raw.left(dot);
    t.name = raw.mid(dot + 1);
  } else {
    t.name = raw;
  }
  return t;
}

int boundLimit(int limit) { return qBound(1, limit, kMaxLimit); }

int boundOffset(int offset) { return qMax(0, offset); }

QVariantMap inspectSqlite(const QString &path, const QString &table, int offset,
                          int limit) {
  if (!DbPreview::looksLikeSqlite(path))
    return fail(QStringLiteral("not a SQLite file"));

  sqlite3 *db = nullptr;
  const QByteArray uri =
      QByteArrayLiteral("file:") +
      QUrl::toPercentEncoding(localPath(path), "/:") +
      QByteArrayLiteral("?mode=ro");
  int rc = sqlite3_open_v2(uri.constData(), &db,
                           SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
  if (rc != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    rc = sqlite3_open_v2(QFile::encodeName(localPath(path)).constData(), &db,
                         SQLITE_OPEN_READONLY, nullptr);
  }
  if (rc != SQLITE_OK) {
    const QString err = db ? QString::fromUtf8(sqlite3_errmsg(db))
                           : QStringLiteral("unreadable");
    if (db)
      sqlite3_close(db);
    return fail(err);
  }
  sqlite3_exec(db, "PRAGMA query_only=ON;", nullptr, nullptr, nullptr);

  QVector<TableRef> tables;
  sqlite3_stmt *st = nullptr;
  rc = sqlite3_prepare_v2(
      db,
      "SELECT name, type FROM sqlite_master WHERE type IN ('table','view') "
      "AND name NOT LIKE 'sqlite_%' ORDER BY type, name;",
      -1, &st, nullptr);
  if (rc == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      TableRef t;
      t.name = QString::fromUtf8(
          reinterpret_cast<const char *>(sqlite3_column_text(st, 0)));
      t.type = QString::fromUtf8(
          reinterpret_cast<const char *>(sqlite3_column_text(st, 1)));
      t.schema = QStringLiteral("main");
      if (!t.name.isEmpty())
        tables.append(t);
    }
  }
  if (st)
    sqlite3_finalize(st);

  QVariantList tableList;
  for (const TableRef &t : tables) {
    QVariantMap row;
    row.insert(QStringLiteral("name"), t.display());
    row.insert(QStringLiteral("type"), t.type);
    row.insert(QStringLiteral("schema"), t.schema);
    tableList.append(row);
  }

  TableRef chosen;
  if (!table.isEmpty()) {
    for (const TableRef &t : tables) {
      if (t.display() == table || t.name == table) {
        chosen = t;
        break;
      }
    }
    if (chosen.name.isEmpty())
      chosen = parseTableRef(table);
  } else if (!tables.isEmpty()) {
    chosen = tables.constFirst();
  }

  QVariantMap out;
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("engine"), QStringLiteral("sqlite"));
  out.insert(QStringLiteral("path"), localPath(path));
  out.insert(QStringLiteral("name"), QFileInfo(path).fileName());
  out.insert(QStringLiteral("tables"), tableList);
  out.insert(QStringLiteral("offset"), boundOffset(offset));
  out.insert(QStringLiteral("limit"), boundLimit(limit));

  if (chosen.name.isEmpty()) {
    out.insert(QStringLiteral("table"), QString());
    out.insert(QStringLiteral("columns"), QVariantList());
    out.insert(QStringLiteral("sample"), QVariantList());
    out.insert(QStringLiteral("sampleNote"),
               tables.isEmpty() ? QStringLiteral("no tables") : QString());
    sqlite3_close(db);
    return out;
  }

  QString qerr;
  const QString ident = chosen.qualified(&qerr);
  if (ident.isEmpty()) {
    sqlite3_close(db);
    return fail(qerr);
  }

  QVariantList columns;
  const QByteArray pragma =
      QByteArrayLiteral("PRAGMA table_info(") + ident.toUtf8() + ')';
  sqlite3_stmt *info = nullptr;
  if (sqlite3_prepare_v2(db, pragma.constData(), -1, &info, nullptr) ==
      SQLITE_OK) {
    while (sqlite3_step(info) == SQLITE_ROW) {
      QVariantMap col;
      col.insert(QStringLiteral("name"),
                 QString::fromUtf8(reinterpret_cast<const char *>(
                     sqlite3_column_text(info, 1))));
      col.insert(QStringLiteral("type"),
                 QString::fromUtf8(reinterpret_cast<const char *>(
                     sqlite3_column_text(info, 2))));
      col.insert(QStringLiteral("notnull"), sqlite3_column_int(info, 3) != 0);
      col.insert(QStringLiteral("pk"), sqlite3_column_int(info, 5) != 0);
      columns.append(col);
    }
  }
  if (info)
    sqlite3_finalize(info);

  const int lim = boundLimit(limit);
  const int off = boundOffset(offset);
  const QByteArray sql =
      QByteArrayLiteral("SELECT * FROM ") + ident.toUtf8() +
      QByteArrayLiteral(" LIMIT ") + QByteArray::number(lim + 1) +
      QByteArrayLiteral(" OFFSET ") + QByteArray::number(off);
  QVariantList sample;
  bool truncated = false;
  sqlite3_stmt *rows = nullptr;
  if (sqlite3_prepare_v2(db, sql.constData(), -1, &rows, nullptr) ==
      SQLITE_OK) {
    const int n = sqlite3_column_count(rows);
    if (columns.isEmpty()) {
      for (int i = 0; i < n; ++i) {
        QVariantMap col;
        col.insert(QStringLiteral("name"),
                   QString::fromUtf8(sqlite3_column_name(rows, i)));
        col.insert(QStringLiteral("type"), QString());
        columns.append(col);
      }
    }
    int got = 0;
    while (sqlite3_step(rows) == SQLITE_ROW) {
      if (got >= lim) {
        truncated = true;
        break;
      }
      QVariantMap rec;
      for (int i = 0; i < n; ++i) {
        const char *cname = sqlite3_column_name(rows, i);
        const int type = sqlite3_column_type(rows, i);
        QVariant val;
        if (type == SQLITE_NULL)
          val = QVariant();
        else if (type == SQLITE_BLOB) {
          const int nblob = sqlite3_column_bytes(rows, i);
          val = QStringLiteral("(blob %1)").arg(nblob);
        } else if (type == SQLITE_INTEGER)
          val = QString::number(sqlite3_column_int64(rows, i));
        else if (type == SQLITE_FLOAT)
          val = QString::number(sqlite3_column_double(rows, i), 'g', 15);
        else
          val = QString::fromUtf8(reinterpret_cast<const char *>(
              sqlite3_column_text(rows, i)));
        rec.insert(QString::fromUtf8(cname), val);
      }
      sample.append(rec);
      ++got;
    }
  } else {
    out.insert(QStringLiteral("sampleNote"),
               QString::fromUtf8(sqlite3_errmsg(db)));
  }
  if (rows)
    sqlite3_finalize(rows);
  sqlite3_close(db);

  out.insert(QStringLiteral("table"), chosen.display());
  out.insert(QStringLiteral("columns"), columns);
  out.insert(QStringLiteral("sample"), sample);
  out.insert(QStringLiteral("truncated"), truncated);
  return out;
}

QJsonArray runDuckJson(const QString &path, const QString &sql, QString *error) {
  const QString bin = QStandardPaths::findExecutable(QStringLiteral("duckdb"));
  if (bin.isEmpty()) {
    if (error)
      *error = QStringLiteral("missing");
    return {};
  }
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  proc.start(bin, {QStringLiteral("-readonly"), QStringLiteral("-safe"),
                   QStringLiteral("-json"), localPath(path),
                   QStringLiteral("-c"), sql});
  if (!proc.waitForFinished(8000) ||
      proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (proc.state() != QProcess::NotRunning)
      proc.kill();
    if (error)
      *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
    return {};
  }
  const QByteArray raw = proc.readAllStandardOutput();
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (!doc.isArray())
    return {};
  return doc.array();
}

QVariantMap inspectDuck(const QString &path, const QString &table, int offset,
                        int limit) {
  if (!QFileInfo::exists(path))
    return fail(QStringLiteral("missing"));
  if (!DbPreview::looksLikeDuckDb(path))
    return fail(QStringLiteral("not a DuckDB file"));

  QVariantMap out;
  out.insert(QStringLiteral("engine"), QStringLiteral("duckdb"));
  out.insert(QStringLiteral("path"), localPath(path));
  out.insert(QStringLiteral("name"), QFileInfo(path).fileName());
  out.insert(QStringLiteral("offset"), boundOffset(offset));
  out.insert(QStringLiteral("limit"), boundLimit(limit));

  if (QStandardPaths::findExecutable(QStringLiteral("duckdb")).isEmpty()) {
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("tables"), QVariantList());
    out.insert(QStringLiteral("columns"), QVariantList());
    out.insert(QStringLiteral("sample"), QVariantList());
    out.insert(QStringLiteral("sampleNote"),
               QStringLiteral("install duckdb to browse tables"));
    out.insert(QStringLiteral("duckdb"), false);
    return out;
  }
  out.insert(QStringLiteral("duckdb"), true);

  QString err;
  const QJsonArray listed = runDuckJson(
      path,
      QStringLiteral(
          "SELECT table_schema, table_name, table_type "
          "FROM information_schema.tables "
          "WHERE table_schema NOT IN ('information_schema','pg_catalog') "
          "ORDER BY table_schema, table_name"),
      &err);
  if (listed.isEmpty() && !err.isEmpty() && err != QLatin1String("missing"))
    return fail(err.isEmpty() ? QStringLiteral("duckdb could not list tables")
                              : err);

  QVector<TableRef> tables;
  for (const QJsonValue &v : listed) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    TableRef t;
    t.schema = o.value(QStringLiteral("table_schema")).toString();
    t.name = o.value(QStringLiteral("table_name")).toString();
    t.type = o.value(QStringLiteral("table_type")).toString().toLower();
    if (t.type.contains(QLatin1String("view")))
      t.type = QStringLiteral("view");
    else
      t.type = QStringLiteral("table");
    if (!t.name.isEmpty())
      tables.append(t);
  }

  QVariantList tableList;
  for (const TableRef &t : tables) {
    QVariantMap row;
    row.insert(QStringLiteral("name"), t.display());
    row.insert(QStringLiteral("type"), t.type);
    row.insert(QStringLiteral("schema"), t.schema);
    tableList.append(row);
  }

  TableRef chosen;
  if (!table.isEmpty()) {
    for (const TableRef &t : tables) {
      if (t.display() == table || t.name == table) {
        chosen = t;
        break;
      }
    }
    if (chosen.name.isEmpty())
      chosen = parseTableRef(table);
  } else if (!tables.isEmpty()) {
    chosen = tables.constFirst();
  }

  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("tables"), tableList);
  if (chosen.name.isEmpty()) {
    out.insert(QStringLiteral("table"), QString());
    out.insert(QStringLiteral("columns"), QVariantList());
    out.insert(QStringLiteral("sample"), QVariantList());
    out.insert(QStringLiteral("sampleNote"),
               tables.isEmpty() ? QStringLiteral("no tables") : QString());
    return out;
  }

  QString qerr;
  const QString ident = chosen.qualified(&qerr);
  if (ident.isEmpty())
    return fail(qerr);

  const QJsonArray desc =
      runDuckJson(path, QStringLiteral("DESCRIBE %1").arg(ident), &err);
  QVariantList columns;
  for (const QJsonValue &v : desc) {
    if (!v.isObject())
      continue;
    const QJsonObject o = v.toObject();
    QVariantMap col;
    col.insert(QStringLiteral("name"),
               o.value(QStringLiteral("column_name")).toString());
    col.insert(QStringLiteral("type"),
               o.value(QStringLiteral("column_type")).toString());
    col.insert(QStringLiteral("notnull"),
               o.value(QStringLiteral("null")).toString() ==
                   QLatin1String("NO"));
    col.insert(QStringLiteral("pk"),
               !o.value(QStringLiteral("key")).isNull() &&
                   o.value(QStringLiteral("key")).isString() &&
                   !o.value(QStringLiteral("key")).toString().isEmpty());
    columns.append(col);
  }

  const int lim = boundLimit(limit);
  const int off = boundOffset(offset);
  const QJsonArray rows = runDuckJson(
      path,
      QStringLiteral("SELECT COLUMNS(*)::VARCHAR FROM %1 LIMIT %2 OFFSET %3")
          .arg(ident)
          .arg(lim + 1)
          .arg(off),
      &err);
  QVariantList sample;
  bool truncated = false;
  int got = 0;
  for (const QJsonValue &v : rows) {
    if (!v.isObject())
      continue;
    if (got >= lim) {
      truncated = true;
      break;
    }
    QVariantMap rec;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
      rec.insert(it.key(), jsonToVariant(it.value()));
    sample.append(rec);
    ++got;
  }
  if (sample.isEmpty() && !err.isEmpty())
    out.insert(QStringLiteral("sampleNote"), err);

  out.insert(QStringLiteral("table"), chosen.display());
  out.insert(QStringLiteral("columns"), columns);
  out.insert(QStringLiteral("sample"), sample);
  out.insert(QStringLiteral("truncated"), truncated);
  return out;
}

} // namespace

bool DbPreview::looksLikeSqlite(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const QByteArray mag = f.read(16);
  static const char kMagic[] = "SQLite format 3";
  return mag.size() == 16 && mag.startsWith(kMagic) && mag.at(15) == '\0';
}

bool DbPreview::looksLikeDuckDb(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  if (f.size() < 12 || !f.seek(8))
    return false;
  return f.read(4) == QByteArrayLiteral("DUCK");
}

QVariantMap DbPreview::inspect(const QString &path, const QString &engine,
                               const QString &table, int offset, int limit) {
  if (path.isEmpty())
    return fail(QStringLiteral("no path"));
  const QString e = engine.trimmed().toLower();
  if (e == QLatin1String("sqlite") || e == QLatin1String("sqlite3"))
    return inspectSqlite(path, table, offset, limit);
  if (e == QLatin1String("duckdb") || e == QLatin1String("duck"))
    return inspectDuck(path, table, offset, limit);
  return fail(QStringLiteral("unknown engine"));
}
