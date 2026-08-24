#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <atomic>

struct DirectoryEntry {
  QString name;
  QString path;
  QUrl uri;
  bool isDir = false;
  qint64 size = -1;
  qint64 mtime = 0;
  QString mime;
  QString typeLabel; // human-readable kind, e.g. "PNG image", "Folder"
  QString iconName;
  QString thumbnail;
  bool thumbnailPending = false;
  QStringList previewPaths;
  bool isHidden = false;
  bool isSymlink = false;
  QString dirKind;
  QString origPath;
  QString parentPath;
  QString detail;
  int perm = 0;
  qint64 used = -1;
  qint64 total = -1;
  int percent = -1;
};

class DirectoryLister : public QObject {
  Q_OBJECT

public:
  explicit DirectoryLister(QObject *parent = nullptr);

  // Thread-safe: drop in-flight work whose generation is older.
  void abandon(quint64 generation);

public slots:
  void requestList(quint64 generation, const QString &path);
  void requestStatNames(quint64 generation, const QString &dirPath,
                        const QStringList &names);

signals:
  void batchReady(quint64 generation, const QVector<DirectoryEntry> &batch);
  void statsReady(quint64 generation, const QVector<DirectoryEntry> &batch,
                  bool priority);
  void finished(quint64 generation, bool ok, const QString &error);

private:
  bool abandoned(quint64 generation) const;
  void listPath(quint64 generation, const QString &path);

  std::atomic<quint64> m_wanted{0};
};

Q_DECLARE_METATYPE(DirectoryEntry)
Q_DECLARE_METATYPE(QVector<DirectoryEntry>)
