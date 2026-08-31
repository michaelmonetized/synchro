#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

// A deliberately bounded inotify surface for the catalog daemon. Synchro's
// complete tree can contain millions of directories, so correctness still
// comes from periodic reconciliation; this watcher makes active locations
// feel live without consuming a watch for every directory on the machine.
class HotSetWatcher : public QObject {
  Q_OBJECT

public:
  explicit HotSetWatcher(int maxWatches = 2048, int debounceMs = 650,
                         QObject *parent = nullptr);
  ~HotSetWatcher() override;

  bool available() const { return m_fd >= 0; }
  int watchCount() const { return m_byPath.size(); }
  int maxWatches() const { return m_maxWatches; }
  int debounceMs() const { return m_debounce.interval(); }

  bool addDirectory(const QString &path);
  int addNeighborhood(const QString &path, int maxChildren = 128);
  void setMaxWatches(int maxWatches);
  void setDebounceMs(int debounceMs);

signals:
  void directoriesChanged(const QStringList &paths);
  void directoriesDiscovered(const QStringList &paths);
  void watchCountChanged(int count);

private:
  void drain();
  void flush();
  void removeWatch(int wd, bool kernelAlreadyRemoved = false);
  void evictOldest();
  void touch(const QString &path);

  int m_fd = -1;
  int m_maxWatches = 2048;
  quint64 m_clock = 0;
  QSocketNotifier *m_notifier = nullptr;
  QTimer m_debounce;
  QHash<int, QString> m_byWatch;
  QHash<QString, int> m_byPath;
  QHash<QString, quint64> m_recency;
  QSet<QString> m_pending;
  QSet<QString> m_discovered;
};
