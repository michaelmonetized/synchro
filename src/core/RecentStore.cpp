#include "RecentStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace {

// Compact replaces the JSONL inode (QSaveFile rename). flock on the
// data file would not serialize writers across that replace, so the
// lock lives on a sidecar.
class FileLock {
public:
  FileLock(const QString &lockPath, int op) {
    const QByteArray enc = QFile::encodeName(lockPath);
    m_fd = ::open(enc.constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (m_fd < 0)
      return;
    if (::flock(m_fd, op) != 0) {
      ::close(m_fd);
      m_fd = -1;
    }
  }
  ~FileLock() {
    if (m_fd >= 0) {
      ::flock(m_fd, LOCK_UN);
      ::close(m_fd);
    }
  }
  bool ok() const { return m_fd >= 0; }
  FileLock(const FileLock &) = delete;
  FileLock &operator=(const FileLock &) = delete;

private:
  int m_fd = -1;
};

QVector<RecentStore::Entry> parseEntries(const QString &path) {
  QVector<RecentStore::Entry> out;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return out;
  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty())
      continue;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject())
      continue;
    const QJsonObject obj = doc.object();
    RecentStore::Entry e;
    e.ts = obj.value(QStringLiteral("ts")).toString();
    e.path = obj.value(QStringLiteral("path")).toString();
    e.mime = obj.value(QStringLiteral("mime")).toString();
    if (!e.path.isEmpty())
      out.append(e);
  }
  return out;
}

} // namespace

RecentStore::RecentStore(QObject *parent)
    : RecentStore(QString(), 500, 1000, parent) {}

RecentStore::RecentStore(const QString &filePath, int keep, int compactAt,
                         QObject *parent)
    : QObject(parent),
      m_path(filePath.isEmpty() ? defaultPath() : filePath),
      m_keep(keep > 0 ? keep : 500),
      m_compactAt(compactAt > m_keep ? compactAt : m_keep + 1) {}

QString RecentStore::defaultPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericDataLocation))
      .filePath(QStringLiteral("synchro/recent.jsonl"));
}

QString RecentStore::lockPath() const {
  return m_path + QStringLiteral(".lock");
}

void RecentStore::record(const QString &path, const QString &mime) {
  if (path.isEmpty())
    return;

  const QString dir = QFileInfo(m_path).absolutePath();
  if (!QDir().mkpath(dir))
    return;

  FileLock lock(lockPath(), LOCK_EX);
  if (!lock.ok())
    return;

  QFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    return;

  QJsonObject obj;
  const QDateTime now = QDateTime::currentDateTime();
  obj.insert(QStringLiteral("ts"),
             now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate));
  obj.insert(QStringLiteral("path"), path);
  obj.insert(QStringLiteral("mime"), mime);
  obj.insert(QStringLiteral("ws"), QJsonValue::Null);
  file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.close();

  m_lines = parseEntries(m_path).size();
  m_counted = true;
  if (m_lines >= m_compactAt)
    compact();
}

QVector<RecentStore::Entry> RecentStore::entries() const {
  FileLock lock(lockPath(), LOCK_SH);
  if (!lock.ok())
    return {};
  return parseEntries(m_path);
}

void RecentStore::compact() {
  // Caller holds LOCK_EX. Re-read so a sibling window's append is kept.
  const QVector<Entry> all = parseEntries(m_path);
  QVector<Entry> kept;
  QSet<QString> seen;
  kept.reserve(qMin(all.size(), m_keep));
  for (int i = all.size() - 1; i >= 0; --i) {
    const Entry &e = all.at(i);
    if (seen.contains(e.path))
      continue;
    seen.insert(e.path);
    kept.prepend(e);
    if (kept.size() >= m_keep)
      break;
  }

  QSaveFile out(m_path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  for (const Entry &e : kept) {
    QJsonObject obj;
    obj.insert(QStringLiteral("ts"), e.ts);
    obj.insert(QStringLiteral("path"), e.path);
    obj.insert(QStringLiteral("mime"), e.mime);
    obj.insert(QStringLiteral("ws"), QJsonValue::Null);
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    out.write("\n");
  }
  if (!out.commit())
    return;
  m_lines = kept.size();
  m_counted = true;
}
