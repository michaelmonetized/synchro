#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct sqlite3;

// One SQLite file at ~/.synchro/thumbs.sqlite (or $SYNCHRO_HOME).
// Not a directory of PNGs — that is already 2k+ inodes in ~/.cache.
class ThumbCache {
public:
  static ThumbCache &instance();

  static QString homeDir();
  static QString dbPath();
  static QString makeKey(const QString &path, qint64 mtime, int sizePx);
  static QString imageUrl(const QString &path, qint64 mtime, int sizePx);

  bool contains(const QString &path, qint64 mtime, int sizePx);
  QByteArray getPng(const QString &path, qint64 mtime, int sizePx);
  QImage getImage(const QString &path, qint64 mtime, int sizePx);
  QImage imageForKey(const QString &key);
  void putPng(const QString &path, qint64 mtime, int sizePx,
              const QByteArray &png);
  void putImage(const QString &path, qint64 mtime, int sizePx,
                const QImage &img);
  bool ingestFile(const QString &path, qint64 mtime, int sizePx,
                  const QString &pngPath);
  void removePath(const QString &path);

  int entryCount();
  qint64 byteSize();

private:
  ThumbCache() = default;
  ~ThumbCache();
  ThumbCache(const ThumbCache &) = delete;
  ThumbCache &operator=(const ThumbCache &) = delete;

  bool ensureOpen();
  void close();
  bool exec(const char *sql);
  QByteArray getPngLocked(const QString &key);
  void touchLocked(const QString &key);
  void evictLocked();
  void rememberLocked(const QString &key, const QImage &img);

  mutable QMutex m_mutex;
  sqlite3 *m_db = nullptr;
  QString m_dbPath;
  QHash<QString, QImage> m_lru;
  QStringList m_lruOrder;
};
