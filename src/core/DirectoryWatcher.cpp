#include "DirectoryWatcher.h"

#include <QFile>
#include <QSocketNotifier>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <sys/inotify.h>
#include <unistd.h>

namespace {

constexpr int kCoalesceMs = 50;
constexpr uint32_t kWatchMask = IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                IN_MOVED_TO | IN_ATTRIB | IN_DELETE_SELF |
                                IN_MOVE_SELF;

bool debugWatch() {
  static const bool on = qEnvironmentVariableIntValue("SYNCHRO_DEBUG") > 0;
  return on;
}

void logEvent(const DirectoryWatchEvent &ev) {
  if (!debugWatch())
    return;
  const char *kind = "attrib";
  switch (ev.kind) {
  case DirectoryWatchEvent::Created:
    kind = "create";
    break;
  case DirectoryWatchEvent::Deleted:
    kind = "delete";
    break;
  case DirectoryWatchEvent::Attrib:
    kind = "attrib";
    break;
  case DirectoryWatchEvent::Renamed:
    kind = "rename";
    break;
  case DirectoryWatchEvent::Gone:
    kind = "gone";
    break;
  case DirectoryWatchEvent::Overflow:
    kind = "overflow";
    break;
  }
  std::fprintf(stderr, "synchro: inotify %s %s -> %s%s\n", kind,
               qPrintable(ev.name), qPrintable(ev.newName),
               ev.isDir ? " dir" : "");
}

} // namespace

DirectoryWatcher::DirectoryWatcher(QObject *parent) : QObject(parent) {
  qRegisterMetaType<DirectoryWatchEvent>();
  qRegisterMetaType<QVector<DirectoryWatchEvent>>();
  m_flush.setSingleShot(true);
  m_flush.setInterval(kCoalesceMs);
  connect(&m_flush, &QTimer::timeout, this, &DirectoryWatcher::flush);
}

DirectoryWatcher::~DirectoryWatcher() {
  m_flush.stop();
  dropWatch();
  if (m_notifier) {
    m_notifier->setEnabled(false);
    delete m_notifier;
    m_notifier = nullptr;
  }
  if (m_fd >= 0) {
    ::close(m_fd);
    m_fd = -1;
  }
}

void DirectoryWatcher::setPath(const QString &path) {
  if (path == m_path && m_wd >= 0)
    return;
  ++m_serial;
  m_flush.stop();
  m_queue.clear();
  dropWatch();
  // Leftover CREATE/DELETE_SELF for the old wd stay readable; Linux
  // reuses wd 1, so drain must discard them before the new add_watch.
  discardKernelEvents();
  m_path = path;
  if (!m_path.isEmpty())
    addWatch();
}

void DirectoryWatcher::ensureFd() {
  if (m_fd >= 0)
    return;
  m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (m_fd < 0)
    return;
  m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
  connect(m_notifier, &QSocketNotifier::activated, this,
          [this](QSocketDescriptor, QSocketNotifier::Type) { drain(); });
}

void DirectoryWatcher::dropWatch() {
  if (m_fd >= 0 && m_wd >= 0)
    inotify_rm_watch(m_fd, m_wd);
  m_wd = -1;
}

void DirectoryWatcher::discardKernelEvents() {
  if (m_fd < 0)
    return;
  const bool wasEnabled = m_notifier && m_notifier->isEnabled();
  if (m_notifier)
    m_notifier->setEnabled(false);
  alignas(struct inotify_event) char buf[65536];
  while (true) {
    const ssize_t n = ::read(m_fd, buf, sizeof(buf));
    if (n <= 0)
      break;
  }
  if (m_notifier && wasEnabled)
    m_notifier->setEnabled(true);
}

void DirectoryWatcher::addWatch() {
  ensureFd();
  if (m_fd < 0)
    return;
  const QByteArray encoded = QFile::encodeName(m_path);
  m_wd = inotify_add_watch(m_fd, encoded.constData(), kWatchMask);
  if (m_wd < 0 && debugWatch()) {
    std::fprintf(stderr, "synchro: inotify watch failed %s: %s\n",
                 qPrintable(m_path), std::strerror(errno));
  }
}

void DirectoryWatcher::drain() {
  if (m_fd < 0)
    return;

  alignas(struct inotify_event) char buf[65536];
  while (true) {
    const ssize_t n = ::read(m_fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      break;
    }
    if (n == 0)
      break;
    ssize_t off = 0;
    while (off < n) {
      const auto *ev =
          reinterpret_cast<const struct inotify_event *>(buf + off);
      const ssize_t step =
          static_cast<ssize_t>(sizeof(struct inotify_event) + ev->len);
      if (step <= 0 || off + step > n)
        break;
      off += step;

      if (ev->mask & IN_Q_OVERFLOW) {
        RawEvent raw;
        raw.mask = ev->mask;
        raw.serial = m_serial;
        m_queue.append(raw);
        continue;
      }
      if (ev->mask & IN_IGNORED)
        continue;
      if (m_wd >= 0 && ev->wd != m_wd)
        continue;

      RawEvent raw;
      raw.mask = ev->mask;
      raw.cookie = ev->cookie;
      raw.wd = ev->wd;
      raw.serial = m_serial;
      if (ev->len > 0)
        raw.name = QFile::decodeName(QByteArray(ev->name));
      m_queue.append(raw);
    }
  }

  if (!m_queue.isEmpty() && !m_flush.isActive())
    m_flush.start();
}

void DirectoryWatcher::flush() {
  const QVector<RawEvent> queued = m_queue;
  m_queue.clear();
  if (queued.isEmpty())
    return;

  QVector<DirectoryWatchEvent> out;
  QVector<RawEvent> froms;
  QVector<RawEvent> tos;
  QVector<RawEvent> others;
  bool gone = false;
  bool overflow = false;

  for (const RawEvent &raw : queued) {
    if (raw.serial != m_serial)
      continue;
    if (raw.mask & IN_Q_OVERFLOW) {
      overflow = true;
      continue;
    }
    if (raw.mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
      gone = true;
      continue;
    }
    if (raw.mask & IN_MOVED_FROM)
      froms.append(raw);
    else if (raw.mask & IN_MOVED_TO)
      tos.append(raw);
    else
      others.append(raw);
  }

  if (gone) {
    DirectoryWatchEvent ev;
    ev.kind = DirectoryWatchEvent::Gone;
    ev.serial = m_serial;
    logEvent(ev);
    emit eventsReady({ev});
    return;
  }
  if (overflow) {
    DirectoryWatchEvent ev;
    ev.kind = DirectoryWatchEvent::Overflow;
    ev.serial = m_serial;
    logEvent(ev);
    emit eventsReady({ev});
    return;
  }

  QVector<bool> usedTo(tos.size(), false);
  for (const RawEvent &from : froms) {
    int match = -1;
    if (from.cookie != 0) {
      for (int i = 0; i < tos.size(); ++i) {
        if (!usedTo.at(i) && tos.at(i).cookie == from.cookie) {
          match = i;
          break;
        }
      }
    }
    DirectoryWatchEvent ev;
    ev.serial = from.serial;
    if (match >= 0) {
      usedTo[match] = true;
      ev.kind = DirectoryWatchEvent::Renamed;
      ev.name = from.name;
      ev.newName = tos.at(match).name;
      ev.isDir = (tos.at(match).mask & IN_ISDIR) != 0;
    } else {
      ev.kind = DirectoryWatchEvent::Deleted;
      ev.name = from.name;
      ev.isDir = (from.mask & IN_ISDIR) != 0;
    }
    out.append(ev);
  }

  for (int i = 0; i < tos.size(); ++i) {
    if (usedTo.at(i))
      continue;
    DirectoryWatchEvent ev;
    ev.kind = DirectoryWatchEvent::Created;
    ev.name = tos.at(i).name;
    ev.isDir = (tos.at(i).mask & IN_ISDIR) != 0;
    ev.serial = tos.at(i).serial;
    out.append(ev);
  }

  for (const RawEvent &raw : others) {
    DirectoryWatchEvent ev;
    ev.isDir = (raw.mask & IN_ISDIR) != 0;
    ev.name = raw.name;
    ev.serial = raw.serial;
    if (raw.mask & IN_CREATE)
      ev.kind = DirectoryWatchEvent::Created;
    else if (raw.mask & IN_DELETE)
      ev.kind = DirectoryWatchEvent::Deleted;
    else if (raw.mask & IN_ATTRIB)
      ev.kind = DirectoryWatchEvent::Attrib;
    else
      continue;
    out.append(ev);
  }

  if (out.isEmpty())
    return;
  if (debugWatch()) {
    for (const DirectoryWatchEvent &ev : out)
      logEvent(ev);
  }
  emit eventsReady(out);
}
