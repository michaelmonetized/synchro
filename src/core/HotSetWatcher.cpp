#include "HotSetWatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>

#include <cerrno>
#include <limits>
#include <utility>

#include <sys/inotify.h>
#include <unistd.h>

namespace {

constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_CREATE | IN_DELETE |
                                IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                                IN_DELETE_SELF | IN_MOVE_SELF;

QString directoryPath(const QString &raw) {
  const QFileInfo info(QDir::cleanPath(raw));
  if (!info.exists() || !info.isDir() || info.isSymLink())
    return {};
  return info.absoluteFilePath();
}

} // namespace

HotSetWatcher::HotSetWatcher(int maxWatches, int debounceMs, QObject *parent)
    : QObject(parent), m_maxWatches(qBound(16, maxWatches, 65536)) {
  m_debounce.setSingleShot(true);
  m_debounce.setInterval(qBound(25, debounceMs, 10000));
  connect(&m_debounce, &QTimer::timeout, this, &HotSetWatcher::flush);

  m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (m_fd < 0)
    return;
  m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this,
          [this](QSocketDescriptor, QSocketNotifier::Type) { drain(); });
}

HotSetWatcher::~HotSetWatcher() {
  m_debounce.stop();
  if (m_notifier)
    m_notifier->setEnabled(false);
  if (m_fd >= 0)
    ::close(m_fd);
}

void HotSetWatcher::setMaxWatches(int maxWatches) {
  m_maxWatches = qBound(16, maxWatches, 65536);
  while (m_byPath.size() > m_maxWatches) {
    const int before = m_byPath.size();
    evictOldest();
    if (m_byPath.size() == before)
      break;
  }
}

void HotSetWatcher::setDebounceMs(int debounceMs) {
  m_debounce.setInterval(qBound(25, debounceMs, 10000));
}

void HotSetWatcher::touch(const QString &path) {
  m_recency.insert(path, ++m_clock);
}

void HotSetWatcher::evictOldest() {
  QString oldest;
  quint64 tick = std::numeric_limits<quint64>::max();
  for (auto it = m_recency.cbegin(); it != m_recency.cend(); ++it) {
    if (it.value() < tick) {
      tick = it.value();
      oldest = it.key();
    }
  }
  if (!oldest.isEmpty())
    removeWatch(m_byPath.value(oldest, -1));
}

bool HotSetWatcher::addDirectory(const QString &rawPath) {
  if (m_fd < 0)
    return false;
  const QString path = directoryPath(rawPath);
  if (path.isEmpty())
    return false;
  if (m_byPath.contains(path)) {
    touch(path);
    return true;
  }
  while (m_byPath.size() >= m_maxWatches)
    evictOldest();
  const QByteArray encoded = QFile::encodeName(path);
  const int wd = inotify_add_watch(m_fd, encoded.constData(), kWatchMask);
  if (wd < 0)
    return false;
  m_byWatch.insert(wd, path);
  m_byPath.insert(path, wd);
  touch(path);
  emit watchCountChanged(m_byPath.size());
  return true;
}

int HotSetWatcher::addNeighborhood(const QString &rawPath, int maxChildren) {
  const QString path = directoryPath(rawPath);
  if (path.isEmpty())
    return 0;
  int added = addDirectory(path) ? 1 : 0;
  const QFileInfoList children =
      QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
  const int limit = qBound(0, maxChildren, m_maxWatches);
  for (int i = 0; i < children.size() && i < limit; ++i) {
    if (!children.at(i).isSymLink() &&
        addDirectory(children.at(i).absoluteFilePath()))
      ++added;
  }
  return added;
}

void HotSetWatcher::removeWatch(int wd, bool kernelAlreadyRemoved) {
  const QString path = m_byWatch.take(wd);
  if (path.isEmpty())
    return;
  m_byPath.remove(path);
  m_recency.remove(path);
  if (!kernelAlreadyRemoved && m_fd >= 0)
    inotify_rm_watch(m_fd, wd);
  emit watchCountChanged(m_byPath.size());
}

void HotSetWatcher::drain() {
  if (m_fd < 0)
    return;
  alignas(struct inotify_event) char buffer[65536];
  while (true) {
    const ssize_t bytes = ::read(m_fd, buffer, sizeof(buffer));
    if (bytes < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      return;
    }
    if (bytes == 0)
      break;
    ssize_t offset = 0;
    while (offset < bytes) {
      const auto *event =
          reinterpret_cast<const struct inotify_event *>(buffer + offset);
      const ssize_t step =
          static_cast<ssize_t>(sizeof(struct inotify_event) + event->len);
      if (step <= 0 || offset + step > bytes)
        break;
      offset += step;

      if (event->mask & IN_Q_OVERFLOW) {
        for (const QString &path : std::as_const(m_byWatch))
          m_pending.insert(path);
        continue;
      }
      const QString parent = m_byWatch.value(event->wd);
      if (parent.isEmpty())
        continue;
      if (event->mask & IN_IGNORED) {
        removeWatch(event->wd, true);
        continue;
      }
      m_pending.insert(parent);
      touch(parent);
      if ((event->mask & IN_ISDIR) &&
          (event->mask & (IN_CREATE | IN_MOVED_TO)) && event->len > 0) {
        const QString name = QFile::decodeName(QByteArray(event->name));
        const QString child = QDir(parent).filePath(name);
        if (QFileInfo(child).isDir())
          m_discovered.insert(child);
      }
      if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF))
        removeWatch(event->wd);
    }
  }
  if (!m_pending.isEmpty() || !m_discovered.isEmpty())
    m_debounce.start();
}

void HotSetWatcher::flush() {
  QStringList changed = m_pending.values();
  QStringList discovered = m_discovered.values();
  m_pending.clear();
  m_discovered.clear();
  changed.sort();
  discovered.sort();
  if (!discovered.isEmpty()) {
    for (const QString &path : std::as_const(discovered))
      addDirectory(path);
    emit directoriesDiscovered(discovered);
  }
  if (!changed.isEmpty())
    emit directoriesChanged(changed);
}
