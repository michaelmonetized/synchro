#include "FileCatalog.h"

#include "DirectoryModel.h"
#include "FsnLayout.h"
#include "ThumbnailService.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
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

#include <sys/stat.h>

namespace {

constexpr int kDefaultMaxRows = 200;
constexpr int kImageVisualVersion = 2;
constexpr int kStrataPreviewChildLimit = 96;
constexpr int kShadowSchemaVersion = 2;
// Automatic refreshes are eligibility checks, not a promise to rebuild. The
// active generation remains queryable while a newer one is prepared.
constexpr int kShadowRefreshCheckIntervalMs = 5 * 60 * 1000;
constexpr qint64 kShadowQuietPeriodMs = 10 * 60 * 1000;
constexpr qint64 kShadowRefreshCooldownMs = 60 * 60 * 1000;
constexpr int kShadowDuckThreads = 2;

struct SceneRollup {
  qint64 bytes = 0;
  int files = 0;
  int directories = 0;
};

QMutex sceneRollupMutex;
QHash<QString, SceneRollup> sceneRollupCache;

bool cachedSceneRollup(const QString &key, SceneRollup *rollup) {
  const QMutexLocker lock(&sceneRollupMutex);
  const auto it = sceneRollupCache.constFind(key);
  if (it == sceneRollupCache.cend())
    return false;
  *rollup = it.value();
  return true;
}

void cacheSceneRollup(const QString &key, const SceneRollup &rollup) {
  const QMutexLocker lock(&sceneRollupMutex);
  sceneRollupCache.insert(key, rollup);
}

void clearSceneRollups() {
  const QMutexLocker lock(&sceneRollupMutex);
  sceneRollupCache.clear();
}

enum class CatalogSqlDialect { SQLite, DuckDb };

QString sqlString(QString value) {
  value.replace(QLatin1Char('\''), QLatin1String("''"));
  return QLatin1Char('\'') + value + QLatin1Char('\'');
}

QString normalizedPath(const QString &raw) {
  if (raw.isEmpty() || DirectoryModel::isVirtualPath(raw))
    return {};
  return QDir::cleanPath(QFileInfo(raw).absoluteFilePath());
}

QString kindExpression() {
  return QStringLiteral(
      "CASE WHEN is_dir<>0 THEN 'folder' "
      "WHEN lower(extension) IN ('jpg','jpeg','png','gif','webp','avif',"
      "'bmp','tif','tiff','svg','heic','heif','ico') THEN 'image' "
      "WHEN lower(extension) IN ('mp4','mkv','webm','mov','avi','m4v',"
      "'mpeg','mpg') THEN 'video' "
      "WHEN lower(extension) IN ('mp3','flac','wav','ogg','opus','m4a',"
      "'aac') THEN 'audio' "
      "WHEN lower(extension) IN ('pdf','doc','docx','odt','rtf','epub') "
      "THEN 'document' "
      "WHEN lower(extension) IN ('txt','md','markdown','rst','log') "
      "THEN 'text' "
      "WHEN lower(extension) IN ('c','cc','cpp','h','hpp','rs','go','py',"
      "'js','jsx','ts','tsx','java','kt','kts','rb','php','swift','sh',"
      "'bash','zsh','fish','lua','clj','cljs','ex','exs','erl','hrl',"
      "'sql','qml') THEN 'code' "
      "WHEN lower(extension) IN ('csv','tsv','json','jsonl','parquet',"
      "'arrow','feather','orc','db','sqlite','duckdb') THEN 'data' "
      "WHEN lower(extension) IN ('zip','tar','gz','bz2','xz','zst','7z',"
      "'rar','tgz') THEN 'archive' "
      "WHEN lower(extension) IN ('appimage','deb','rpm','pkg','apk') "
      "THEN 'package' "
      "WHEN lower(extension) IN ('ttf','otf','woff','woff2') THEN 'font' "
      "WHEN lower(extension) IN ('iso','img','qcow','qcow2','vdi','vmdk') "
      "THEN 'disk image' "
      "WHEN lower(extension) IN ('blend','gltf','glb','obj','stl','fbx') "
      "THEN '3d' "
      "WHEN extension='' THEN 'file' ELSE 'other' END");
}

QString fsnCategory(const QString &extension, bool isDir) {
  if (isDir)
    return QStringLiteral("folder");
  const QString ext = extension.toLower();
  static const QSet<QString> images = {
      QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
      QStringLiteral("gif"), QStringLiteral("webp"), QStringLiteral("avif"),
      QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"),
      QStringLiteral("svg"), QStringLiteral("heic"), QStringLiteral("heif"),
      QStringLiteral("ico")};
  static const QSet<QString> videos = {
      QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
      QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v"),
      QStringLiteral("mpeg"), QStringLiteral("mpg")};
  static const QSet<QString> audio = {
      QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
      QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("m4a"),
      QStringLiteral("aac")};
  static const QSet<QString> code = {
      QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
      QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("rs"),
      QStringLiteral("go"), QStringLiteral("py"), QStringLiteral("js"),
      QStringLiteral("jsx"), QStringLiteral("ts"), QStringLiteral("tsx"),
      QStringLiteral("java"), QStringLiteral("kt"), QStringLiteral("rb"),
      QStringLiteral("php"), QStringLiteral("swift"), QStringLiteral("sh"),
      QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish"),
      QStringLiteral("lua"), QStringLiteral("sql"), QStringLiteral("qml")};
  static const QSet<QString> data = {
      QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("json"),
      QStringLiteral("jsonl"), QStringLiteral("parquet"),
      QStringLiteral("arrow"), QStringLiteral("feather"),
      QStringLiteral("orc"), QStringLiteral("db"),
      QStringLiteral("sqlite"), QStringLiteral("duckdb")};
  static const QSet<QString> archives = {
      QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"),
      QStringLiteral("bz2"), QStringLiteral("xz"), QStringLiteral("zst"),
      QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("tgz")};
  static const QSet<QString> documents = {
      QStringLiteral("pdf"), QStringLiteral("doc"), QStringLiteral("docx"),
      QStringLiteral("odt"), QStringLiteral("rtf"), QStringLiteral("epub"),
      QStringLiteral("txt"), QStringLiteral("md"),
      QStringLiteral("markdown"), QStringLiteral("rst"),
      QStringLiteral("log")};
  if (images.contains(ext))
    return QStringLiteral("image");
  if (videos.contains(ext))
    return QStringLiteral("video");
  if (audio.contains(ext))
    return QStringLiteral("audio");
  if (code.contains(ext))
    return QStringLiteral("code");
  if (data.contains(ext))
    return QStringLiteral("data");
  if (archives.contains(ext))
    return QStringLiteral("archive");
  if (documents.contains(ext))
    return QStringLiteral("document");
  return ext.isEmpty() ? QStringLiteral("file") : QStringLiteral("other");
}

QString fsnAgeBucket(qint64 mtime) {
  if (mtime <= 0)
    return QStringLiteral("unknown");
  const qint64 days =
      qMax<qint64>(0, (QDateTime::currentMSecsSinceEpoch() - mtime) / 86400000);
  if (days == 0)
    return QStringLiteral("today");
  if (days <= 7)
    return QStringLiteral("week");
  if (days <= 31)
    return QStringLiteral("month");
  if (days <= 365)
    return QStringLiteral("year");
  return QStringLiteral("older");
}

QString stemExpression() {
  return QStringLiteral(
      "CASE WHEN extension<>'' AND length(name)>length(extension)+1 "
      "THEN substr(name,1,length(name)-length(extension)-1) ELSE name END");
}

QString ageDaysExpression(CatalogSqlDialect dialect) {
  if (dialect == CatalogSqlDialect::SQLite) {
    return QStringLiteral(
        "max(0,CAST(((CAST(strftime('%s','now') AS INTEGER)*1000)-mtime)"
        "/86400000 AS INTEGER))");
  }
  return QStringLiteral(
      "greatest(0,CAST(floor((epoch_ms(current_timestamp)-mtime)"
      "/86400000.0) AS BIGINT))");
}

QString ageBucketExpression(CatalogSqlDialect dialect) {
  const QString days = ageDaysExpression(dialect);
  return QStringLiteral(
             "CASE WHEN mtime<=0 THEN 'unknown' WHEN %1<1 THEN 'today' "
             "WHEN %1<7 THEN 'this week' WHEN %1<30 THEN 'this month' "
             "WHEN %1<365 THEN 'this year' ELSE 'older' END")
      .arg(days);
}

QString sizeBucketExpression() {
  return QStringLiteral(
      "CASE WHEN is_dir<>0 THEN 'folder' WHEN size=0 THEN 'empty' "
      "WHEN size<1048576 THEN '< 1 MB' "
      "WHEN size<104857600 THEN '1-100 MB' "
      "WHEN size<1073741824 THEN '100 MB-1 GB' "
      "WHEN size<10737418240 THEN '1-10 GB' ELSE '10+ GB' END");
}

QString catalogFieldExpression(const QString &key, CatalogSqlDialect dialect) {
  if (key == QLatin1String("kind"))
    return kindExpression();
  if (key == QLatin1String("stem"))
    return stemExpression();
  if (key == QLatin1String("depth"))
    return QStringLiteral("length(path)-length(replace(path,'/',''))");
  if (key == QLatin1String("age_days"))
    return ageDaysExpression(dialect);
  if (key == QLatin1String("age_bucket"))
    return ageBucketExpression(dialect);
  if (key == QLatin1String("size_bucket"))
    return sizeBucketExpression();
  if (key == QLatin1String("modified_date")) {
    return dialect == CatalogSqlDialect::SQLite
               ? QStringLiteral(
                     "CASE WHEN mtime>0 THEN strftime('%Y-%m-%d',mtime/1000,"
                     "'unixepoch','localtime') ELSE NULL END")
               : QStringLiteral(
                     "CASE WHEN mtime>0 THEN strftime(to_timestamp(mtime/"
                     "1000.0),'%Y-%m-%d') ELSE NULL END");
  }
  if (key == QLatin1String("modified_month")) {
    return dialect == CatalogSqlDialect::SQLite
               ? QStringLiteral(
                     "CASE WHEN mtime>0 THEN strftime('%Y-%m',mtime/1000,"
                     "'unixepoch','localtime') ELSE NULL END")
               : QStringLiteral(
                     "CASE WHEN mtime>0 THEN strftime(to_timestamp(mtime/"
                     "1000.0),'%Y-%m') ELSE NULL END");
  }
  if (key == QLatin1String("root"))
    return QStringLiteral("scan_root");
  return {};
}

QString derivedProjection(CatalogSqlDialect dialect) {
  const QStringList keys = {
      QStringLiteral("kind"),          QStringLiteral("stem"),
      QStringLiteral("depth"),         QStringLiteral("age_days"),
      QStringLiteral("modified_date"), QStringLiteral("modified_month"),
      QStringLiteral("size_bucket"),   QStringLiteral("age_bucket"),
      QStringLiteral("root")};
  QStringList columns;
  columns.reserve(keys.size());
  for (const QString &key : keys) {
    columns.append(QStringLiteral("%1 AS %2")
                       .arg(catalogFieldExpression(key, dialect), key));
  }
  return columns.join(QLatin1Char(','));
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

bool bumpRevision(sqlite3 *db, const char *revisionKey,
                  const char *changedAtKey) {
  if (!db || !revisionKey || !changedAtKey)
    return false;
  sqlite3_stmt *revision = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "INSERT INTO catalog_meta(key,value) VALUES(?,'1') "
          "ON CONFLICT(key) DO UPDATE SET value="
          "CAST(CAST(catalog_meta.value AS INTEGER)+1 AS TEXT);",
          -1, &revision, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(revision, 1, revisionKey, -1, SQLITE_STATIC);
  const bool bumped = sqlite3_step(revision) == SQLITE_DONE;
  sqlite3_finalize(revision);
  if (!bumped)
    return false;

  sqlite3_stmt *changed = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "INSERT INTO catalog_meta(key,value) VALUES(?,?) "
          "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
          -1, &changed, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(changed, 1, changedAtKey, -1, SQLITE_STATIC);
  const QByteArray now = QByteArray::number(QDateTime::currentMSecsSinceEpoch());
  sqlite3_bind_text(changed, 2, now.constData(), now.size(), SQLITE_TRANSIENT);
  const bool stamped = sqlite3_step(changed) == SQLITE_DONE;
  sqlite3_finalize(changed);
  return stamped;
}

bool bumpCatalogRevision(sqlite3 *db) {
  return bumpRevision(db, "catalog_revision", "catalog_changed_at");
}

bool bumpFactsRevision(sqlite3 *db) {
  return bumpRevision(db, "facts_revision", "facts_changed_at");
}

qint64 catalogMetaInteger(sqlite3 *db, const char *key) {
  if (!db)
    return 0;
  sqlite3_stmt *st = nullptr;
  qint64 value = 0;
  if (sqlite3_prepare_v2(
          db,
          "SELECT CAST(value AS INTEGER) FROM catalog_meta "
          "WHERE key=?;",
          -1, &st, nullptr) == SQLITE_OK &&
      (sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC),
       sqlite3_step(st) == SQLITE_ROW))
    value = sqlite3_column_int64(st, 0);
  if (st)
    sqlite3_finalize(st);
  return value;
}

qint64 catalogRevision(sqlite3 *db) {
  return catalogMetaInteger(db, "catalog_revision");
}

bool tableHasColumn(sqlite3 *db, const char *table, const char *column) {
  if (!db)
    return false;
  const QByteArray sql = QByteArrayLiteral("PRAGMA table_info(") + table + ')';
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql.constData(), -1, &st, nullptr) != SQLITE_OK)
    return false;
  bool found = false;
  while (sqlite3_step(st) == SQLITE_ROW) {
    const auto *name = sqlite3_column_text(st, 1);
    if (name && qstrcmp(reinterpret_cast<const char *>(name), column) == 0) {
      found = true;
      break;
    }
  }
  sqlite3_finalize(st);
  return found;
}

bool addColumnIfMissing(sqlite3 *db, const char *column,
                        const char *definition) {
  if (tableHasColumn(db, "files", column))
    return true;
  const QByteArray sql =
      QByteArrayLiteral("ALTER TABLE files ADD COLUMN ") + definition + ';';
  return execSql(db, sql.constData()) || tableHasColumn(db, "files", column);
}

void bindText(sqlite3_stmt *st, int index, const QString &value);

sqlite3 *openCatalog(QString *error = nullptr) {
  const QString path = FileCatalog::dbPath();
  QDir().mkpath(QFileInfo(path).absolutePath());
  sqlite3 *db = nullptr;
  const int rc = sqlite3_open_v2(QFile::encodeName(path).constData(), &db,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                     SQLITE_OPEN_FULLMUTEX,
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
      db, "CREATE TABLE IF NOT EXISTS files ("
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
  const bool migrated =
      ok && addColumnIfMissing(db, "file_id", "file_id TEXT") &&
      addColumnIfMissing(db, "device", "device INTEGER") &&
      addColumnIfMissing(db, "inode", "inode INTEGER") &&
      execSql(db, "CREATE TABLE IF NOT EXISTS file_facts ("
                  " file_id TEXT NOT NULL,"
                  " analyzer TEXT NOT NULL,"
                  " analyzer_version INTEGER NOT NULL,"
                  " source_size INTEGER NOT NULL DEFAULT 0,"
                  " source_mtime INTEGER NOT NULL DEFAULT 0,"
                  " key TEXT NOT NULL,"
                  " text_value TEXT,"
                  " numeric_value REAL,"
                  " updated_at INTEGER NOT NULL DEFAULT 0,"
                  " PRIMARY KEY(file_id,analyzer,key)"
                  ");"
                  "CREATE INDEX IF NOT EXISTS file_facts_analyzer "
                  "ON file_facts(analyzer,analyzer_version,key);"
                  "CREATE INDEX IF NOT EXISTS file_facts_current "
                  "ON file_facts(file_id,source_size,source_mtime);"
                  "CREATE TABLE IF NOT EXISTS catalog_meta ("
                  " key TEXT PRIMARY KEY,"
                  " value TEXT NOT NULL"
                  ");"
                  "INSERT OR IGNORE INTO catalog_meta(key,value) "
                  "VALUES('catalog_revision','0');"
                  "INSERT OR IGNORE INTO catalog_meta(key,value) "
                  "VALUES('facts_revision','0');"
                  "INSERT OR IGNORE INTO catalog_meta(key,value) "
                  "VALUES('catalog_changed_at','0');"
                  "INSERT OR IGNORE INTO catalog_meta(key,value) "
                  "VALUES('facts_changed_at','0');");
  if (!migrated) {
    if (error)
      *error = QString::fromUtf8(sqlite3_errmsg(db));
    sqlite3_close(db);
    return nullptr;
  }
  bool migrateImageFacts = true;
  sqlite3_stmt *imageVersion = nullptr;
  if (sqlite3_prepare_v2(
          db, "SELECT value FROM catalog_meta WHERE key='image.visual';", -1,
          &imageVersion, nullptr) == SQLITE_OK &&
      sqlite3_step(imageVersion) == SQLITE_ROW) {
    const auto *value = sqlite3_column_text(imageVersion, 0);
    migrateImageFacts =
        !value || QString::fromUtf8(reinterpret_cast<const char *>(value)) !=
                      QString::number(kImageVisualVersion);
  }
  if (imageVersion)
    sqlite3_finalize(imageVersion);
  if (migrateImageFacts &&
      !execSql(db,
               "BEGIN IMMEDIATE;"
               "DELETE FROM file_facts WHERE analyzer='image.visual' AND "
               "analyzer_version<2;"
               "INSERT INTO catalog_meta(key,value) VALUES('image.visual','2') "
               "ON CONFLICT(key) DO UPDATE SET value=excluded.value;"
               "COMMIT;")) {
    if (error)
      *error = QString::fromUtf8(sqlite3_errmsg(db));
    sqlite3_close(db);
    return nullptr;
  }
  QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
  return db;
}

sqlite3 *openCatalogReadOnly(QString *error = nullptr) {
  sqlite3 *db = nullptr;
  const int rc =
      sqlite3_open_v2(QFile::encodeName(FileCatalog::dbPath()).constData(), &db,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX, nullptr);
  if (rc != SQLITE_OK || !db) {
    if (error)
      *error = db ? QString::fromUtf8(sqlite3_errmsg(db))
                  : QStringLiteral("catalog unavailable");
    if (db)
      sqlite3_close(db);
    return nullptr;
  }
  sqlite3_busy_timeout(db, 5000);
  return db;
}

QVariantMap lookupCatalogMetadata(const QString &rawPath) {
  QVariantMap out;
  const QString path = normalizedPath(rawPath);
  if (path.isEmpty()) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QStringLiteral("invalid path"));
    return out;
  }

  sqlite3 *db = openCatalogReadOnly();
  if (!db) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QStringLiteral("catalog unavailable"));
    return out;
  }
  sqlite3_busy_timeout(db, 250);

  static const char *kFileSql =
      "SELECT path,parent,name,extension,is_dir,size,mtime,mime,is_hidden,"
      "is_symlink,file_id FROM files WHERE path=? LIMIT 1;";
  sqlite3_stmt *file = nullptr;
  if (sqlite3_prepare_v2(db, kFileSql, -1, &file, nullptr) != SQLITE_OK) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QString::fromUtf8(sqlite3_errmsg(db)));
    sqlite3_close(db);
    return out;
  }
  bindText(file, 1, path);
  QVariantMap row;
  QString fileId;
  qint64 size = 0;
  qint64 mtime = 0;
  if (sqlite3_step(file) == SQLITE_ROW) {
    const QStringList textKeys = {
        QStringLiteral("path"), QStringLiteral("parent"),
        QStringLiteral("name"), QStringLiteral("extension")};
    for (int i = 0; i < textKeys.size(); ++i) {
      const auto *value = sqlite3_column_text(file, i);
      if (value)
        row.insert(textKeys.at(i),
                   QString::fromUtf8(reinterpret_cast<const char *>(value)));
    }
    row.insert(QStringLiteral("is_dir"), sqlite3_column_int(file, 4) != 0);
    size = sqlite3_column_int64(file, 5);
    mtime = sqlite3_column_int64(file, 6);
    row.insert(QStringLiteral("size"), size);
    row.insert(QStringLiteral("mtime"), mtime);
    const auto *mime = sqlite3_column_text(file, 7);
    if (mime)
      row.insert(QStringLiteral("mime"),
                 QString::fromUtf8(reinterpret_cast<const char *>(mime)));
    row.insert(QStringLiteral("hidden"), sqlite3_column_int(file, 8) != 0);
    row.insert(QStringLiteral("is_symlink"), sqlite3_column_int(file, 9) != 0);
    const auto *id = sqlite3_column_text(file, 10);
    if (id)
      fileId = QString::fromUtf8(reinterpret_cast<const char *>(id));
  }
  sqlite3_finalize(file);

  if (!fileId.isEmpty()) {
    static const char *kFactsSql =
        "SELECT key,text_value,numeric_value FROM file_facts "
        "WHERE file_id=? AND source_size=? AND source_mtime=? "
        "AND analyzer='image.visual' AND analyzer_version=?;";
    sqlite3_stmt *facts = nullptr;
    if (sqlite3_prepare_v2(db, kFactsSql, -1, &facts, nullptr) == SQLITE_OK) {
      bindText(facts, 1, fileId);
      sqlite3_bind_int64(facts, 2, size);
      sqlite3_bind_int64(facts, 3, mtime);
      sqlite3_bind_int(facts, 4, kImageVisualVersion);
      while (sqlite3_step(facts) == SQLITE_ROW) {
        const auto *keyText = sqlite3_column_text(facts, 0);
        if (!keyText)
          continue;
        const QString key =
            QString::fromUtf8(reinterpret_cast<const char *>(keyText));
        if (sqlite3_column_type(facts, 1) != SQLITE_NULL) {
          row.insert(key, QString::fromUtf8(reinterpret_cast<const char *>(
                              sqlite3_column_text(facts, 1))));
        } else if (sqlite3_column_type(facts, 2) != SQLITE_NULL) {
          row.insert(key, sqlite3_column_double(facts, 2));
        }
      }
      sqlite3_finalize(facts);
    }
  }
  sqlite3_close(db);
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("found"), !row.isEmpty());
  out.insert(QStringLiteral("rows"),
             row.isEmpty() ? QVariantList() : QVariantList{row});
  return out;
}

struct CatalogRow {
  QString path;
  QString parent;
  QString name;
  QString extension;
  QString mime;
  QString fileId;
  qint64 device = 0;
  qint64 inode = 0;
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
  struct stat st{};
  const QByteArray native = QFile::encodeName(row.path);
  if (::lstat(native.constData(), &st) == 0) {
    row.device = qint64(st.st_dev);
    row.inode = qint64(st.st_ino);
    row.fileId = QString::number(qulonglong(st.st_dev), 16) + QLatin1Char(':') +
                 QString::number(qulonglong(st.st_ino), 16);
  }
  return row;
}

CatalogRow rowForPath(const QString &raw) { return rowForInfo(QFileInfo(raw)); }

void bindText(sqlite3_stmt *st, int index, const QString &value) {
  const QByteArray utf8 = value.toUtf8();
  sqlite3_bind_text(st, index, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}

bool upsertRows(sqlite3 *db, const QVector<CatalogRow> &rows,
                const QString &scanRoot = QString(), qint64 seenAt = 0,
                bool bumpTreeRevision = true,
                int *changedRowsOut = nullptr) {
  if (!db || rows.isEmpty())
    return true;
  static const char *liveSql =
      "INSERT INTO files(path,parent,name,extension,is_dir,size,mtime,mime,"
      "is_hidden,is_symlink,seen_at,scan_root,file_id,device,inode) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(path) DO UPDATE SET parent=excluded.parent,"
      "name=excluded.name,extension=excluded.extension,is_dir=excluded.is_dir,"
      "size=excluded.size,mtime=excluded.mtime,"
      "mime=CASE WHEN excluded.mime='' THEN files.mime ELSE excluded.mime END,"
      "is_hidden=excluded.is_hidden,is_symlink=excluded.is_symlink,"
      "file_id=excluded.file_id,device=excluded.device,inode=excluded.inode,"
      "scan_root=CASE WHEN files.scan_root='' THEN excluded.scan_root "
      "ELSE files.scan_root END "
      "WHERE files.parent<>excluded.parent OR files.name<>excluded.name OR "
      "files.extension<>excluded.extension OR files.is_dir<>excluded.is_dir OR "
      "files.size<>excluded.size OR files.mtime<>excluded.mtime OR "
      "files.is_hidden<>excluded.is_hidden OR "
      "files.is_symlink<>excluded.is_symlink OR "
      "coalesce(files.file_id,'')<>excluded.file_id OR "
      "coalesce(files.device,0)<>excluded.device OR "
      "coalesce(files.inode,0)<>excluded.inode OR "
      "(files.mime='' AND excluded.mime<>'') OR "
      "(files.scan_root='' AND excluded.scan_root<>'');";
  static const char *scanSql =
      "INSERT INTO files(path,parent,name,extension,is_dir,size,mtime,mime,"
      "is_hidden,is_symlink,seen_at,scan_root,file_id,device,inode) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
      "ON CONFLICT(path) DO UPDATE SET parent=excluded.parent,"
      "name=excluded.name,extension=excluded.extension,is_dir=excluded.is_dir,"
      "size=excluded.size,mtime=excluded.mtime,"
      "mime=CASE WHEN excluded.mime='' THEN files.mime ELSE excluded.mime END,"
      "is_hidden=excluded.is_hidden,is_symlink=excluded.is_symlink,"
      "seen_at=excluded.seen_at,"
      "file_id=excluded.file_id,device=excluded.device,inode=excluded.inode,"
      "scan_root=CASE WHEN excluded.scan_root='' THEN files.scan_root "
      "ELSE excluded.scan_root END;";
  const bool scanWrite = seenAt > 0;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, scanWrite ? scanSql : liveSql, -1, &st, nullptr) !=
      SQLITE_OK)
    return false;
  const qint64 now = seenAt > 0 ? seenAt : QDateTime::currentMSecsSinceEpoch();
  bool ok = true;
  int changedRows = 0;
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
    bindText(st, 13, row.fileId);
    sqlite3_bind_int64(st, 14, row.device);
    sqlite3_bind_int64(st, 15, row.inode);
    if (sqlite3_step(st) != SQLITE_DONE) {
      ok = false;
      break;
    }
    changedRows += sqlite3_changes(db);
  }
  sqlite3_finalize(st);
  if (ok && bumpTreeRevision && changedRows > 0)
    ok = bumpCatalogRevision(db);
  execSql(db, ok ? "COMMIT;" : "ROLLBACK;");
  if (ok && changedRowsOut)
    *changedRowsOut += changedRows;
  return ok;
}

bool upsertChangedRows(sqlite3 *db, const QVector<CatalogRow> &rows,
                       const QString &scanRoot, bool bumpTreeRevision = true,
                       int *changedRowsOut = nullptr) {
  return upsertRows(db, rows, scanRoot, 0, bumpTreeRevision, changedRowsOut);
}

bool writeFacts(sqlite3 *db, const CatalogRow &row, const QString &analyzer,
                int version, const QVariantMap &facts) {
  if (!db || row.fileId.isEmpty() || facts.isEmpty())
    return false;
  if (!upsertRows(db, QVector<CatalogRow>{row}))
    return false;

  execSql(db, "BEGIN IMMEDIATE;");
  sqlite3_stmt *clear = nullptr;
  if (sqlite3_prepare_v2(
          db,
          "DELETE FROM file_facts WHERE file_id=? AND analyzer=? AND "
          "(analyzer_version<>? OR source_size<>? OR source_mtime<>?);",
          -1, &clear, nullptr) != SQLITE_OK) {
    execSql(db, "ROLLBACK;");
    return false;
  }
  bindText(clear, 1, row.fileId);
  bindText(clear, 2, analyzer);
  sqlite3_bind_int(clear, 3, version);
  sqlite3_bind_int64(clear, 4, row.size);
  sqlite3_bind_int64(clear, 5, row.mtime);
  sqlite3_step(clear);
  int changedFacts = sqlite3_changes(db);
  sqlite3_finalize(clear);

  static const char *sql =
      "INSERT INTO file_facts(file_id,analyzer,analyzer_version,source_size,"
      "source_mtime,key,text_value,numeric_value,updated_at) "
      "VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(file_id,analyzer,key) DO UPDATE "
      "SET analyzer_version=excluded.analyzer_version,"
      "source_size=excluded.source_size,source_mtime=excluded.source_mtime,"
      "text_value=excluded.text_value,numeric_value=excluded.numeric_value,"
      "updated_at=excluded.updated_at "
      "WHERE file_facts.analyzer_version<>excluded.analyzer_version OR "
      "file_facts.source_size<>excluded.source_size OR "
      "file_facts.source_mtime<>excluded.source_mtime OR "
      "file_facts.text_value IS NOT excluded.text_value OR "
      "file_facts.numeric_value IS NOT excluded.numeric_value;";
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
    execSql(db, "ROLLBACK;");
    return false;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  bool ok = true;
  for (auto it = facts.cbegin(); it != facts.cend(); ++it) {
    sqlite3_reset(st);
    sqlite3_clear_bindings(st);
    bindText(st, 1, row.fileId);
    bindText(st, 2, analyzer);
    sqlite3_bind_int(st, 3, version);
    sqlite3_bind_int64(st, 4, row.size);
    sqlite3_bind_int64(st, 5, row.mtime);
    bindText(st, 6, it.key());
    const QVariant value = it.value();
    if (value.metaType().id() == QMetaType::QString)
      bindText(st, 7, value.toString());
    else
      sqlite3_bind_null(st, 7);
    if (value.metaType().id() != QMetaType::QString &&
        (value.metaType().id() == QMetaType::Bool ||
         value.canConvert<double>()))
      sqlite3_bind_double(st, 8, value.toDouble());
    else
      sqlite3_bind_null(st, 8);
    sqlite3_bind_int64(st, 9, now);
    if (sqlite3_step(st) != SQLITE_DONE) {
      ok = false;
      break;
    }
    changedFacts += sqlite3_changes(db);
  }
  sqlite3_finalize(st);
  if (ok && changedFacts > 0)
    ok = bumpFactsRevision(db);
  execSql(db, ok ? "COMMIT;" : "ROLLBACK;");
  return ok;
}

int deleteCatalogPath(sqlite3 *db, const QString &path, bool isDir) {
  if (!db || path.isEmpty())
    return 0;
  const char *sql =
      isDir ? "WITH RECURSIVE doomed(path) AS ("
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
  const QString lower =
      root == QLatin1String("/") ? root : root + QLatin1Char('/');
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
  const int count =
      sqlite3_step(st) == SQLITE_ROW ? sqlite3_column_int(st, 0) : 0;
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
  sqlite3_bind_int64(st, 4, complete ? QDateTime::currentMSecsSinceEpoch() : 0);
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
    while (pos < json.size() && (json.at(pos) == ' ' || json.at(pos) == '\t' ||
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
    while (pos < json.size() && (json.at(pos) == ' ' || json.at(pos) == '\t' ||
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
      QStringLiteral(
          "\\bfrom\\s+(here|tree|selection|facts|image_facts|projects)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  const auto match = re.match(scrubSql(sql));
  return match.hasMatch() ? match.captured(1).toLower() : QString();
}

QStringList queryGroupKeys(const QString &sql) {
  static const QSet<QString> catalogColumns = {QStringLiteral("path"),
                                               QStringLiteral("parent"),
                                               QStringLiteral("name"),
                                               QStringLiteral("extension"),
                                               QStringLiteral("is_dir"),
                                               QStringLiteral("size"),
                                               QStringLiteral("kb"),
                                               QStringLiteral("mb"),
                                               QStringLiteral("gb"),
                                               QStringLiteral("mtime"),
                                               QStringLiteral("mime"),
                                               QStringLiteral("hidden"),
                                               QStringLiteral("is_hidden"),
                                               QStringLiteral("is_symlink"),
                                               QStringLiteral("kind"),
                                               QStringLiteral("stem"),
                                               QStringLiteral("depth"),
                                               QStringLiteral("age_days"),
                                               QStringLiteral("modified_date"),
                                               QStringLiteral("modified_month"),
                                               QStringLiteral("size_bucket"),
                                               QStringLiteral("age_bucket"),
                                               QStringLiteral("root")};
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
  const QString derived =
      catalogFieldExpression(key, CatalogSqlDialect::SQLite);
  if (!derived.isEmpty())
    return derived;
  static const QSet<QString> direct = {
      QStringLiteral("path"),      QStringLiteral("parent"),
      QStringLiteral("name"),      QStringLiteral("extension"),
      QStringLiteral("is_dir"),    QStringLiteral("size"),
      QStringLiteral("mtime"),     QStringLiteral("mime"),
      QStringLiteral("is_hidden"), QStringLiteral("is_symlink")};
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
  if (!db || groupKeys.isEmpty())
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
      where.append(QStringLiteral("%1 = %2").arg(quoted, duckLiteral(value)));
  }
  QString out =
      QStringLiteral("select name, extension, size, kb, mb, gb, mtime, path, "
                     "is_dir, hidden, kind, age_days, size_bucket, age_bucket\n"
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
      *error =
          QStringLiteral("SQL workbench is read-only: SELECT or WITH only");
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

bool scopeContains(const QString &root, const QString &path) {
  return !root.isEmpty() && (root == QLatin1String("/") || path == root ||
                             path.startsWith(root + QLatin1Char('/')));
}

QVariantMap catalogScopeMetadata(const QString &cwd) {
  QVariantMap out;
  out.insert(QStringLiteral("path"), FileCatalog::dbPath());
  out.insert(QStringLiteral("coverageComplete"), false);
  out.insert(QStringLiteral("indexedRows"), 0);

  sqlite3 *db = openCatalogReadOnly();
  if (!db)
    return out;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT root,row_count,completed_at FROM scan_state "
                         "WHERE complete=1 ORDER BY length(root) DESC;",
                         -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      const auto *rootText = sqlite3_column_text(st, 0);
      if (!rootText)
        continue;
      const QString root =
          QString::fromUtf8(reinterpret_cast<const char *>(rootText));
      if (!scopeContains(root, cwd))
        continue;
      const qint64 completedAt = sqlite3_column_int64(st, 2);
      out.insert(QStringLiteral("coverageComplete"), true);
      out.insert(QStringLiteral("indexedRoot"), root);
      out.insert(QStringLiteral("indexedRows"), sqlite3_column_int64(st, 1));
      out.insert(QStringLiteral("lastCompleteScanAt"), completedAt);
      if (completedAt > 0) {
        out.insert(
            QStringLiteral("lastCompleteScanIso"),
            QDateTime::fromMSecsSinceEpoch(completedAt).toString(Qt::ISODate));
      }
      break;
    }
  }
  if (st)
    sqlite3_finalize(st);
  sqlite3_close(db);

  const QFileInfo catalog(FileCatalog::dbPath());
  if (catalog.exists()) {
    const qint64 modifiedAt = catalog.lastModified().toMSecsSinceEpoch();
    out.insert(QStringLiteral("catalogModifiedAt"), modifiedAt);
    out.insert(QStringLiteral("catalogModifiedIso"),
               catalog.lastModified().toString(Qt::ISODate));
  }
  return out;
}

QVariantMap readShadowStatus() {
  QVariantMap out;
  const QString path = FileCatalog::shadowPath();
  out.insert(QStringLiteral("path"), path);
  out.insert(QStringLiteral("available"), false);

  sqlite3 *source = openCatalogReadOnly();
  const qint64 currentRevision = catalogRevision(source);
  const qint64 currentFactsRevision =
      catalogMetaInteger(source, "facts_revision");
  const qint64 catalogChangedAt =
      catalogMetaInteger(source, "catalog_changed_at");
  const qint64 factsChangedAt =
      catalogMetaInteger(source, "facts_changed_at");
  if (source)
    sqlite3_close(source);
  out.insert(QStringLiteral("currentRevision"), currentRevision);
  out.insert(QStringLiteral("currentFactsRevision"), currentFactsRevision);
  out.insert(QStringLiteral("catalogChangedAt"), catalogChangedAt);
  out.insert(QStringLiteral("factsChangedAt"), factsChangedAt);
  out.insert(QStringLiteral("lastSourceChangeAt"),
             qMax(catalogChangedAt, factsChangedAt));

  const QFileInfo shadow(path);
  if (!shadow.isFile() || shadow.size() <= 0)
    return out;
  const QString duck =
      QStandardPaths::findExecutable(QStringLiteral("duckdb"));
  if (duck.isEmpty()) {
    out.insert(QStringLiteral("error"),
               QStringLiteral("duckdb is not installed"));
    return out;
  }

  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(
      duck,
      {QStringLiteral("-readonly"), QStringLiteral("-json"), path,
       QStringLiteral("-c"),
       QStringLiteral("SELECT * FROM _synchro_shadow_meta LIMIT 1;")});
  if (!process.waitForFinished(5000)) {
    process.kill();
    process.waitForFinished(1000);
    out.insert(QStringLiteral("error"),
               QStringLiteral("shadow status timed out"));
    return out;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    out.insert(QStringLiteral("error"),
               QString::fromUtf8(process.readAllStandardError()).trimmed());
    return out;
  }
  QJsonParseError parseError;
  const QJsonDocument document =
      QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
  if (!document.isArray() || document.array().isEmpty() ||
      !document.array().first().isObject()) {
    out.insert(QStringLiteral("error"),
               QStringLiteral("shadow metadata is unreadable"));
    return out;
  }
  const QJsonObject row = document.array().first().toObject();
  const int schemaVersion = row.value(QStringLiteral("schema_version")).toInt();
  const qint64 sourceRevision =
      row.value(QStringLiteral("source_revision")).toVariant().toLongLong();
  const bool hasFactsRevision =
      row.contains(QStringLiteral("source_facts_revision"));
  const qint64 sourceFactsRevision =
      row.value(QStringLiteral("source_facts_revision"))
          .toVariant()
          .toLongLong();
  const qint64 builtAt =
      row.value(QStringLiteral("built_at")).toVariant().toLongLong();
  out.insert(QStringLiteral("schemaVersion"), schemaVersion);
  out.insert(QStringLiteral("builtAt"), builtAt);
  out.insert(QStringLiteral("builtAtIso"),
             QDateTime::fromMSecsSinceEpoch(builtAt).toString(Qt::ISODate));
  out.insert(QStringLiteral("sourceRevision"), sourceRevision);
  out.insert(QStringLiteral("sourceFactsRevision"), sourceFactsRevision);
  out.insert(QStringLiteral("rowCount"),
             row.value(QStringLiteral("row_count")).toVariant().toLongLong());
  out.insert(QStringLiteral("lagRevisions"),
             qMax<qint64>(0, currentRevision - sourceRevision));
  out.insert(QStringLiteral("lagFactsRevisions"),
             qMax<qint64>(0, currentFactsRevision - sourceFactsRevision));
  const bool available = schemaVersion == kShadowSchemaVersion;
  out.insert(QStringLiteral("available"), available);
  out.insert(QStringLiteral("stale"),
             !available || sourceRevision != currentRevision ||
                 !hasFactsRevision ||
                 sourceFactsRevision != currentFactsRevision);
  return out;
}

QVariantMap rebuildShadowInternal(bool force) {
  QVariantMap out;
  out.insert(QStringLiteral("ok"), false);
  out.insert(QStringLiteral("path"), FileCatalog::shadowPath());
  out.insert(QStringLiteral("rebuilt"), false);

  QString sourceError;
  sqlite3 *source = openCatalog(&sourceError);
  if (!source) {
    out.insert(QStringLiteral("error"), sourceError);
    return out;
  }
  sqlite3_close(source);

  const QString shadow = FileCatalog::shadowPath();
  QDir().mkpath(QFileInfo(shadow).absolutePath());
  QLockFile lock(shadow + QStringLiteral(".lock"));
  lock.setStaleLockTime(30 * 60 * 1000);
  if (!lock.tryLock(0)) {
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("busy"), true);
    out.insert(QStringLiteral("message"),
               QStringLiteral("a shadow rebuild is already running"));
    return out;
  }

  const QVariantMap previous = readShadowStatus();
  if (!force && previous.value(QStringLiteral("available")).toBool() &&
      !previous.value(QStringLiteral("stale")).toBool()) {
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
      out.insert(it.key(), it.value());
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("current"), true);
    return out;
  }

  const QString duck =
      QStandardPaths::findExecutable(QStringLiteral("duckdb"));
  if (duck.isEmpty()) {
    out.insert(QStringLiteral("error"),
               QStringLiteral("duckdb is not installed"));
    return out;
  }
  const QString temporary =
      shadow + QStringLiteral(".build.%1.%2")
                   .arg(QCoreApplication::applicationPid())
                   .arg(QDateTime::currentMSecsSinceEpoch());
  QFile::remove(temporary);

  const QString shadowProjection =
      QStringLiteral("%1 AS kind,%2 AS stem,%3 AS depth,%4 AS modified_date,"
                     "%5 AS modified_month,%6 AS size_bucket,%7 AS root")
          .arg(kindExpression(), stemExpression(),
               catalogFieldExpression(QStringLiteral("depth"),
                                      CatalogSqlDialect::DuckDb),
               catalogFieldExpression(QStringLiteral("modified_date"),
                                      CatalogSqlDialect::DuckDb),
               catalogFieldExpression(QStringLiteral("modified_month"),
                                      CatalogSqlDialect::DuckDb),
               sizeBucketExpression(),
               catalogFieldExpression(QStringLiteral("root"),
                                      CatalogSqlDialect::DuckDb));

  const QString buildSql =
      QStringLiteral(
          "SET threads=%4; SET memory_limit='2GB'; "
          "LOAD sqlite; SET autoinstall_known_extensions=false; "
          "SET autoload_known_extensions=false; "
          "ATTACH %1 AS source (TYPE sqlite, READ_ONLY); "
          "CREATE TABLE _synchro_files AS SELECT *,%3 FROM source.files; "
          "CREATE TABLE _synchro_file_facts AS SELECT * FROM "
          "source.file_facts; "
          "CREATE TABLE _synchro_scan_state AS SELECT * FROM "
          "source.scan_state; "
          "CREATE TABLE _synchro_catalog_meta AS SELECT * FROM "
          "source.catalog_meta; "
          "CREATE TABLE _synchro_shadow_meta AS SELECT %2::INTEGER AS "
          "schema_version,epoch_ms(current_timestamp)::BIGINT AS built_at,"
          "coalesce((SELECT CAST(value AS BIGINT) FROM "
          "_synchro_catalog_meta WHERE "
          "key='catalog_revision'),0)::BIGINT AS source_revision,"
          "coalesce((SELECT CAST(value AS BIGINT) FROM "
          "_synchro_catalog_meta WHERE "
          "key='facts_revision'),0)::BIGINT AS source_facts_revision,"
          "count(*)::BIGINT AS row_count FROM _synchro_files; "
          "CREATE UNIQUE INDEX files_path ON _synchro_files(path); "
          "CREATE INDEX files_parent ON _synchro_files(parent); "
          "CREATE INDEX files_kind_size ON _synchro_files(kind,size); "
          "CREATE INDEX facts_file_id ON _synchro_file_facts(file_id); "
          "CHECKPOINT; "
          "SELECT row_count,(SELECT count(*) FROM _synchro_files)::BIGINT AS "
          "copied_rows,source_revision FROM _synchro_shadow_meta;")
          .arg(sqlString(FileCatalog::dbPath()))
          .arg(kShadowSchemaVersion)
          .arg(shadowProjection)
          .arg(kShadowDuckThreads);

  QElapsedTimer timer;
  timer.start();
  QProcess process;
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start(duck, {QStringLiteral("-bail"), QStringLiteral("-json"),
                       temporary, QStringLiteral("-c"), buildSql});
  if (!process.waitForFinished(30 * 60 * 1000)) {
    process.kill();
    process.waitForFinished(1000);
    QFile::remove(temporary);
    out.insert(QStringLiteral("error"),
               QStringLiteral("shadow rebuild timed out"));
    return out;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const QString error =
        QString::fromUtf8(process.readAllStandardError()).trimmed();
    QFile::remove(temporary);
    out.insert(QStringLiteral("error"),
               error.isEmpty() ? QStringLiteral("shadow rebuild failed")
                               : error);
    return out;
  }
  QJsonParseError parseError;
  const QJsonDocument validation =
      QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
  if (!validation.isArray() || validation.array().isEmpty() ||
      !validation.array().first().isObject()) {
    QFile::remove(temporary);
    out.insert(QStringLiteral("error"),
               QStringLiteral("shadow validation is unreadable"));
    return out;
  }
  const QJsonObject check = validation.array().first().toObject();
  const qint64 expected =
      check.value(QStringLiteral("row_count")).toVariant().toLongLong();
  const qint64 copied =
      check.value(QStringLiteral("copied_rows")).toVariant().toLongLong();
  if (expected != copied) {
    QFile::remove(temporary);
    out.insert(QStringLiteral("error"),
               QStringLiteral("shadow validation row count mismatch"));
    return out;
  }

  QFile::setPermissions(temporary, QFile::ReadOwner | QFile::WriteOwner);
  const QByteArray temporaryName = QFile::encodeName(temporary);
  const QByteArray shadowName = QFile::encodeName(shadow);
  if (::rename(temporaryName.constData(), shadowName.constData()) != 0) {
    QFile::remove(temporary);
    out.insert(QStringLiteral("error"),
               QStringLiteral("could not atomically promote shadow"));
    return out;
  }

  out = readShadowStatus();
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("rebuilt"), true);
  out.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  // A writer may have committed while DuckDB held its SQLite snapshot. The
  // promoted generation is still valid; stale=true schedules another pass.
  return out;
}

QVariantMap runDuckQuery(QString sql, const QString &cwd,
                         const QStringList &selection, int maxRows,
                         bool allowShadow = true) {
  QVariantMap out;
  const QString cleanCwd =
      normalizedPath(cwd.isEmpty() ? QDir::currentPath() : cwd);
  const QString relation = querySourceRelation(sql);
  QVariantMap scope;
  scope.insert(QStringLiteral("cwd"), cleanCwd);
  scope.insert(QStringLiteral("relation"), relation);
  scope.insert(QStringLiteral("selectionCount"), selection.size());
  out.insert(QStringLiteral("scope"), scope);
  QVariantMap catalog = catalogScopeMetadata(cleanCwd);
  const QVariantMap shadow = readShadowStatus();
  const bool useShadow =
      allowShadow && shadow.value(QStringLiteral("available")).toBool();
  catalog.insert(QStringLiteral("shadow"), shadow);
  out.insert(QStringLiteral("catalog"), catalog);
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
  if (sqlite3 *db = openCatalogReadOnly(&dbError)) {
    sqlite3_close(db);
  } else {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), dbError);
    return out;
  }

  const QString sourceFiles =
      useShadow ? QStringLiteral("main._synchro_files")
                : QStringLiteral("catalog.files");
  const QString sourceFacts =
      useShadow ? QStringLiteral("main._synchro_file_facts")
                : QStringLiteral("catalog.file_facts");
  const QString queryDerived =
      useShadow
          ? QStringLiteral(
                "kind,stem,depth,%1 AS age_days,modified_date,modified_month,"
                "size_bucket,%2 AS age_bucket,root")
                .arg(ageDaysExpression(CatalogSqlDialect::DuckDb),
                     ageBucketExpression(CatalogSqlDialect::DuckDb))
          : derivedProjection(CatalogSqlDialect::DuckDb);
  QString preamble;
  if (!useShadow)
    preamble =
        QStringLiteral("LOAD sqlite; ATTACH %1 AS catalog (TYPE sqlite, "
                       "READ_ONLY); ")
            .arg(sqlString(FileCatalog::dbPath()));
  preamble +=
      QStringLiteral("SET autoinstall_known_extensions=false; "
                     "SET autoload_known_extensions=false; "
                     "CREATE TEMP TABLE _synchro_context(cwd VARCHAR); "
                     "INSERT INTO _synchro_context VALUES (%1); "
                     "CREATE TEMP TABLE _synchro_selection(path VARCHAR);")
          .arg(sqlString(cleanCwd));
  for (const QString &path : selection)
    preamble += QStringLiteral("INSERT INTO _synchro_selection VALUES (%1);")
                    .arg(sqlString(normalizedPath(path)));
  preamble +=
      QStringLiteral(
          "CREATE TEMP VIEW files AS SELECT path,parent,name,extension,file_id,"
          "device,inode,"
          "CAST(is_dir AS BOOLEAN) AS is_dir,size,"
          "round(CAST(size AS DOUBLE)/1024.0,2) AS kb,"
          "round(CAST(size AS DOUBLE)/1048576.0,2) AS mb,"
          "round(CAST(size AS DOUBLE)/1073741824.0,2) AS gb,"
          "mtime,mime,"
          "CAST(is_hidden AS BOOLEAN) AS hidden,"
          "CAST(is_hidden AS BOOLEAN) AS is_hidden,"
          "CAST(is_symlink AS BOOLEAN) AS is_symlink,%1 FROM %2;"
          "CREATE TEMP VIEW here AS SELECT f.* FROM files f, _synchro_context "
          "c "
          "WHERE f.parent=c.cwd;"
          "CREATE TEMP VIEW tree AS SELECT f.* FROM files f, _synchro_context "
          "c WHERE f.path=c.cwd OR ("
          "f.path>=rtrim(c.cwd,'/') || '/' AND "
          "f.path<rtrim(c.cwd,'/') || '0');"
          "CREATE TEMP VIEW selection AS SELECT f.* FROM files f JOIN "
          "_synchro_selection s USING(path);"
          "CREATE TEMP VIEW facts AS SELECT f.*,x.analyzer,x.analyzer_version,"
          "x.key,x.text_value,x.numeric_value,x.updated_at FROM tree f JOIN "
          "%3 x ON x.file_id=f.file_id AND "
          "x.source_size=f.size AND x.source_mtime=f.mtime;"
          "CREATE TEMP VIEW image_facts AS SELECT f.*,"
          "max(CASE WHEN x.key='width' THEN x.numeric_value END)::BIGINT AS "
          "width,"
          "max(CASE WHEN x.key='height' THEN x.numeric_value END)::BIGINT AS "
          "height,"
          "max(CASE WHEN x.key='aspect_ratio' THEN x.numeric_value END) AS "
          "aspect_ratio,"
          "max(CASE WHEN x.key='orientation' THEN x.text_value END) AS "
          "orientation,"
          "max(CASE WHEN x.key='dominant_color' THEN x.text_value END) AS "
          "dominant_color,"
          "max(CASE WHEN x.key='average_color' THEN x.text_value END) AS "
          "average_color,"
          "max(CASE WHEN x.key='palette_0' THEN x.text_value END) AS palette_0,"
          "max(CASE WHEN x.key='palette_1' THEN x.text_value END) AS palette_1,"
          "max(CASE WHEN x.key='palette_2' THEN x.text_value END) AS palette_2,"
          "max(CASE WHEN x.key='palette_weight_0' THEN x.numeric_value END) AS "
          "palette_weight_0,"
          "max(CASE WHEN x.key='palette_weight_1' THEN x.numeric_value END) AS "
          "palette_weight_1,"
          "max(CASE WHEN x.key='palette_weight_2' THEN x.numeric_value END) AS "
          "palette_weight_2,"
          "max(CASE WHEN x.key='color_family' THEN x.text_value END) AS "
          "color_family,"
          "max(CASE WHEN x.key='brightness' THEN x.numeric_value END) AS "
          "brightness,"
          "max(CASE WHEN x.key='saturation' THEN x.numeric_value END) AS "
          "saturation,"
          "max(CASE WHEN x.key='blue_share' THEN x.numeric_value END) AS "
          "blue_share,"
          "max(CASE WHEN x.key='chromatic_share' THEN x.numeric_value END) AS "
          "chromatic_share,"
          "max(CASE WHEN x.key='visual_hash' THEN x.text_value END) AS "
          "visual_hash "
          "FROM tree f JOIN %3 x ON x.file_id=f.file_id AND "
          "x.source_size=f.size AND x.source_mtime=f.mtime "
          "WHERE x.analyzer='image.visual' AND x.analyzer_version=2 GROUP BY "
          "ALL;"
          "CREATE TEMP VIEW projects AS SELECT parent AS path,"
          "coalesce(nullif(regexp_extract(parent,'[^/]+$'),''),'/') AS name,"
          "true AS is_dir,0::BIGINT AS size,max(mtime) AS mtime,"
          "CASE WHEN count_if(lower(name) IN "
          "('pyproject.toml','setup.py','requirements.txt'))>0 THEN 'python' "
          "WHEN count_if(lower(name)='package.json')>0 THEN 'node' "
          "WHEN count_if(lower(name)='cargo.toml')>0 THEN 'rust' "
          "WHEN count_if(lower(name)='go.mod')>0 THEN 'go' "
          "WHEN count_if(lower(name) IN ('cmakelists.txt','meson.build'))>0 "
          "THEN 'native' "
          "WHEN count_if(lower(name) IN "
          "('gemfile','composer.json','mix.exs','pom.xml','build.gradle'))>0 "
          "THEN 'application' ELSE 'build' END AS project_type,"
          "string_agg(distinct name,', ' ORDER BY name) AS markers "
          "FROM tree WHERE NOT is_dir AND lower(name) IN "
          "('package.json','cargo.toml',"
          "'go.mod','pyproject.toml','setup.py','requirements.txt','cmakelists."
          "txt',"
          "'meson.build','makefile','justfile','gemfile','composer.json','mix."
          "exs',"
          "'pom.xml','build.gradle') GROUP BY parent;")
          .arg(queryDerived, sourceFiles, sourceFacts);
  const int limit = qBound(1, maxRows <= 0 ? kDefaultMaxRows : maxRows, 500);
  const QString command =
      preamble +
      QStringLiteral("SELECT * FROM (%1) AS _synchro_result LIMIT %2")
          .arg(sql, QString::number(limit + 1));

  QElapsedTimer timer;
  timer.start();
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  QStringList duckArguments;
  if (useShadow)
    duckArguments << QStringLiteral("-readonly") << QStringLiteral("-json")
                  << FileCatalog::shadowPath();
  else
    duckArguments << QStringLiteral("-json") << QStringLiteral(":memory:");
  duckArguments << QStringLiteral("-c") << command;
  proc.start(duck, duckArguments);
  if (!proc.waitForFinished(15000)) {
    proc.kill();
    proc.waitForFinished(1000);
    if (useShadow) {
      QVariantMap fallback =
          runDuckQuery(sql, cwd, selection, maxRows, false);
      fallback.insert(QStringLiteral("shadowFallbackError"),
                      QStringLiteral("shadow query timed out"));
      return fallback;
    }
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QStringLiteral("query timed out"));
    return out;
  }
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (useShadow) {
      const QString shadowError =
          QString::fromUtf8(proc.readAllStandardError()).trimmed();
      QVariantMap fallback =
          runDuckQuery(sql, cwd, selection, maxRows, false);
      fallback.insert(QStringLiteral("shadowFallbackError"), shadowError);
      return fallback;
    }
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
  const QJsonDocument doc =
      stdoutBytes.isEmpty() ? QJsonDocument(QJsonArray())
                            : QJsonDocument::fromJson(stdoutBytes, &parseError);
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
  const QStringList groupKeys = queryGroupKeys(sql);
  QHash<QByteArray, QVariantList> groupPreviews;
  bool groupShapeOk = !relation.isEmpty() && !groupKeys.isEmpty();
  for (const QJsonValue &value : std::as_const(array)) {
    if (!value.isObject() ||
        groupSignature(value.toObject(), groupKeys).isEmpty()) {
      groupShapeOk = false;
      break;
    }
  }
  if (groupShapeOk) {
    sqlite3 *previewDb = openCatalogReadOnly();
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
    const QStringList names =
        emittedColumns.isEmpty() ? first.keys() : emittedColumns;
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
        labels.append(relation.isEmpty()
                          ? QStringLiteral("result %1").arg(rowIndex + 1)
                          : relation);
      row.insert(QStringLiteral("_synchro_label"),
                 labels.join(QStringLiteral(" · ")));
    }
    rows.append(row);
  }
  out.insert(QStringLiteral("ok"), true);
  out.insert(QStringLiteral("engine"),
             useShadow ? QStringLiteral("duckdb-shadow")
                       : QStringLiteral("duckdb-sqlite"));
  out.insert(QStringLiteral("columns"), columns);
  out.insert(QStringLiteral("rows"), rows);
  out.insert(QStringLiteral("truncated"), truncated);
  out.insert(QStringLiteral("elapsedMs"), timer.elapsed());
  out.insert(QStringLiteral("cwd"), cleanCwd);
  out.insert(QStringLiteral("selection"), selection);
  out.insert(QStringLiteral("sourceRelation"), relation);
  out.insert(QStringLiteral("groupKeys"), groupKeys);
  if (relation == QLatin1String("image_facts")) {
    QVariantMap coverage;
    sqlite3 *coverageDb = openCatalogReadOnly();
    if (coverageDb) {
      const QString lower = cleanCwd == QLatin1String("/")
                                ? cleanCwd
                                : cleanCwd + QLatin1Char('/');
      const QString upper = cleanCwd == QLatin1String("/")
                                ? QStringLiteral("0")
                                : cleanCwd + QLatin1Char('0');
      const char *countSql =
          "SELECT count(*),count(x.file_id) FROM files f LEFT JOIN "
          "(SELECT DISTINCT file_id,source_size,source_mtime FROM file_facts "
          "WHERE analyzer='image.visual' AND analyzer_version=2) x ON "
          "x.file_id=f.file_id AND x.source_size=f.size AND "
          "x.source_mtime=f.mtime WHERE f.is_dir=0 AND f.path>=? AND f.path<? "
          "AND lower(f.extension) IN ('jpg','jpeg','png','gif','webp','avif',"
          "'bmp','tif','tiff','heic','heif');";
      sqlite3_stmt *count = nullptr;
      if (sqlite3_prepare_v2(coverageDb, countSql, -1, &count, nullptr) ==
          SQLITE_OK) {
        bindText(count, 1, lower);
        bindText(count, 2, upper);
        if (sqlite3_step(count) == SQLITE_ROW) {
          const qint64 total = sqlite3_column_int64(count, 0);
          const qint64 analyzed = sqlite3_column_int64(count, 1);
          coverage.insert(QStringLiteral("analyzed"), analyzed);
          coverage.insert(QStringLiteral("total"), total);
          coverage.insert(QStringLiteral("complete"), analyzed >= total);
        }
      }
      if (count)
        sqlite3_finalize(count);
      sqlite3_close(coverageDb);
    }
    out.insert(QStringLiteral("factCoverage"), coverage);
  }
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
  m_scenePool.setMaxThreadCount(1);
  m_scenePool.setExpiryTimeout(-1);
  m_scenePool.setThreadPriority(QThread::LowPriority);
  m_analysisPool.setMaxThreadCount(1);
  m_analysisPool.setExpiryTimeout(-1);
  m_analysisPool.setThreadPriority(QThread::LowPriority);
  m_snapshotTimer.setSingleShot(true);
  m_snapshotTimer.setInterval(90);
  connect(&m_snapshotTimer, &QTimer::timeout, this,
          &FileCatalog::captureSnapshot);
  m_shadowRefreshTimer.setInterval(kShadowRefreshCheckIntervalMs);
  connect(&m_shadowRefreshTimer, &QTimer::timeout, this,
          &FileCatalog::requestShadowRefresh);
  if (m_model) {
    connect(m_model, &DirectoryModel::pathChanged, this,
            &FileCatalog::scheduleSnapshot);
    connect(m_model, &DirectoryModel::countChanged, this,
            &FileCatalog::scheduleSnapshot);
    connect(m_model, &DirectoryModel::statsApplied, this,
            &FileCatalog::enqueueUpsert);
    connect(m_model, &DirectoryModel::catalogPathsRemoved, this,
            &FileCatalog::enqueueDelete);
    connect(m_model, &DirectoryModel::imageFactsReady, this,
            &FileCatalog::recordImageFacts);
    connect(m_model, &DirectoryModel::listingChanged, this, [this] {
      if (m_model && !m_model->listing())
        scheduleSnapshot();
    });
  }
  restoreScanState();
  scheduleSnapshot();
  if (QCoreApplication::applicationName() == QLatin1String("synchro") &&
      !qEnvironmentVariableIsSet("SYNCHRO_DISABLE_SHADOW_REFRESH")) {
    m_shadowRefreshTimer.start();
    QTimer::singleShot(30000, this, &FileCatalog::requestShadowRefresh);
  }
}

FileCatalog::~FileCatalog() {
  m_snapshotTimer.stop();
  m_shadowRefreshTimer.stop();
  if (m_scanCancel)
    m_scanCancel->store(true);
  if (m_sceneCancel)
    m_sceneCancel->store(true);
  m_scanPool.waitForDone();
  m_writerPool.waitForDone();
  m_queryPool.waitForDone();
  m_scenePool.waitForDone();
  m_analysisPool.waitForDone();
}

QString FileCatalog::dbPath() {
  const QByteArray home = qgetenv("SYNCHRO_HOME");
  if (!home.isEmpty())
    return QString::fromLocal8Bit(home) + QStringLiteral("/catalog.sqlite");
  return QDir::homePath() +
         QStringLiteral("/.local/share/synchro/catalog.sqlite");
}

QString FileCatalog::shadowPath() {
  const QByteArray home = qgetenv("SYNCHRO_HOME");
  if (!home.isEmpty())
    return QString::fromLocal8Bit(home) + QStringLiteral("/catalog.duckdb");
  return QDir::homePath() +
         QStringLiteral("/.local/share/synchro/catalog.duckdb");
}

QVariantMap FileCatalog::rebuildShadow(bool force) {
  return rebuildShadowInternal(force);
}

QVariantMap FileCatalog::shadowStatus() { return readShadowStatus(); }

QVariantMap FileCatalog::querySync(const QString &sql, const QString &cwd,
                                   const QStringList &selection, int maxRows) {
  return runDuckQuery(sql, cwd, selection, maxRows);
}

bool FileCatalog::validateReadOnlySql(const QString &sql, QString *error) {
  QString candidate = sql;
  return validateReadOnlyQuery(&candidate, error);
}

QString FileCatalog::sourceRelationForQuery(const QString &sql) {
  return querySourceRelation(sql);
}

QStringList FileCatalog::fields() const {
  // Row-major order for the SQL editor's compact three-column field index.
  return {QStringLiteral("path"),
          QStringLiteral("size"),
          QStringLiteral("kind"),
          QStringLiteral("parent"),
          QStringLiteral("kb"),
          QStringLiteral("size_bucket"),
          QStringLiteral("name"),
          QStringLiteral("mb"),
          QStringLiteral("age_days"),
          QStringLiteral("extension"),
          QStringLiteral("gb"),
          QStringLiteral("age_bucket"),
          QStringLiteral("stem"),
          QStringLiteral("mtime"),
          QStringLiteral("modified_date"),
          QStringLiteral("is_dir"),
          QStringLiteral("modified_month"),
          QStringLiteral("hidden"),
          QStringLiteral("depth"),
          QStringLiteral("is_hidden"),
          QStringLiteral("root"),
          QStringLiteral("mime"),
          QStringLiteral("is_symlink"),
          QStringLiteral("file_id"),
          QStringLiteral("device"),
          QStringLiteral("inode")};
}

QVariantMap FileCatalog::status() const {
  QVariantMap out;
  out.insert(QStringLiteral("indexing"), m_indexing);
  out.insert(QStringLiteral("root"), m_indexedRoot);
  out.insert(QStringLiteral("count"), m_indexedCount);
  out.insert(QStringLiteral("text"), m_statusText);
  out.insert(QStringLiteral("path"), dbPath());
  out.insert(QStringLiteral("shadow"), shadowStatus());
  return out;
}

void FileCatalog::requestShadowRefresh() {
  if (QCoreApplication::applicationName() != QLatin1String("synchro") ||
      qEnvironmentVariableIsSet("SYNCHRO_DISABLE_SHADOW_REFRESH") ||
      m_indexing)
    return;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (m_lastShadowRefreshRequestAt > 0 &&
      now - m_lastShadowRefreshRequestAt < kShadowRefreshCooldownMs)
    return;

  sqlite3 *source = openCatalogReadOnly();
  qint64 lastSourceChange =
      qMax(catalogMetaInteger(source, "catalog_changed_at"),
           catalogMetaInteger(source, "facts_changed_at"));
  if (source)
    sqlite3_close(source);
  // The WAL timestamp also catches writes from another Synchro process and
  // long full-scan batches that deliberately coalesce their revision bump.
  const QFileInfo catalogFile(dbPath());
  const QFileInfo catalogWal(dbPath() + QStringLiteral("-wal"));
  if (catalogFile.exists())
    lastSourceChange =
        qMax(lastSourceChange,
             catalogFile.lastModified().toMSecsSinceEpoch());
  if (catalogWal.exists())
    lastSourceChange =
        qMax(lastSourceChange, catalogWal.lastModified().toMSecsSinceEpoch());
  if (lastSourceChange > 0 &&
      now - lastSourceChange < kShadowQuietPeriodMs)
    return;

  const QFileInfo activeShadow(shadowPath());
  if (activeShadow.isFile() &&
      now - activeShadow.lastModified().toMSecsSinceEpoch() <
          kShadowRefreshCooldownMs)
    return;

  const QString executable = QCoreApplication::applicationFilePath();
  if (executable.isEmpty())
    return;
  m_lastShadowRefreshRequestAt = now;
  QString program = executable;
  QStringList arguments{QStringLiteral("catalog"), QStringLiteral("shadow"),
                        QStringLiteral("rebuild"),
                        QStringLiteral("--compact")};
  const QString systemdRun =
      QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
  if (!systemdRun.isEmpty()) {
    program = systemdRun;
    QStringList broker{QStringLiteral("--user"), QStringLiteral("--collect"),
                       QStringLiteral("--quiet"),
                       QStringLiteral("--service-type=exec"),
                       QStringLiteral("--property=Nice=15"),
                       QStringLiteral("--property=IOSchedulingClass=idle"),
                       QStringLiteral("--property=CPUWeight=10"),
                       QStringLiteral("--property=IOWeight=10"),
                       QStringLiteral("--property=CPUQuota=200%")};
    const QByteArray catalogHome = qgetenv("SYNCHRO_HOME");
    if (!catalogHome.isEmpty())
      broker.append(QStringLiteral("--setenv=SYNCHRO_HOME=%1")
                        .arg(QString::fromLocal8Bit(catalogHome)));
    broker.append(executable);
    broker.append(arguments);
    arguments = broker;
  }
  QProcess::startDetached(program, arguments);
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
    int removedRows = 0;
    for (const QString &path : paths) {
      sqlite3_reset(st);
      sqlite3_clear_bindings(st);
      bindText(st, 1, normalizedPath(path));
      if (sqlite3_step(st) == SQLITE_DONE)
        removedRows += sqlite3_changes(db);
    }
    sqlite3_finalize(st);
    const bool ok = removedRows == 0 || bumpCatalogRevision(db);
    execSql(db, ok ? "COMMIT;" : "ROLLBACK;");
    sqlite3_close(db);
  });
}

void FileCatalog::recordImageFacts(const QString &rawPath, qint64 mtime,
                                   const QVariantMap &facts) {
  const QString path = normalizedPath(rawPath);
  if (path.isEmpty() || facts.isEmpty())
    return;
  m_writerPool.start([path, mtime, facts] {
    const CatalogRow row = rowForPath(path);
    // A queued thumbnail completion for an older file revision must not
    // overwrite facts for the replacement now at the same path.
    if (row.path.isEmpty() || row.mtime != mtime)
      return;
    sqlite3 *db = openCatalog();
    if (!db)
      return;
    writeFacts(db, row, QStringLiteral("image.visual"), kImageVisualVersion,
               facts);
    sqlite3_close(db);
  });
}

void FileCatalog::analyzeImages(const QString &rawRoot, int maxFiles) {
  const QString root = normalizedPath(rawRoot);
  if (root.isEmpty() || m_analyzing || !QFileInfo(root).isDir())
    return;
  const int cap = qBound(1, maxFiles <= 0 ? 500 : maxFiles, 2000);
  m_analyzing = true;
  m_analysisStatus = QStringLiteral("finding unanalyzed images…");
  emit analysisChanged();
  QPointer<FileCatalog> self(this);
  m_analysisPool.start([self, root, cap] {
    sqlite3 *db = openCatalog();
    if (!db) {
      if (self)
        QMetaObject::invokeMethod(
            self,
            [self] {
              if (self) {
                self->m_analyzing = false;
                self->m_analysisStatus = QStringLiteral("facts unavailable");
                emit self->analysisChanged();
              }
            },
            Qt::QueuedConnection);
      return;
    }
    const QString lower =
        root == QLatin1String("/") ? root : root + QLatin1Char('/');
    const QString upper = root == QLatin1String("/") ? QStringLiteral("0")
                                                     : root + QLatin1Char('0');
    sqlite3_stmt *st = nullptr;
    QVector<CatalogRow> rows;
    const char *sql =
        "SELECT path FROM files f WHERE is_dir=0 AND path>=? AND path<? AND "
        "lower(extension) IN ('jpg','jpeg','png','gif','webp','avif','bmp',"
        "'tif','tiff','heic','heif') AND NOT EXISTS (SELECT 1 FROM file_facts "
        "x "
        "WHERE x.file_id=f.file_id AND x.analyzer='image.visual' AND "
        "x.analyzer_version=2 AND x.source_size=f.size AND "
        "x.source_mtime=f.mtime) ORDER BY mtime DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
      bindText(st, 1, lower);
      bindText(st, 2, upper);
      sqlite3_bind_int(st, 3, cap);
      while (sqlite3_step(st) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(st, 0);
        if (text)
          rows.append(rowForPath(
              QString::fromUtf8(reinterpret_cast<const char *>(text))));
      }
    }
    if (st)
      sqlite3_finalize(st);

    int analyzed = 0;
    for (const CatalogRow &row : std::as_const(rows)) {
      if (!self)
        break;
      const QImage image = ThumbnailService::decodeRaster(row.path, 96);
      if (image.isNull())
        continue;
      const QVariantMap facts =
          ThumbnailService::deterministicImageFacts(row.path, image);
      if (writeFacts(db, row, QStringLiteral("image.visual"),
                     kImageVisualVersion, facts))
        ++analyzed;
      if ((analyzed % 50) == 0 && self) {
        QMetaObject::invokeMethod(
            self,
            [self, analyzed, total = rows.size()] {
              if (self) {
                self->m_analysisStatus = QStringLiteral("image facts · %1 / %2")
                                             .arg(analyzed)
                                             .arg(total);
                emit self->analysisChanged();
              }
            },
            Qt::QueuedConnection);
      }
    }
    sqlite3_close(db);
    if (self)
      QMetaObject::invokeMethod(
          self,
          [self, analyzed, requested = rows.size()] {
            if (self) {
              self->m_analyzing = false;
              self->m_analysisStatus =
                  requested == 0
                      ? QStringLiteral("image facts current")
                      : QStringLiteral("image facts · %1 added").arg(analyzed);
              emit self->analysisChanged();
            }
          },
          Qt::QueuedConnection);
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
    const QVariantMap result = querySync(sql, cwd, selection, maxRows);
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

quint64 FileCatalog::metadata(const QString &path) {
  const quint64 request = ++m_nextRequest;
  QPointer<FileCatalog> self(this);
  m_queryPool.start([self, request, path] {
    const QVariantMap result = lookupCatalogMetadata(path);
    if (!self)
      return;
    QMetaObject::invokeMethod(
        self,
        [self, request, result] {
          if (self)
            emit self->metadataFinished(request, result);
        },
        Qt::QueuedConnection);
  });
  return request;
}

quint64 FileCatalog::scene(const QString &rawRoot, const QString &view,
                           bool hidden, int maxNodes, int maxDepth) {
  Q_UNUSED(maxNodes)
  Q_UNUSED(maxDepth)
  return sceneExpanded(rawRoot, view, hidden, {});
}

quint64 FileCatalog::sceneExpanded(const QString &rawRoot,
                                   const QString &view, bool hidden,
                                   const QStringList &rawExpandedPaths) {
  const quint64 request = ++m_nextRequest;
  if (m_sceneCancel)
    m_sceneCancel->store(true);
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  m_sceneCancel = cancel;
  const QString root = normalizedPath(rawRoot);
  QSet<QString> expandedPaths;
  for (const QString &rawPath : rawExpandedPaths) {
    const QString path = normalizedPath(rawPath);
    if (path != root && path.startsWith(root + QLatin1Char('/')))
      expandedPaths.insert(path);
  }
  const int depthCap = FsnLayout::kMaxDepth;
  const FsnLayout::View fsnView =
      view.compare(QLatin1String("tree"), Qt::CaseInsensitive) == 0
          ? FsnLayout::StrataVView
          : FsnLayout::MapView;
  QPointer<FileCatalog> self(this);
  m_scenePool.start(
      [self, cancel, request, root, hidden, expandedPaths, depthCap, fsnView] {
        QVariantMap result;
        result.insert(QStringLiteral("source"), QStringLiteral("catalog"));
        result.insert(QStringLiteral("root"), root);
        result.insert(QStringLiteral("maxDepth"), depthCap);
        if (root.isEmpty()) {
          result.insert(QStringLiteral("ok"), false);
          result.insert(QStringLiteral("error"), QStringLiteral("invalid root"));
        } else {
          sqlite3 *db = nullptr;
          const int rc = sqlite3_open_v2(
              QFile::encodeName(FileCatalog::dbPath()).constData(), &db,
              SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
          if (rc != SQLITE_OK || !db) {
            result.insert(QStringLiteral("ok"), false);
            result.insert(QStringLiteral("error"),
                          db ? QString::fromUtf8(sqlite3_errmsg(db))
                             : QStringLiteral("catalog unavailable"));
            if (db)
              sqlite3_close(db);
          } else {
            sqlite3_busy_timeout(db, 1200);
            sqlite3_progress_handler(
                db, 2000,
                [](void *context) {
                  return static_cast<std::atomic_bool *>(context)->load() ? 1
                                                                          : 0;
                },
                cancel.get());
            static const char *sql =
                "SELECT f.path,f.parent,f.name,f.extension,f.is_dir,f.size,"
                "f.mtime,f.mime,f.is_hidden,f.is_symlink,"
                "CASE WHEN f.is_dir<>0 THEN MAX(32768,COALESCE(SUM("
                "CASE WHEN c.path IS NULL THEN 0 WHEN c.is_dir<>0 THEN 32768 "
                "WHEN c.size>0 THEN c.size ELSE 1 END),0)) "
                "ELSE MAX(1,f.size) END AS display_bytes,"
                "COUNT(c.path) AS child_count,"
                "COALESCE(SUM(CASE WHEN c.path IS NOT NULL AND c.is_dir=0 "
                "THEN 1 ELSE 0 END),0) AS file_count,"
                "COALESCE(SUM(CASE WHEN c.is_dir<>0 THEN 1 ELSE 0 END),0) "
                "AS dir_count,COUNT(*) OVER() AS sibling_count "
                "FROM files f LEFT JOIN files c ON c.parent=f.path "
                "AND (?<>0 OR c.is_hidden=0) "
                "WHERE f.parent=? AND (?<>0 OR f.is_hidden=0) "
                "GROUP BY f.path "
                "ORDER BY f.is_dir DESC,display_bytes DESC,"
                "f.name COLLATE NOCASE;";
            sqlite3_stmt *st = nullptr;
            const int prepared = sqlite3_prepare_v2(db, sql, -1, &st, nullptr);
            if (prepared != SQLITE_OK) {
              result.insert(QStringLiteral("ok"), false);
              result.insert(QStringLiteral("error"),
                            QString::fromUtf8(sqlite3_errmsg(db)));
            } else {
              QVector<FsnLayout::Item> items;
              QVector<int> rootDirectories;
              struct SceneParent {
                QString path;
                int depth = 0;
                int knownChildren = 0;
                bool previewOnly = false;
              };
              QQueue<SceneParent> parents;
              parents.enqueue({root, 0, 0});
              qint64 collapsedCount = 0;
              int rootChildCount = 0;
              auto columnText = [](sqlite3_stmt *row, int column) {
                const auto *text = sqlite3_column_text(row, column);
                return text ? QString::fromUtf8(
                                  reinterpret_cast<const char *>(text))
                            : QString();
              };
              while (!parents.isEmpty() && !cancel->load()) {
                const auto current = parents.dequeue();
                sqlite3_reset(st);
                sqlite3_clear_bindings(st);
                sqlite3_bind_int(st, 1, hidden ? 1 : 0);
                bindText(st, 2, current.path);
                sqlite3_bind_int(st, 3, hidden ? 1 : 0);
                int siblingCount = 0;
                int admitted = 0;
                while (!cancel->load() && sqlite3_step(st) == SQLITE_ROW) {
                  if (current.previewOnly &&
                      admitted >= kStrataPreviewChildLimit)
                    break;
                  FsnLayout::Item item;
                  item.path = columnText(st, 0);
                  item.parentPath = columnText(st, 1);
                  item.name = columnText(st, 2);
                  item.extension = columnText(st, 3);
                  item.isDir = sqlite3_column_int(st, 4) != 0;
                  item.mtime = sqlite3_column_int64(st, 6);
                  item.mime = columnText(st, 7);
                  item.hidden = sqlite3_column_int(st, 8) != 0;
                  item.isLink = sqlite3_column_int(st, 9) != 0;
                  item.bytes = sqlite3_column_int64(st, 10);
                  item.childCount = sqlite3_column_int(st, 11);
                  item.fileCount = sqlite3_column_int(st, 12);
                  item.dirCount = sqlite3_column_int(st, 13);
                  siblingCount = sqlite3_column_int(st, 14);
                  if (current.depth == 0)
                    rootChildCount = siblingCount;
                  item.category = fsnCategory(item.extension, item.isDir);
                  item.ageBucket = fsnAgeBucket(item.mtime);
                  item.aggregate = item.isDir && item.childCount > 0;
                  item.expanded = expandedPaths.contains(item.path);
                  item.previewChildren =
                      fsnView == FsnLayout::StrataVView &&
                      current.depth == 0 && item.isDir;
                  if (current.depth == 0 && item.isDir && !item.isLink)
                    rootDirectories.append(items.size());
                  items.append(item);
                  ++admitted;
                  if (item.isDir && item.childCount > 0) {
                    const bool rooftopPreview = item.previewChildren;
                    if ((item.expanded || rooftopPreview) &&
                        current.depth + 1 < depthCap)
                      parents.enqueue({item.path, current.depth + 1,
                                       item.childCount,
                                       rooftopPreview && !item.expanded});
                    else
                      collapsedCount += item.childCount;
                  }
                }
                if (current.previewOnly && siblingCount > admitted)
                  collapsedCount += siblingCount - admitted;
              }
              sqlite3_finalize(st);
              st = nullptr;
              int rollupHits = 0;
              int rollupMisses = 0;
              QElapsedTimer rollupTimer;
              rollupTimer.start();
              sqlite3_stmt *rollupStatement = nullptr;
              static const char *rollupSql =
                  "SELECT COALESCE(SUM(CASE WHEN is_dir=0 AND size>0 "
                  "THEN size ELSE 0 END),0),"
                  "COALESCE(SUM(CASE WHEN is_dir=0 THEN 1 ELSE 0 END),0),"
                  "COALESCE(SUM(CASE WHEN is_dir<>0 THEN 1 ELSE 0 END),0) "
                  "FROM files WHERE path>=? AND path<?;";
              if (!rootDirectories.isEmpty())
                sqlite3_prepare_v2(db, rollupSql, -1, &rollupStatement,
                                   nullptr);
              const QString cachePrefix = FileCatalog::dbPath() + QLatin1Char('\n');
              for (const int itemIndex : std::as_const(rootDirectories)) {
                if (cancel->load())
                  break;
                FsnLayout::Item &item = items[itemIndex];
                const QString cacheKey = cachePrefix + item.path;
                SceneRollup rollup;
                bool haveRollup = false;
                if (cachedSceneRollup(cacheKey, &rollup)) {
                  ++rollupHits;
                  haveRollup = true;
                } else if (rollupStatement) {
                  sqlite3_reset(rollupStatement);
                  sqlite3_clear_bindings(rollupStatement);
                  bindText(rollupStatement, 1,
                           item.path + QLatin1Char('/'));
                  bindText(rollupStatement, 2,
                           item.path + QLatin1Char('0'));
                  if (sqlite3_step(rollupStatement) == SQLITE_ROW) {
                    rollup.bytes = sqlite3_column_int64(rollupStatement, 0);
                    rollup.files = sqlite3_column_int(rollupStatement, 1);
                    rollup.directories =
                        sqlite3_column_int(rollupStatement, 2);
                    cacheSceneRollup(cacheKey, rollup);
                    ++rollupMisses;
                    haveRollup = true;
                  }
                }
                if (haveRollup) {
                  item.bytes = std::max(rollup.bytes, qint64(0));
                  item.fileCount = rollup.files;
                  item.dirCount = rollup.directories;
                  item.aggregate =
                      rollup.files > 0 || rollup.directories > 0;
                }
              }
              if (rollupStatement)
                sqlite3_finalize(rollupStatement);
              if (!cancel->load()) {
                const QFileInfo rootInfo(root);
                const QVector<FsnLayout::Prim> prims = FsnLayout::buildFromItems(
                    rootInfo.fileName().isEmpty() ? root : rootInfo.fileName(),
                    root, items, fsnView,
                    QStringList(expandedPaths.begin(), expandedPaths.end()));
                result.insert(QStringLiteral("ok"), true);
                result.insert(QStringLiteral("boxes"),
                              FsnLayout::toVariantList(prims));
                result.insert(QStringLiteral("nodeCount"), items.size());
                result.insert(QStringLiteral("primitiveCount"), prims.size());
                result.insert(QStringLiteral("rootChildCount"), rootChildCount);
                result.insert(QStringLiteral("expandedCount"),
                              expandedPaths.size());
                result.insert(QStringLiteral("rollupCacheHits"), rollupHits);
                result.insert(QStringLiteral("rollupCacheMisses"),
                              rollupMisses);
                result.insert(QStringLiteral("rollupMs"),
                              rollupTimer.elapsed());
                result.insert(QStringLiteral("rooftopPreviewLimit"),
                              kStrataPreviewChildLimit);
                result.insert(QStringLiteral("collapsedCount"),
                              collapsedCount);
                result.insert(QStringLiteral("omittedCount"), 0);
                result.insert(QStringLiteral("truncated"), false);
              }
            }
            if (st)
              sqlite3_finalize(st);
            sqlite3_close(db);
          }
        }
        if (!self || cancel->load())
          return;
        QMetaObject::invokeMethod(
            self,
            [self, request, result] {
              if (self)
                emit self->sceneFinished(request, result);
            },
            Qt::QueuedConnection);
      });
  return request;
}

QString FileCatalog::sourceRelation(const QString &sql) const {
  return sourceRelationForQuery(sql);
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
          states.append(
              {QString::fromUtf8(reinterpret_cast<const char *>(text)),
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
              db, "SELECT scan_root FROM files WHERE scan_root<>'' LIMIT 1;",
              -1, &rootProbe, nullptr) == SQLITE_OK &&
          sqlite3_step(rootProbe) == SQLITE_ROW) {
        const auto *text = sqlite3_column_text(rootProbe, 0);
        if (text)
          legacyRoot = QString::fromUtf8(reinterpret_cast<const char *>(text));
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
  // Recursive visual totals are derived from the durable tree and cached in
  // process. Any explicit full or incremental refresh invalidates them.
  clearSceneRollups();
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

      const QString lower =
          root == QLatin1String("/") ? root : root + QLatin1Char('/');
      const QString upper = root == QLatin1String("/")
                                ? QStringLiteral("0")
                                : root + QLatin1Char('0');
      int checked = 0;
      int changedDirs = 0;
      int changedRows = 0;
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

      const auto reconcileDirectory = [&](const DirectoryStamp &stamp) {
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
        const qint64 currentMtime = dirInfo.lastModified().toMSecsSinceEpoch();
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
        if (sqlite3_prepare_v2(db,
                               "SELECT path,is_dir FROM files WHERE parent=?;",
                               -1, &childQuery, nullptr) == SQLITE_OK) {
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

        const QFileInfoList entries =
            QDir(stamp.path)
                .entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System |
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
        upsertChangedRows(db, rows, root, false, &changedRows);
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
          const QString path =
              QString::fromUtf8(reinterpret_cast<const char *>(text));
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
        if (changedRows > 0 || removedRows > 0)
          bumpCatalogRevision(db);
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
                  elapsedMs < 1000 ? QStringLiteral("%L1 ms").arg(elapsedMs)
                                   : QStringLiteral("%L1 s").arg(
                                         elapsedMs / 1000.0, 0, 'f', 1);
              self->setStatus(
                  false, root, count,
                  QStringLiteral("Tree refreshed in %1 · %L2 folders changed · "
                                 "%L3 stale rows removed · %L4 rows")
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
    int scanWrites = 0;
    int count = 0;
    bool capped = false;
    bool stopped = false;
    while (!pending.isEmpty() && count < cap && !cancelled->load()) {
      const QString dirPath = pending.dequeue();
      const QDir dir(dirPath);
      const QFileInfoList entries = dir.entryInfoList(
          QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
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
          upsertRows(db, batch, root, scanToken, false, &scanWrites);
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
      upsertRows(db, batch, root, scanToken, false, &scanWrites);
    // A complete scan is authoritative for this root. Capped scans retain the
    // previous tail because absence beyond the cap does not mean deletion.
    if (!capped && !cancelled->load()) {
      int staleRows = 0;
      sqlite3_stmt *st = nullptr;
      if (sqlite3_prepare_v2(
              db, "DELETE FROM files WHERE scan_root=? AND seen_at<>?;", -1,
              &st, nullptr) == SQLITE_OK) {
        bindText(st, 1, root);
        sqlite3_bind_int64(st, 2, scanToken);
        if (sqlite3_step(st) == SQLITE_DONE)
          staleRows = sqlite3_changes(db);
      }
      if (st)
        sqlite3_finalize(st);
      if (scanWrites > 0 || staleRows > 0)
        bumpCatalogRevision(db);
      persistScanState(db, root, count, true);
    } else if (!cancelled->load() && scanWrites > 0) {
      // A bounded scan is intentionally incomplete, but its newly available
      // rows still need to invalidate the shadow once—not once per batch.
      bumpCatalogRevision(db);
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
                capped ? QStringLiteral(
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
  const bool completedIndex = m_indexing && !indexing && count > 0;
  m_indexing = indexing;
  m_indexedRoot = root;
  m_indexedCount = count;
  m_statusText = text;
  emit statusChanged();
  if (completedIndex)
    requestShadowRefresh();
}
