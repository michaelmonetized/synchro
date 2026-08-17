#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

struct ArchiveEntry {
  QString name;
  qint64 size = -1;
  qint64 packed = -1;
  QString kind;
};

struct ArchiveInfo {
  bool ok = false;
  QString error;
  QString format;
  QString name;
  qint64 fileSize = 0;
  qint64 uncompressed = 0;
  qint64 packed = 0;
  int files = 0;
  int dirs = 0;
  bool truncated = false;
  QString origName;
  QString mtime;
  QString method;
  QString sampleNote;
  QVector<ArchiveEntry> entries;
};

class ArchiveMeta {
public:
  static bool looksLike(const QString &path, const QString &mimeHint = QString());
  static ArchiveInfo inspect(const QString &path, int maxEntries = 200);
  static QVariantMap toMap(const ArchiveInfo &info);
};
