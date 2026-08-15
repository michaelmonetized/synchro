#include "CoreVerbs.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString g_trashRootOverride;

QString encodeTrashPath(const QString &path) {
  return QString::fromUtf8(QUrl::toPercentEncoding(path, "/"));
}

} // namespace

QString CoreVerbs::defaultTrashRoot() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericDataLocation))
      .filePath(QStringLiteral("Trash"));
}

void CoreVerbs::setTrashRootOverride(const QString &root) {
  g_trashRootOverride = root;
}

QString CoreVerbs::trashRoot() {
  return g_trashRootOverride.isEmpty() ? defaultTrashRoot()
                                       : g_trashRootOverride;
}

bool CoreVerbs::isForbiddenTrashPath(const QString &path) {
  const QString n = QDir::cleanPath(path);
  if (n.isEmpty() || n == QLatin1String("/"))
    return true;
  if (n == QDir::homePath())
    return true;
  const QString trash = QDir::cleanPath(trashRoot());
  if (n == trash || n.startsWith(trash + QLatin1Char('/')))
    return true;
  return false;
}

QString CoreVerbs::uniqueName(const QString &filesDir, const QString &infoDir,
                              const QString &name) {
  auto taken = [&](const QString &cand) {
    return QFileInfo::exists(QDir(filesDir).filePath(cand)) ||
           QFileInfo::exists(QDir(infoDir).filePath(cand +
                                                    QStringLiteral(".trashinfo")));
  };
  if (!taken(name))
    return name;
  for (int i = 2; i < 10000; ++i) {
    const QString cand = name + QLatin1Char('.') + QString::number(i);
    if (!taken(cand))
      return cand;
  }
  return {};
}

bool CoreVerbs::writeInfo(const QString &infoPath, const QString &origPath,
                          QString *error) {
  QFile f(infoPath);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error)
      *error = QStringLiteral("cannot write %1").arg(infoPath);
    return false;
  }
  const QString date =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  const QByteArray body =
      "[Trash Info]\nPath=" + encodeTrashPath(origPath).toUtf8() +
      "\nDeletionDate=" + date.toUtf8() + "\n";
  if (f.write(body) != body.size()) {
    if (error)
      *error = QStringLiteral("short write %1").arg(infoPath);
    return false;
  }
  return true;
}

bool CoreVerbs::trash(const QStringList &paths, QString *error) {
  if (paths.isEmpty()) {
    if (error)
      *error = QStringLiteral("nothing to trash");
    return false;
  }
  const QString root = trashRoot();
  const QString filesDir = QDir(root).filePath(QStringLiteral("files"));
  const QString infoDir = QDir(root).filePath(QStringLiteral("info"));
  if (!QDir().mkpath(filesDir) || !QDir().mkpath(infoDir)) {
    if (error)
      *error = QStringLiteral("cannot create trash directories");
    return false;
  }

  for (const QString &raw : paths) {
    const QFileInfo fi(raw);
    const QString path = fi.absoluteFilePath();
    if (!fi.exists()) {
      if (error)
        *error = QStringLiteral("not found: %1").arg(path);
      return false;
    }
    if (isForbiddenTrashPath(path)) {
      if (error)
        *error = QStringLiteral("refusing to trash %1").arg(path);
      return false;
    }
    const QString destName = uniqueName(filesDir, infoDir, fi.fileName());
    if (destName.isEmpty()) {
      if (error)
        *error = QStringLiteral("trash name collision");
      return false;
    }
    const QString dest = QDir(filesDir).filePath(destName);
    const QString infoPath =
        QDir(infoDir).filePath(destName + QStringLiteral(".trashinfo"));
    if (!writeInfo(infoPath, path, error))
      return false;
    if (!QFile::rename(path, dest)) {
      QFile::remove(infoPath);
      if (error)
        *error = QStringLiteral("could not move %1 to trash").arg(path);
      return false;
    }
  }
  return true;
}
