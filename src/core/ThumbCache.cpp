#include "ThumbCache.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>

#include <sqlite3.h>

#include <cstdio>

namespace {

constexpr qint64 kMaxBytes = 512ll * 1024 * 1024;
constexpr int kLruCap = 384;

QByteArray encodePng(const QImage &img) {
  if (img.isNull())
    return {};
  QByteArray out;
  QBuffer buf(&out);
  if (!buf.open(QIODevice::WriteOnly))
    return {};
  if (!img.save(&buf, "PNG"))
    return {};
  return out;
}

} // namespace

ThumbCache &ThumbCache::instance() {
  static ThumbCache cache;
  return cache;
}

ThumbCache::~ThumbCache() { close(); }

QString ThumbCache::homeDir() {
  const QByteArray env = qgetenv("SYNCHRO_HOME");
  if (!env.isEmpty())
    return QString::fromLocal8Bit(env);
  return QDir::homePath() + QStringLiteral("/.synchro");
}

QString ThumbCache::dbPath() {
  return homeDir() + QStringLiteral("/thumbs.sqlite");
}

QString ThumbCache::makeKey(const QString &path, qint64 mtime, int sizePx) {
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(path.toUtf8());
  hash.addData(QByteArray::number(mtime));
  hash.addData(QByteArray::number(sizePx));
  return QString::fromLatin1(hash.result().toHex());
}

QString ThumbCache::imageUrl(const QString &path, qint64 mtime, int sizePx) {
  return QStringLiteral("image://synchrothumb/") + makeKey(path, mtime, sizePx);
}

bool ThumbCache::ensureOpen() {
  const QString path = dbPath();
  if (m_db && m_dbPath == path)
    return true;
  close();
  QDir().mkpath(homeDir());
  QFile::setPermissions(homeDir(),
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  sqlite3 *db = nullptr;
  const int rc = sqlite3_open_v2(
      QFile::encodeName(path).constData(), &db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  if (rc != SQLITE_OK || !db) {
    if (db)
      sqlite3_close(db);
    return false;
  }
  m_db = db;
  m_dbPath = path;
  exec("PRAGMA journal_mode=WAL;");
  exec("PRAGMA synchronous=NORMAL;");
  exec("PRAGMA busy_timeout=2500;");
  exec("CREATE TABLE IF NOT EXISTS thumbs ("
       " key TEXT PRIMARY KEY,"
       " path TEXT NOT NULL,"
       " mtime INTEGER NOT NULL,"
       " size_px INTEGER NOT NULL,"
       " png BLOB NOT NULL,"
       " bytes INTEGER NOT NULL,"
       " accessed INTEGER NOT NULL"
       ");");
  exec("CREATE INDEX IF NOT EXISTS thumbs_accessed ON thumbs(accessed);");
  QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
  return true;
}

void ThumbCache::close() {
  m_lru.clear();
  m_lruOrder.clear();
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
  m_dbPath.clear();
}

bool ThumbCache::exec(const char *sql) {
  if (!m_db || !sql)
    return false;
  char *err = nullptr;
  const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
  if (err)
    sqlite3_free(err);
  return rc == SQLITE_OK;
}

void ThumbCache::rememberLocked(const QString &key, const QImage &img) {
  if (key.isEmpty() || img.isNull())
    return;
  if (m_lru.contains(key))
    m_lruOrder.removeAll(key);
  m_lru.insert(key, img);
  m_lruOrder.append(key);
  while (m_lruOrder.size() > kLruCap) {
    const QString old = m_lruOrder.takeFirst();
    m_lru.remove(old);
  }
}

void ThumbCache::touchLocked(const QString &key) {
  if (!m_db || key.isEmpty())
    return;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db, "UPDATE thumbs SET accessed=? WHERE key=?;", -1,
                         &st, nullptr) != SQLITE_OK)
    return;
  sqlite3_bind_int64(st, 1, QDateTime::currentSecsSinceEpoch());
  const QByteArray k = key.toUtf8();
  sqlite3_bind_text(st, 2, k.constData(), k.size(), SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}

QByteArray ThumbCache::getPngLocked(const QString &key) {
  if (!m_db || key.isEmpty())
    return {};
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db, "SELECT png FROM thumbs WHERE key=?;", -1, &st,
                         nullptr) != SQLITE_OK)
    return {};
  const QByteArray k = key.toUtf8();
  sqlite3_bind_text(st, 1, k.constData(), k.size(), SQLITE_TRANSIENT);
  QByteArray out;
  if (sqlite3_step(st) == SQLITE_ROW) {
    const void *blob = sqlite3_column_blob(st, 0);
    const int n = sqlite3_column_bytes(st, 0);
    if (blob && n > 0)
      out = QByteArray(static_cast<const char *>(blob), n);
  }
  sqlite3_finalize(st);
  if (!out.isEmpty())
    touchLocked(key);
  return out;
}

bool ThumbCache::contains(const QString &path, qint64 mtime, int sizePx) {
  QMutexLocker lock(&m_mutex);
  if (!ensureOpen())
    return false;
  const QString key = makeKey(path, mtime, sizePx);
  if (m_lru.contains(key))
    return true;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db, "SELECT 1 FROM thumbs WHERE key=?;", -1, &st,
                         nullptr) != SQLITE_OK)
    return false;
  const QByteArray k = key.toUtf8();
  sqlite3_bind_text(st, 1, k.constData(), k.size(), SQLITE_TRANSIENT);
  const bool hit = sqlite3_step(st) == SQLITE_ROW;
  sqlite3_finalize(st);
  return hit;
}

QByteArray ThumbCache::getPng(const QString &path, qint64 mtime, int sizePx) {
  QMutexLocker lock(&m_mutex);
  if (!ensureOpen())
    return {};
  return getPngLocked(makeKey(path, mtime, sizePx));
}

QImage ThumbCache::getImage(const QString &path, qint64 mtime, int sizePx) {
  return imageForKey(makeKey(path, mtime, sizePx));
}

QImage ThumbCache::imageForKey(const QString &key) {
  QMutexLocker lock(&m_mutex);
  if (key.isEmpty() || !ensureOpen())
    return {};
  if (const auto it = m_lru.constFind(key); it != m_lru.cend()) {
    m_lruOrder.removeAll(key);
    m_lruOrder.append(key);
    return it.value();
  }
  const QByteArray png = getPngLocked(key);
  if (png.isEmpty())
    return {};
  const QImage img = QImage::fromData(png, "PNG");
  rememberLocked(key, img);
  return img;
}

void ThumbCache::evictLocked() {
  if (!m_db)
    return;
  sqlite3_stmt *sum = nullptr;
  if (sqlite3_prepare_v2(m_db, "SELECT COALESCE(SUM(bytes),0) FROM thumbs;", -1,
                         &sum, nullptr) != SQLITE_OK)
    return;
  qint64 total = 0;
  if (sqlite3_step(sum) == SQLITE_ROW)
    total = sqlite3_column_int64(sum, 0);
  sqlite3_finalize(sum);
  if (total <= kMaxBytes)
    return;
  const qint64 target = kMaxBytes * 3 / 4;
  while (total > target) {
    sqlite3_stmt *victim = nullptr;
    if (sqlite3_prepare_v2(
            m_db,
            "SELECT key, bytes FROM thumbs ORDER BY accessed ASC LIMIT 1;", -1,
            &victim, nullptr) != SQLITE_OK)
      break;
    if (sqlite3_step(victim) != SQLITE_ROW) {
      sqlite3_finalize(victim);
      break;
    }
    const QString key =
        QString::fromUtf8(reinterpret_cast<const char *>(
                              sqlite3_column_text(victim, 0)));
    const qint64 bytes = sqlite3_column_int64(victim, 1);
    sqlite3_finalize(victim);
    sqlite3_stmt *del = nullptr;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM thumbs WHERE key=?;", -1, &del,
                           nullptr) != SQLITE_OK)
      break;
    const QByteArray k = key.toUtf8();
    sqlite3_bind_text(del, 1, k.constData(), k.size(), SQLITE_TRANSIENT);
    sqlite3_step(del);
    sqlite3_finalize(del);
    m_lru.remove(key);
    m_lruOrder.removeAll(key);
    total -= bytes;
  }
}

void ThumbCache::putPng(const QString &path, qint64 mtime, int sizePx,
                        const QByteArray &png) {
  if (path.isEmpty() || png.isEmpty() || mtime <= 0 || sizePx <= 0)
    return;
  QMutexLocker lock(&m_mutex);
  if (!ensureOpen())
    return;
  const QString key = makeKey(path, mtime, sizePx);
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db,
                         "INSERT OR REPLACE INTO thumbs"
                         " (key,path,mtime,size_px,png,bytes,accessed)"
                         " VALUES (?,?,?,?,?,?,?);",
                         -1, &st, nullptr) != SQLITE_OK)
    return;
  const QByteArray k = key.toUtf8();
  const QByteArray p = path.toUtf8();
  sqlite3_bind_text(st, 1, k.constData(), k.size(), SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, p.constData(), p.size(), SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 3, mtime);
  sqlite3_bind_int(st, 4, sizePx);
  sqlite3_bind_blob(st, 5, png.constData(), png.size(), SQLITE_TRANSIENT);
  sqlite3_bind_int64(st, 6, png.size());
  sqlite3_bind_int64(st, 7, QDateTime::currentSecsSinceEpoch());
  sqlite3_step(st);
  sqlite3_finalize(st);
  rememberLocked(key, QImage::fromData(png, "PNG"));
  evictLocked();
}

void ThumbCache::putImage(const QString &path, qint64 mtime, int sizePx,
                          const QImage &img) {
  putPng(path, mtime, sizePx, encodePng(img));
}

bool ThumbCache::ingestFile(const QString &path, qint64 mtime, int sizePx,
                            const QString &pngPath) {
  QFile f(pngPath);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const QByteArray png = f.readAll();
  if (png.isEmpty())
    return false;
  putPng(path, mtime, sizePx, png);
  return true;
}

int ThumbCache::entryCount() {
  QMutexLocker lock(&m_mutex);
  if (!ensureOpen())
    return 0;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM thumbs;", -1, &st,
                         nullptr) != SQLITE_OK)
    return 0;
  int n = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    n = sqlite3_column_int(st, 0);
  sqlite3_finalize(st);
  return n;
}

qint64 ThumbCache::byteSize() {
  QMutexLocker lock(&m_mutex);
  if (!ensureOpen())
    return 0;
  sqlite3_stmt *st = nullptr;
  if (sqlite3_prepare_v2(m_db, "SELECT COALESCE(SUM(bytes),0) FROM thumbs;", -1,
                         &st, nullptr) != SQLITE_OK)
    return 0;
  qint64 n = 0;
  if (sqlite3_step(st) == SQLITE_ROW)
    n = sqlite3_column_int64(st, 0);
  sqlite3_finalize(st);
  return n;
}
