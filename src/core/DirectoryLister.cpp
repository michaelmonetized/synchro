#include "DirectoryLister.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>

#include <cerrno>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10
#endif

namespace {

constexpr int kFirstBatch = 256;
constexpr int kStatBatch = 256;

void ensureMetaTypes() {
  static const int kEntry = qRegisterMetaType<DirectoryEntry>();
  static const int kVec = qRegisterMetaType<QVector<DirectoryEntry>>();
  Q_UNUSED(kEntry);
  Q_UNUSED(kVec);
}

qint64 mtimeMs(const struct stat &st) {
  return static_cast<qint64>(st.st_mtim.tv_sec) * 1000 +
         static_cast<qint64>(st.st_mtim.tv_nsec) / 1000000;
}

bool isDotName(const char *name) {
  return name[0] == '.' &&
         (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

struct RawEntry {
  QByteArray rawName;
  unsigned char dType = DT_UNKNOWN;
};

bool isPendingType(unsigned char t) { return t == DT_LNK || t == DT_UNKNOWN; }

DirectoryEntry fromDirent(const QString &dirPath, const RawEntry &raw) {
  DirectoryEntry e;
  e.name = QFile::decodeName(raw.rawName);
  e.path = QDir(dirPath).filePath(e.name);
  e.uri = QUrl::fromLocalFile(e.path);
  e.isHidden = !e.name.isEmpty() && e.name[0] == QLatin1Char('.');
  e.size = -1;
  e.mtime = 0;

  const unsigned char t = raw.dType;
  if (t == DT_DIR) {
    e.isDir = true;
    e.dirKind = QStringLiteral("posix");
    e.iconName = QStringLiteral("folder");
    e.mime = QStringLiteral("inode/directory");
  } else if (t == DT_REG) {
    e.dirKind = QStringLiteral("posix");
    e.iconName = QStringLiteral("text-x-generic");
  } else if (t == DT_LNK) {
    e.isSymlink = true;
    e.dirKind = QStringLiteral("pending");
    e.iconName = QStringLiteral("emblem-symbolic-link");
  } else if (t == DT_UNKNOWN) {
    e.dirKind = QStringLiteral("pending");
    e.iconName = QStringLiteral("text-x-generic");
  } else {
    e.dirKind = QStringLiteral("posix");
    e.iconName = QStringLiteral("text-x-generic");
  }
  return e;
}

void applyMime(DirectoryEntry &e, QMimeDatabase &db) {
  if (e.isDir) {
    e.mime = QStringLiteral("inode/directory");
    e.iconName = QStringLiteral("folder");
    return;
  }
  QMimeType mime = db.mimeTypeForFile(e.path, QMimeDatabase::MatchExtension);
  const QFileInfo fi(e.name);
  if (fi.suffix().isEmpty())
    mime = db.mimeTypeForFile(e.path, QMimeDatabase::MatchContent);
  e.mime = mime.name();
  e.iconName = mime.genericIconName();
  if (e.iconName.isEmpty())
    e.iconName = mime.iconName();
  if (e.iconName.isEmpty())
    e.iconName = e.isSymlink ? QStringLiteral("emblem-symbolic-link")
                             : QStringLiteral("text-x-generic");
}

DirectoryEntry enrich(int dirfd, const QString &dirPath, const RawEntry &raw,
                      QMimeDatabase &db) {
  DirectoryEntry e = fromDirent(dirPath, raw);

  struct stat lst{};
  if (fstatat(dirfd, raw.rawName.constData(), &lst, AT_SYMLINK_NOFOLLOW) != 0) {
    e.dirKind = QStringLiteral("posix");
    return e;
  }

  e.isSymlink = S_ISLNK(lst.st_mode);
  e.mtime = mtimeMs(lst);
  e.size = static_cast<qint64>(lst.st_size);

  struct stat st = lst;
  if (e.isSymlink) {
    if (fstatat(dirfd, raw.rawName.constData(), &st, 0) != 0) {
      e.isDir = false;
      e.dirKind = QStringLiteral("posix");
      e.iconName = QStringLiteral("emblem-symbolic-link");
      return e;
    }
  }

  e.isDir = S_ISDIR(st.st_mode);
  e.mtime = mtimeMs(st);
  e.perm = static_cast<int>(lst.st_mode & 07777);
  if (e.isDir)
    e.size = -1;
  else
    e.size = static_cast<qint64>(st.st_size);
  e.dirKind = QStringLiteral("posix");
  applyMime(e, db);
  return e;
}

} // namespace

DirectoryLister::DirectoryLister(QObject *parent) : QObject(parent) {
  ensureMetaTypes();
}

void DirectoryLister::abandon(quint64 generation) {
  m_wanted.store(generation, std::memory_order_release);
}

bool DirectoryLister::abandoned(quint64 generation) const {
  return m_wanted.load(std::memory_order_acquire) != generation;
}

void DirectoryLister::requestList(quint64 generation, const QString &path) {
  quint64 wanted = m_wanted.load(std::memory_order_acquire);
  while (true) {
    if (wanted != 0 && wanted > generation)
      return;
    if (wanted == generation)
      break;
    // wanted == 0 (test path) or older than this gen: adopt.
    if (m_wanted.compare_exchange_weak(wanted, generation,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire))
      break;
  }
  listPath(generation, path);
}

void DirectoryLister::listPath(quint64 generation, const QString &path) {
  if (abandoned(generation))
    return;

  const QByteArray encoded = QFile::encodeName(path);
  const int fd =
      ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    emit finished(generation, false,
                  QString::fromLocal8Bit(std::strerror(errno)));
    return;
  }

  DIR *dir = fdopendir(fd);
  if (!dir) {
    const int err = errno;
    ::close(fd);
    emit finished(generation, false,
                  QString::fromLocal8Bit(std::strerror(err)));
    return;
  }

  const int dirfd = ::dirfd(dir);
  QVector<RawEntry> pending;
  QVector<RawEntry> rest;
  QVector<DirectoryEntry> batch;
  batch.reserve(kFirstBatch);
  pending.reserve(16);
  rest.reserve(256);

  bool emittedAny = false;
  while (true) {
    // readdir leaves errno unchanged on EOF; Qt work in the body must not
    // be mistaken for a read failure.
    errno = 0;
    struct dirent *ent = readdir(dir);
    if (!ent) {
      const int readErr = errno;
      if (readErr != 0) {
        if (!batch.isEmpty() || !emittedAny)
          emit batchReady(generation, batch);
        closedir(dir);
        emit finished(generation, false,
                      QString::fromLocal8Bit(std::strerror(readErr)));
        return;
      }
      break;
    }
    if (abandoned(generation)) {
      closedir(dir);
      return;
    }
    if (isDotName(ent->d_name))
      continue;

    RawEntry raw;
    raw.rawName = QByteArray(ent->d_name);
    raw.dType = ent->d_type;
    if (isPendingType(raw.dType))
      pending.append(raw);
    else
      rest.append(raw);

    batch.append(fromDirent(path, raw));
    if (batch.size() >= kFirstBatch) {
      emit batchReady(generation, batch);
      batch.clear();
      emittedAny = true;
    }
  }

  if (!batch.isEmpty() || !emittedAny)
    emit batchReady(generation, batch);

  if (abandoned(generation)) {
    closedir(dir);
    return;
  }

  QMimeDatabase db;
  auto flushStats = [&](QVector<DirectoryEntry> &out, bool priority) {
    if (out.isEmpty())
      return;
    emit statsReady(generation, out, priority);
    out.clear();
  };

  QVector<DirectoryEntry> stats;
  stats.reserve(kStatBatch);
  for (const RawEntry &raw : pending) {
    if (abandoned(generation)) {
      closedir(dir);
      return;
    }
    stats.append(enrich(dirfd, path, raw, db));
    if (stats.size() >= kStatBatch)
      flushStats(stats, true);
  }
  flushStats(stats, true);

  for (const RawEntry &raw : rest) {
    if (abandoned(generation)) {
      closedir(dir);
      return;
    }
    stats.append(enrich(dirfd, path, raw, db));
    if (stats.size() >= kStatBatch)
      flushStats(stats, false);
  }
  flushStats(stats, false);

  closedir(dir);
  if (!abandoned(generation))
    emit finished(generation, true, QString());
}

void DirectoryLister::requestStatNames(quint64 generation,
                                       const QString &dirPath,
                                       const QStringList &names) {
  if (abandoned(generation) || names.isEmpty())
    return;

  const QByteArray encoded = QFile::encodeName(dirPath);
  const int fd =
      ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    return;

  QMimeDatabase db;
  QVector<DirectoryEntry> stats;
  stats.reserve(names.size());
  for (const QString &name : names) {
    if (abandoned(generation)) {
      ::close(fd);
      return;
    }
    RawEntry raw;
    raw.rawName = QFile::encodeName(name);
    raw.dType = DT_UNKNOWN;
    stats.append(enrich(fd, dirPath, raw, db));
  }
  ::close(fd);
  if (!abandoned(generation) && !stats.isEmpty())
    emit statsReady(generation, stats, true);
}
