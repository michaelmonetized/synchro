#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <cstdint>

class QSocketNotifier;

struct DirectoryWatchEvent {
  enum Kind { Created, Deleted, Attrib, Renamed, Gone, Overflow };
  Kind kind = Attrib;
  QString name;
  QString newName;
  bool isDir = false;
};

class DirectoryWatcher : public QObject {
  Q_OBJECT

public:
  explicit DirectoryWatcher(QObject *parent = nullptr);
  ~DirectoryWatcher() override;

  QString path() const { return m_path; }
  bool watching() const { return m_wd >= 0; }

  void setPath(const QString &path);

signals:
  void eventsReady(const QVector<DirectoryWatchEvent> &events);

private:
  struct RawEvent {
    uint32_t mask = 0;
    uint32_t cookie = 0;
    int wd = -1;
    QString name;
  };

  void ensureFd();
  void dropWatch();
  void addWatch();
  void drain();
  void flush();

  QString m_path;
  int m_fd = -1;
  int m_wd = -1;
  QSocketNotifier *m_notifier = nullptr;
  QTimer m_flush;
  QVector<RawEvent> m_queue;
};

Q_DECLARE_METATYPE(DirectoryWatchEvent)
Q_DECLARE_METATYPE(QVector<DirectoryWatchEvent>)
