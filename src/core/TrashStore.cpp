#include "TrashStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {

bool existsHere(const QString &path) {
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
}

void splitStemExt(const QString &name, QString *stem, QString *ext) {
  const int dot = name.lastIndexOf(QLatin1Char('.'));
  if (dot <= 0) {
    *stem = name;
    *ext = {};
    return;
  }
  *stem = name.left(dot);
  *ext = name.mid(dot);
}

QString clean(const QString &path) { return QDir::cleanPath(path); }

bool pathEquals(const QString &a, const QString &b) {
  if (clean(a) == clean(b))
    return true;
  const QString ca = QFileInfo(a).canonicalFilePath();
  const QString cb = QFileInfo(b).canonicalFilePath();
  return !ca.isEmpty() && ca == cb;
}

bool pathIsOrUnder(const QString &root, const QString &path) {
  const QString r = clean(root);
  const QString p = clean(path);
  if (r.isEmpty() || p.isEmpty())
    return false;
  if (p == r)
    return true;
  if (p.startsWith(r + QLatin1Char('/')))
    return true;
  const QString rc = QFileInfo(r).canonicalFilePath();
  const QString pc = QFileInfo(path).canonicalFilePath();
  if (rc.isEmpty() || pc.isEmpty())
    return false;
  return pc == rc || pc.startsWith(rc + QLatin1Char('/'));
}

bool hasDotDot(const QString &path) {
  const QStringList parts = clean(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  return parts.contains(QStringLiteral(".."));
}

QString fallbackRoot() {
  return clean(QDir(QDir::homePath()).filePath(QStringLiteral(".local/share/Trash")));
}

QString infoPathForName(const QString &name) {
  return QDir(TrashStore::infoDir())
      .filePath(name + QStringLiteral(".trashinfo"));
}

void applyStat(TrashStore::Item *item) {
  const QFileInfo info(item->trashFile);
  item->isSymlink = info.isSymLink();
  item->isDir = info.isDir();
  item->size = item->isDir ? -1 : info.size();
}

bool parseTrashInfo(const QString &infoFile, TrashStore::Item *out) {
  QFile f(infoFile);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return false;
  const QString text = QString::fromUtf8(f.readAll());
  f.close();

  bool header = false;
  QString path;
  QString date;
  const QStringList lines = text.split(QLatin1Char('\n'));
  for (QString line : lines) {
    if (line.endsWith(QLatin1Char('\r')))
      line.chop(1);
    if (line.trimmed().isEmpty())
      continue;
    if (line.trimmed() == QLatin1String("[Trash Info]")) {
      header = true;
      continue;
    }
    const int eq = line.indexOf(QLatin1Char('='));
    if (eq <= 0)
      continue;
    const QString key = line.left(eq).trimmed();
    const QString value = line.mid(eq + 1);
    // Spec: first Path= / DeletionDate= wins (Nautilus may rewrite).
    if (key == QLatin1String("Path") && path.isEmpty())
      path = TrashStore::decodeOrigPath(value.trimmed());
    else if (key == QLatin1String("DeletionDate") && date.isEmpty())
      date = value.trimmed();
  }
  if (!header || path.isEmpty())
    return false;

  out->infoFile = infoFile;
  out->origPath = path;
  out->deletedAt =
      QDateTime::fromString(date, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  if (!out->deletedAt.isValid())
    out->deletedAt = QDateTime::fromString(date, Qt::ISODate);
  return true;
}

} // namespace

QString TrashStore::dataHome() {
  const QByteArray env = qgetenv("XDG_DATA_HOME");
  if (!env.isEmpty()) {
    const QString p = QFile::decodeName(env);
    if (QDir::isAbsolutePath(p))
      return clean(p);
  }
  return clean(QDir(QDir::homePath()).filePath(QStringLiteral(".local/share")));
}

QString TrashStore::root() {
  return QDir(dataHome()).filePath(QStringLiteral("Trash"));
}

QString TrashStore::filesDir() {
  return QDir(root()).filePath(QStringLiteral("files"));
}

QString TrashStore::infoDir() {
  return QDir(root()).filePath(QStringLiteral("info"));
}

bool TrashStore::isTrashUrl(const QString &path) {
  const QString t = path.trimmed();
  return t == QLatin1String("trash:") || t == QLatin1String(kUrl) ||
         t.startsWith(QLatin1String("trash://"));
}

QString TrashStore::normalizeUrl(const QString &path) {
  if (isTrashUrl(path))
    return QString::fromUtf8(kUrl);
  return path;
}

bool TrashStore::mapsToTrashView(const QString &path) {
  if (path.isEmpty())
    return false;
  const auto underFiles = [&](const QString &trashRoot) {
    const QString files = clean(QDir(trashRoot).filePath(QStringLiteral("files")));
    const QString p = clean(path);
    if (p == files || p.startsWith(files + QLatin1Char('/')))
      return true;
    const QString fc = QFileInfo(files).canonicalFilePath();
    const QString pc = QFileInfo(path).canonicalFilePath();
    if (fc.isEmpty() || pc.isEmpty())
      return false;
    return pc == fc || pc.startsWith(fc + QLatin1Char('/'));
  };
  if (underFiles(root()))
    return true;
  const QString fallback = fallbackRoot();
  return clean(fallback) != clean(root()) && underFiles(fallback);
}

bool TrashStore::isProtectedTree(const QString &path) {
  if (path.isEmpty() || hasDotDot(path))
    return true;
  const QString cleaned = clean(path);
  const auto protect = [&](const QString &trashRoot) {
    const QString r = clean(trashRoot);
    if (cleaned == r || cleaned == r + QLatin1String("/files") ||
        cleaned == r + QLatin1String("/info"))
      return true;
    const QString rc = QFileInfo(r).canonicalFilePath();
    const QString pc = QFileInfo(path).canonicalFilePath();
    if (rc.isEmpty() || pc.isEmpty())
      return false;
    return pc == rc || pc == rc + QLatin1String("/files") ||
           pc == rc + QLatin1String("/info");
  };
  if (protect(root()))
    return true;
  const QString fallback = fallbackRoot();
  if (clean(fallback) != clean(root()) && protect(fallback))
    return true;
  return false;
}

bool TrashStore::isInsideTrash(const QString &path) {
  if (path.isEmpty())
    return false;
  if (pathIsOrUnder(root(), path))
    return true;
  const QString fallback = fallbackRoot();
  if (clean(fallback) != clean(root()) && pathIsOrUnder(fallback, path))
    return true;
  return false;
}

bool TrashStore::isTrashItem(const QString &path) {
  if (path.isEmpty() || hasDotDot(path))
    return false;
  const QString files = clean(filesDir());
  const QString cleaned = clean(path);
  if (cleaned == files || !cleaned.startsWith(files + QLatin1Char('/')))
    return false;
  const QString rel = cleaned.mid(files.size() + 1);
  return !rel.isEmpty() && !rel.contains(QLatin1Char('/'));
}

bool TrashStore::canTrash(const QString &path, QString *err) {
  auto fail = [&](const QString &e) {
    if (err)
      *err = e;
    return false;
  };
  if (path.isEmpty() || clean(path) == QLatin1String("/") || hasDotDot(path))
    return fail(QStringLiteral("refusing operation on /"));
  const QString home = clean(QDir::homePath());
  if (pathEquals(path, home))
    return fail(QStringLiteral("refusing to trash home"));
  if (isProtectedTree(path) || isInsideTrash(path))
    return fail(QStringLiteral("refusing to trash the trash"));
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink())
    return fail(QStringLiteral("missing: %1").arg(path));
  return true;
}

bool TrashStore::ensureDirs(QString *err) {
  if (!QDir().mkpath(filesDir()) || !QDir().mkpath(infoDir())) {
    if (err)
      *err = QStringLiteral("cannot create trash directories");
    return false;
  }
#ifdef Q_OS_UNIX
  // Spec SHOULD: trash dirs are 0700 so a 755 $HOME cannot leak deletions.
  const auto mode0700 = [](const QString &p) {
    return ::chmod(QFile::encodeName(p).constData(), S_IRWXU) == 0;
  };
  if (!mode0700(root()) || !mode0700(filesDir()) || !mode0700(infoDir())) {
    if (err)
      *err = QStringLiteral("cannot restrict trash permissions");
    return false;
  }
#endif
  return true;
}

QString TrashStore::encodeOrigPath(const QString &path) {
  return QString::fromUtf8(QUrl::toPercentEncoding(path, "/"));
}

QString TrashStore::decodeOrigPath(const QString &encoded) {
  QString path = QUrl::fromPercentEncoding(encoded.toUtf8());
  if (path.startsWith(QLatin1String("file://")))
    path = QUrl(path).toLocalFile();
  if (path.isEmpty())
    return {};
  if (!QDir::isAbsolutePath(path)) {
    // Home-trash relative Path= is relative to the parent of Trash/.
    if (hasDotDot(path))
      return {};
    path = QDir::cleanPath(QDir(dataHome()).filePath(path));
  }
  if (hasDotDot(path))
    return {};
  return path;
}

bool TrashStore::prepareTrash(const QString &src, QString *trashFile,
                              QString *err) {
  auto fail = [&](const QString &e) {
    if (err)
      *err = e;
    return false;
  };
  QString why;
  if (!canTrash(src, &why))
    return fail(why);
  if (!ensureDirs(&why))
    return fail(why);

  const QString base = QFileInfo(src).fileName();
  if (base.isEmpty() || base == QLatin1String(".") ||
      base == QLatin1String(".."))
    return fail(QStringLiteral("invalid name"));

  const QString orig = QDir::cleanPath(QFileInfo(src).absoluteFilePath());
  const QString date =
      QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  const QByteArray body =
      QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n")
          .arg(encodeOrigPath(orig), date)
          .toUtf8();

  auto taken = [&](const QString &name) {
    return existsHere(QDir(filesDir()).filePath(name)) ||
           existsHere(infoPathForName(name));
  };

  QString stem;
  QString ext;
  splitStemExt(base, &stem, &ext);

  for (int n = 0; n < 10000; ++n) {
    const QString name =
        n == 0 ? base : stem + QLatin1Char('.') + QString::number(n + 1) + ext;
    if (n > 0 && taken(name))
      continue;
    if (n == 0 && taken(name))
      continue;
    const QString infoPath = infoPathForName(name);
    QFile info(infoPath);
    if (!info.open(QIODevice::WriteOnly | QIODevice::NewOnly))
      continue;
    if (info.write(body) != body.size()) {
      info.close();
      info.remove();
      return fail(QStringLiteral("cannot write trashinfo"));
    }
    info.close();
    if (trashFile)
      *trashFile = QDir(filesDir()).filePath(name);
    return true;
  }
  return fail(QStringLiteral("cannot find unique trash name"));
}

void TrashStore::abandonPrepared(const QString &trashFile) {
  removeInfoFor(trashFile);
}

bool TrashStore::itemForName(const QString &name, Item *out) {
  if (!out || name.isEmpty() || name.contains(QLatin1Char('/')))
    return false;
  Item item;
  item.name = name;
  item.trashFile = QDir(filesDir()).filePath(name);
  item.infoFile = infoPathForName(name);
  if (!existsHere(item.trashFile))
    return false;
  parseTrashInfo(item.infoFile, &item);
  applyStat(&item);
  *out = item;
  return true;
}

bool TrashStore::itemForTrashFile(const QString &trashFile, Item *out) {
  if (!isTrashItem(trashFile))
    return false;
  return itemForName(QFileInfo(trashFile).fileName(), out);
}

QVector<TrashStore::Item> TrashStore::list() {
  QVector<Item> out;
  QDir dir(filesDir());
  if (!dir.exists())
    return out;
  const QStringList names = dir.entryList(
      QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
  out.reserve(names.size());
  for (const QString &name : names) {
    Item item;
    if (itemForName(name, &item))
      out.append(item);
  }
  return out;
}

void TrashStore::removeInfoFor(const QString &trashFile) {
  const QString name = QFileInfo(clean(trashFile)).fileName();
  if (name.isEmpty())
    return;
  QFile::remove(infoPathForName(name));
}

void TrashStore::purgeOrphanInfos() {
  QDir dir(infoDir());
  if (!dir.exists())
    return;
  const QStringList names = dir.entryList({QStringLiteral("*.trashinfo")},
                                          QDir::Files | QDir::Hidden);
  for (const QString &name : names) {
    QString base = name;
    if (base.endsWith(QLatin1String(".trashinfo")))
      base.chop(10);
    if (!existsHere(QDir(filesDir()).filePath(base)))
      QFile::remove(dir.filePath(name));
  }
}

QString TrashStore::uniqueDest(const QString &dir, const QString &name) {
  if (dir.isEmpty() || name.isEmpty())
    return {};
  const QString first = QDir(dir).filePath(name);
  if (!existsHere(first))
    return first;
  QString stem;
  QString ext;
  splitStemExt(name, &stem, &ext);
  for (int n = 1; n < 10000; ++n) {
    const QString cand =
        QDir(dir).filePath(stem + QStringLiteral(" (%1)").arg(n) + ext);
    if (!existsHere(cand))
      return cand;
  }
  return {};
}

namespace {

bool removeTree(const QString &path, QString *err) {
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink())
    return true;
  if (info.isDir() && !info.isSymLink()) {
    QDir dir(path);
    if (!dir.removeRecursively()) {
      if (err)
        *err = QStringLiteral("cannot remove %1").arg(path);
      return false;
    }
    return true;
  }
  if (!QFile::remove(path)) {
    if (err)
      *err = QStringLiteral("cannot remove %1").arg(path);
    return false;
  }
  return true;
}

bool movePath(const QString &src, const QString &dest, QString *err) {
  if (QFile::rename(src, dest))
    return true;
  const QFileInfo info(src);
  if (info.isDir() && !info.isSymLink()) {
    if (!QFile::copy(src, dest)) {
      // QFile::copy does not recurse; walk.
      if (!QDir().mkpath(dest)) {
        if (err)
          *err = QStringLiteral("cannot create %1").arg(dest);
        return false;
      }
      const QFileInfoList kids =
          QDir(src).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries |
                                  QDir::Hidden | QDir::System);
      for (const QFileInfo &kid : kids) {
        if (!movePath(kid.absoluteFilePath(),
                      QDir(dest).filePath(kid.fileName()), err))
          return false;
      }
      if (!QDir(src).removeRecursively()) {
        if (err)
          *err = QStringLiteral("cannot remove %1").arg(src);
        return false;
      }
      return true;
    }
  }
  if (QFile::copy(src, dest) && QFile::remove(src))
    return true;
  if (err)
    *err = QStringLiteral("cannot restore %1").arg(src);
  return false;
}

} // namespace

bool TrashStore::restore(const QString &trashFile, QString *restoredPath,
                         QString *err) {
  auto fail = [&](const QString &e) {
    if (err)
      *err = e;
    return false;
  };
  Item item;
  if (!itemForTrashFile(trashFile, &item))
    return fail(QStringLiteral("not a trash item"));
  if (item.origPath.isEmpty())
    return fail(QStringLiteral("missing original path"));
  const QString parent = QFileInfo(item.origPath).absolutePath();
  const QString base = QFileInfo(item.origPath).fileName();
  if (!QDir().mkpath(parent))
    return fail(QStringLiteral("cannot create %1").arg(parent));
  QString dest = item.origPath;
  if (existsHere(dest) && clean(dest) != clean(item.trashFile)) {
    dest = uniqueDest(parent, base);
    if (dest.isEmpty())
      return fail(QStringLiteral("cannot find unique name"));
  }
  QString why;
  if (!movePath(item.trashFile, dest, &why))
    return fail(why);
  removeInfoFor(item.trashFile);
  if (restoredPath)
    *restoredPath = dest;
  return true;
}

bool TrashStore::empty(QString *err) {
  const QVector<Item> items = list();
  for (const Item &item : items) {
    if (!isTrashItem(item.trashFile)) {
      if (err)
        *err = QStringLiteral("not a trash item");
      return false;
    }
    QString why;
    if (!removeTree(item.trashFile, &why)) {
      if (err)
        *err = why;
      return false;
    }
    removeInfoFor(item.trashFile);
  }
  purgeOrphanInfos();
  return true;
}
