#include "VolumeStore.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#endif

namespace {

const char *kIgnoredFs[] = {
    "autofs",        "bpf",         "cgroup",     "cgroup2",
    "configfs",      "debugfs",     "devpts",     "devtmpfs",
    "efivarfs",      "fusectl",     "hugetlbfs",  "mqueue",
    "nsfs",          "overlay",     "proc",       "pstore",
    "ramfs",         "rpc_pipefs",  "securityfs", "squashfs",
    "sysfs",         "tracefs",     "binfmt_misc"};

const char *kIgnoredPrefix[] = {"/proc",
                                "/sys",
                                "/dev",
                                "/run/user",
                                "/snap",
                                "/var/lib/snapd",
                                "/var/lib/docker",
                                "/var/lib/containers",
                                "/boot"};

bool eq(const QString &a, const char *b) {
  return a.compare(QLatin1String(b), Qt::CaseInsensitive) == 0;
}

} // namespace

VolumeStore &VolumeStore::instance() {
  static VolumeStore store;
  return store;
}

VolumeStore::VolumeStore(QObject *parent) : QObject(parent) {
  m_debounce = new QTimer(this);
  m_debounce->setSingleShot(true);
  m_debounce->setInterval(250);
  connect(m_debounce, &QTimer::timeout, this, &VolumeStore::refresh);
  refresh();
}

void VolumeStore::startWatching() {
  if (m_watching)
    return;
  m_watching = true;
  m_watch = new QFileSystemWatcher(this);
  const QString mounts = QStringLiteral("/proc/self/mounts");
  if (QFileInfo::exists(mounts))
    m_watch->addPath(mounts);
  const QString media =
      QDir::homePath().isEmpty()
          ? QString()
          : QStringLiteral("/run/media/") + QFileInfo(QDir::homePath()).fileName();
  if (!media.isEmpty() && QFileInfo::exists(media))
    m_watch->addPath(media);
  connect(m_watch, &QFileSystemWatcher::fileChanged, this,
          [this] { m_debounce->start(); });
  connect(m_watch, &QFileSystemWatcher::directoryChanged, this,
          [this] { m_debounce->start(); });
}

void VolumeStore::setScanHook(ScanHook hook) { m_scanHook = std::move(hook); }

void VolumeStore::setEjectHook(EjectHook hook) { m_ejectHook = std::move(hook); }

void VolumeStore::setInventoryForTest(const QVector<Volume> &vols) {
  m_scanHook = [vols] { return vols; };
  if (m_volumes == vols)
    return;
  m_volumes = vols;
  emit changed();
}

QString VolumeStore::unescapeMountField(const QString &field) {
  QString out;
  out.reserve(field.size());
  for (int i = 0; i < field.size(); ++i) {
    if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
      bool ok = false;
      const int ch = field.mid(i + 1, 3).toInt(&ok, 8);
      if (ok) {
        out.append(QChar(ch));
        i += 3;
        continue;
      }
    }
    out.append(field.at(i));
  }
  return out;
}

bool VolumeStore::isIgnoredFs(const QString &fstype) {
  const QString fs = fstype.toLower();
  if (fs.startsWith(QLatin1String("fuse.gvfs")) ||
      fs == QLatin1String("fuse.portal"))
    return true;
  for (const char *n : kIgnoredFs) {
    if (eq(fs, n))
      return true;
  }
  return false;
}

bool VolumeStore::isIgnoredMount(const QString &mount) {
  if (mount.isEmpty())
    return true;
  for (const char *p : kIgnoredPrefix) {
    const QLatin1String pre(p);
    if (mount == pre || mount.startsWith(QString(pre) + QLatin1Char('/')))
      return true;
  }
  return false;
}

bool VolumeStore::looksLikeDiskFs(const QString &fstype) {
  const QString fs = fstype.toLower();
  static const char *const kOk[] = {
      "ext4", "ext3", "ext2",  "xfs",     "btrfs",    "f2fs",  "zfs",
      "vfat", "exfat", "ntfs", "ntfs3",   "fuseblk",  "udf",   "iso9660",
      "bcachefs", "jfs", "reiserfs", "hfsplus", "apfs"};
  for (const char *n : kOk) {
    if (eq(fs, n))
      return true;
  }
  return false;
}

QString VolumeStore::formatBytes(qint64 n) {
  if (n < 0)
    return QStringLiteral("—");
  const double g = 1024.0 * 1024.0 * 1024.0;
  const double m = 1024.0 * 1024.0;
  if (n >= qint64(g))
    return QString::number(n / g, 'f', n >= qint64(10 * g) ? 0 : 1) +
           QLatin1Char('G');
  if (n >= qint64(m))
    return QString::number(n / m, 'f', n >= qint64(10 * m) ? 0 : 1) +
           QLatin1Char('M');
  if (n >= 1024)
    return QString::number(n / 1024.0, 'f', 0) + QLatin1Char('K');
  return QString::number(n) + QLatin1Char('B');
}

QString VolumeStore::detailText(const Volume &v) {
  QStringList bits;
  if (v.total > 0)
    bits.append(formatBytes(v.free) + QStringLiteral(" free / ") +
                formatBytes(v.total));
  if (!v.fstype.isEmpty())
    bits.append(v.fstype);
  if (v.mountPoint == QLatin1String("/"))
    bits.append(QStringLiteral("system"));
  if (v.removable)
    bits.append(QStringLiteral("removable"));
  return bits.join(QStringLiteral(" · "));
}

QString VolumeStore::chipLabel(const Volume &v) {
  QString name = v.label;
  if (name.size() > 12)
    name = name.left(11) + QChar(0x2026);
  if (v.total > 0)
    return name + QLatin1Char(' ') + formatBytes(v.free);
  return name;
}

void VolumeStore::fillStat(Volume *v) {
  if (!v || v->mountPoint.isEmpty())
    return;
#ifdef Q_OS_UNIX
  struct statvfs st {};
  if (statvfs(QFile::encodeName(v->mountPoint).constData(), &st) != 0)
    return;
  const qint64 frsize = qint64(st.f_frsize ? st.f_frsize : st.f_bsize);
  v->total = frsize * qint64(st.f_blocks);
  v->free = frsize * qint64(st.f_bavail);
  v->used = v->total > v->free ? v->total - v->free : 0;
#else
  Q_UNUSED(v);
#endif
}

bool VolumeStore::sysfsRemovable(int major, int minor) {
#ifdef Q_OS_UNIX
  const QString node = QStringLiteral("/sys/dev/block/%1:%2")
                           .arg(major)
                           .arg(minor);
  QString cur = QFileInfo(node).canonicalFilePath();
  if (cur.isEmpty())
    cur = node;
  for (int i = 0; i < 8; ++i) {
    QFile f(cur + QStringLiteral("/removable"));
    if (f.open(QIODevice::ReadOnly)) {
      const QByteArray b = f.readAll().trimmed();
      return b == "1";
    }
    const QFileInfo parent(cur + QStringLiteral("/.."));
    const QString up = parent.canonicalFilePath();
    if (up.isEmpty() || up == cur)
      break;
    cur = up;
    if (!cur.startsWith(QLatin1String("/sys/")))
      break;
  }
#else
  Q_UNUSED(major);
  Q_UNUSED(minor);
#endif
  return false;
}

QString VolumeStore::blockDevice(int major, int minor) {
#ifdef Q_OS_UNIX
  const QString node = QStringLiteral("/dev/block/%1:%2")
                           .arg(major)
                           .arg(minor);
  const QString canon = QFileInfo(node).canonicalFilePath();
  return canon.isEmpty() ? node : canon;
#else
  Q_UNUSED(major);
  Q_UNUSED(minor);
  return {};
#endif
}

QString VolumeStore::volumeLabel(const Volume &v, const QString &homePath,
                                 const QString &homeMount) {
  if (v.mountPoint == QLatin1String("/"))
    return QStringLiteral("/");
  if (!homeMount.isEmpty() && v.mountPoint == homeMount)
    return QStringLiteral("home");
  const QString base = QFileInfo(v.mountPoint).fileName();
  if (!base.isEmpty() && base != QLatin1String("/"))
    return base;
  const QString src = QFileInfo(v.source).fileName();
  if (!src.isEmpty())
    return src;
  Q_UNUSED(homePath);
  return v.mountPoint;
}

QVector<VolumeStore::Volume>
VolumeStore::parseMountinfo(const QString &text, const QString &homePath) {
  QVector<Volume> raw;
  const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    const int dash = line.indexOf(QLatin1String(" - "));
    if (dash < 0)
      continue;
    const QString left = line.left(dash);
    const QString right = line.mid(dash + 3);
    const QStringList l = left.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList r = right.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (l.size() < 5 || r.size() < 2)
      continue;
    const QString root = unescapeMountField(l.at(3));
    Volume v;
    v.mountPoint = unescapeMountField(l.at(4));
    const bool media =
        v.mountPoint.startsWith(QLatin1String("/run/media/")) ||
        v.mountPoint.startsWith(QLatin1String("/media/")) ||
        v.mountPoint.startsWith(QLatin1String("/mnt/"));
    // btrfs subvols use `/@` / `/@home` as the tree root, not `/`.
    if (root != QLatin1String("/") && v.mountPoint != QLatin1String("/") &&
        v.mountPoint != QLatin1String("/home") && !media)
      continue;
    v.fstype = r.at(0);
    v.source = unescapeMountField(r.at(1));
    if (isIgnoredFs(v.fstype) || isIgnoredMount(v.mountPoint))
      continue;
    if (!media && v.mountPoint != QLatin1String("/") &&
        !looksLikeDiskFs(v.fstype))
      continue;
    if (v.fstype == QLatin1String("tmpfs"))
      continue;
    const QStringList dev = l.at(2).split(QLatin1Char(':'));
    int major = 0;
    int minor = 0;
    if (dev.size() == 2) {
      major = dev.at(0).toInt();
      minor = dev.at(1).toInt();
      v.device = blockDevice(major, minor);
      v.removable = sysfsRemovable(major, minor) || media;
    } else {
      v.removable = media;
    }
    if (v.device.isEmpty() && v.source.startsWith(QLatin1String("/dev/")))
      v.device = v.source;
    raw.append(v);
  }

  QString homeMount;
  QString homeCanon = QFileInfo(homePath).canonicalFilePath();
  if (homeCanon.isEmpty())
    homeCanon = QDir::cleanPath(homePath);
  int bestLen = -1;
  for (const Volume &v : raw) {
    if (homeCanon == v.mountPoint ||
        homeCanon.startsWith(v.mountPoint + QLatin1Char('/')) ||
        (v.mountPoint == QLatin1String("/") && bestLen < 0)) {
      if (int(v.mountPoint.size()) > bestLen) {
        bestLen = int(v.mountPoint.size());
        homeMount = v.mountPoint;
      }
    }
  }

  QVector<Volume> out;
  QStringList seen;
  for (Volume v : raw) {
    if (seen.contains(v.mountPoint))
      continue;
    seen.append(v.mountPoint);
    v.label = volumeLabel(v, homePath, homeMount);
    v.extra = v.removable || (v.mountPoint != QLatin1String("/") &&
                              v.mountPoint != homeMount);
    fillStat(&v);
    out.append(v);
  }

  bool hasRoot = false;
  for (const Volume &v : out) {
    if (v.mountPoint == QLatin1String("/")) {
      hasRoot = true;
      break;
    }
  }
  if (!hasRoot) {
    Volume root;
    root.mountPoint = QStringLiteral("/");
    root.label = QStringLiteral("/");
    fillStat(&root);
    out.prepend(root);
  }

  std::sort(out.begin(), out.end(), [](const Volume &a, const Volume &b) {
    auto rank = [](const Volume &v) {
      if (v.mountPoint == QLatin1String("/"))
        return 0;
      if (v.label == QLatin1String("home") && !v.extra)
        return 1;
      if (v.removable)
        return 2;
      return 3;
    };
    const int ra = rank(a);
    const int rb = rank(b);
    if (ra != rb)
      return ra < rb;
    return a.label.toLower() < b.label.toLower();
  });
  return out;
}

QVector<VolumeStore::Volume> VolumeStore::scanLive() const {
  QFile f(QStringLiteral("/proc/self/mountinfo"));
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QString text = QString::fromUtf8(f.readAll());
  return parseMountinfo(text, QDir::homePath());
}

void VolumeStore::refresh() {
  const QVector<Volume> next = m_scanHook ? m_scanHook() : scanLive();
  if (next == m_volumes)
    return;
  m_volumes = next;
  emit changed();
}

VolumeStore::Volume VolumeStore::containing(const QString &path) const {
  if (path.isEmpty() || path.startsWith(QLatin1String("volumes:")))
    return {};
  QString canon = QFileInfo(path).canonicalFilePath();
  if (canon.isEmpty())
    canon = QDir::cleanPath(path);
  Volume best;
  int bestLen = -1;
  for (const Volume &v : m_volumes) {
    if (canon == v.mountPoint ||
        canon.startsWith(v.mountPoint + QLatin1Char('/')) ||
        (v.mountPoint == QLatin1String("/") && bestLen < 0)) {
      if (int(v.mountPoint.size()) > bestLen) {
        best = v;
        bestLen = int(v.mountPoint.size());
      }
    }
  }
  return best;
}

VolumeStore::Volume VolumeStore::extraRoot(const QString &path) const {
  const Volume v = containing(path);
  if (v.extra)
    return v;
  return {};
}

VolumeStore::Volume VolumeStore::findMount(const QString &mountPoint) const {
  const QString want = QDir::cleanPath(mountPoint);
  for (const Volume &v : m_volumes) {
    if (v.mountPoint == want)
      return v;
  }
  return {};
}

QVariantList VolumeStore::extraChips() const {
  QVariantList out;
  for (const Volume &v : m_volumes) {
    if (!v.extra && v.mountPoint != QLatin1String("/"))
      continue;
    QVariantMap row;
    row.insert(QStringLiteral("id"),
               QStringLiteral("volume:") + v.mountPoint);
    row.insert(QStringLiteral("name"), v.label);
    row.insert(QStringLiteral("label"), chipLabel(v));
    row.insert(QStringLiteral("runtime"), QStringLiteral("path"));
    row.insert(QStringLiteral("path"), v.mountPoint);
    row.insert(QStringLiteral("removable"), v.removable);
    out.append(row);
  }
  return out;
}

bool VolumeStore::eject(const QString &target, QString *error) {
  Volume v = findMount(target);
  if (v.mountPoint.isEmpty())
    v = extraRoot(target);
  if (v.mountPoint.isEmpty()) {
    if (error)
      *error = QStringLiteral("not a volume");
    return false;
  }
  if (!v.extra) {
    if (error)
      *error = QStringLiteral("refusing to eject the system volume");
    return false;
  }
  if (m_ejectHook)
    return m_ejectHook(v, error);
  return ejectLive(v, error);
}

bool VolumeStore::ejectLive(const Volume &v, QString *error) {
  const QString disk = v.device.isEmpty() ? v.source : v.device;
  auto run = [](const QStringList &args, QString *err) {
    QProcess p;
    p.start(QStringLiteral("udisksctl"), args);
    if (!p.waitForFinished(15000)) {
      if (err)
        *err = QStringLiteral("udisksctl timed out");
      return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
      if (err)
        *err = QString::fromUtf8(p.readAllStandardError()).trimmed();
      return false;
    }
    return true;
  };
  if (QStandardPaths::findExecutable(QStringLiteral("udisksctl")).isEmpty()) {
    if (error)
      *error = QStringLiteral("udisksctl is not available");
    return false;
  }
  if (!disk.startsWith(QLatin1String("/dev/"))) {
    if (error)
      *error = QStringLiteral("no block device for %1").arg(v.mountPoint);
    return false;
  }
  QString err;
  if (!run({QStringLiteral("unmount"), QStringLiteral("-b"), disk}, &err)) {
    if (error)
      *error = err.isEmpty() ? QStringLiteral("unmount failed") : err;
    return false;
  }
  if (v.removable) {
    QString parent = disk;
    // power-off the disk, not the partition, when we can.
    while (!parent.isEmpty() && parent.back().isDigit())
      parent.chop(1);
    if (parent.endsWith(QLatin1Char('p')) && parent.contains(QLatin1String("nvme")))
      parent.chop(1);
    if (parent.size() < 7)
      parent = disk;
    run({QStringLiteral("power-off"), QStringLiteral("-b"), parent}, nullptr);
  }
  refresh();
  return true;
}


