#include "FileOpEngine.h"

#include "DirectoryModel.h"
#include "SelectionModel.h"
#include "TrashStore.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QUrl>
#include <QtConcurrent>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

void splitStemExt(const QString &name, QString *stem, QString *ext) {
  const int dot = name.lastIndexOf(QLatin1Char('.'));
  // ".gitignore" has no extension; "foo.md" does.
  if (dot <= 0) {
    *stem = name;
    *ext = {};
    return;
  }
  *stem = name.left(dot);
  *ext = name.mid(dot);
}

bool sameFile(const QString &a, const QString &b) {
  const QString ca = QFileInfo(a).canonicalFilePath();
  const QString cb = QFileInfo(b).canonicalFilePath();
  if (!ca.isEmpty() && !cb.isEmpty())
    return ca == cb;
  return QDir::cleanPath(a) == QDir::cleanPath(b);
}

bool existsHere(const QString &path) {
  const QFileInfo info(path);
  return info.exists() || info.isSymLink();
}

} // namespace

FileOpEngine::FileOpEngine(QObject *parent) : QObject(parent), m_undo(this) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this,
          &FileOpEngine::onFinished);
}

FileOpEngine::~FileOpEngine() {
  m_cancel.store(true);
  m_watcher.disconnect();
  if (m_inFlight.isRunning())
    m_inFlight.waitForFinished();
}

void FileOpEngine::setSelection(SelectionModel *sel) { m_sel = sel; }

void FileOpEngine::setDirectoryModel(DirectoryModel *model) {
  if (m_model == model)
    return;
  if (m_model)
    disconnect(m_model, nullptr, this, nullptr);
  m_model = model;
  if (m_model) {
    connect(m_model, &DirectoryModel::restoreRequested, this,
            [this](const QStringList &files) { restorePaths(files, true); });
  }
}

bool FileOpEngine::inTrash() const { return m_model && m_model->isTrash(); }

QString FileOpEngine::clipboardMode() const {
  switch (m_clipMode) {
  case ClipMode::Copy:
    return QStringLiteral("copy");
  case ClipMode::Cut:
    return QStringLiteral("cut");
  case ClipMode::None:
    break;
  }
  return {};
}

int FileOpEngine::clipboardCount() const { return m_clipPaths.size(); }

QStringList FileOpEngine::clipboardPaths() const { return m_clipPaths; }

bool FileOpEngine::isForbiddenPath(const QString &path) {
  if (path.isEmpty())
    return true;
  const QString n = QDir::cleanPath(path);
  if (n == QLatin1String("/"))
    return true;
  // Qt does not collapse "/.." to "/"; leftover ".." is an escape.
  const QStringList parts = n.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (parts.contains(QLatin1String("..")))
    return true;
  return false;
}

bool FileOpEngine::isSameOrDescendant(const QString &root, const QString &path) {
  const QString r = QDir::cleanPath(root);
  const QString p = QDir::cleanPath(path);
  if (r.isEmpty() || p.isEmpty())
    return false;
  if (p == r)
    return true;
  return p.startsWith(r + QLatin1Char('/'));
}

bool FileOpEngine::isProtectedUnlink(const QString &path) {
  if (isForbiddenPath(path))
    return true;
  const QString cleaned = QDir::cleanPath(path);
  const QString home = QDir::cleanPath(QDir::homePath());
  if (cleaned == home)
    return true;
  const QString homeCanon = QFileInfo(home).canonicalFilePath();
  const QString pathCanon = QFileInfo(path).canonicalFilePath();
  if (!homeCanon.isEmpty() && pathCanon == homeCanon)
    return true;
  if (TrashStore::isProtectedTree(path))
    return true;
  return false;
}

QString FileOpEngine::collisionName(const QString &dir, const QString &name) {
  if (name.isEmpty())
    return name;
  const QString first = QDir(dir).filePath(name);
  if (!existsHere(first))
    return name;
  QString stem;
  QString ext;
  splitStemExt(name, &stem, &ext);
  for (int n = 1; n < 10000; ++n) {
    const QString cand = stem + QStringLiteral(" (%1)").arg(n) + ext;
    if (!existsHere(QDir(dir).filePath(cand)))
      return cand;
  }
  return {};
}

QStringList FileOpEngine::selectionPaths() const {
  if (!m_sel)
    return {};
  return m_sel->selectedPaths();
}

QStringList FileOpEngine::effectiveSelectionPaths() const {
  QStringList paths = selectionPaths();
  if (paths.isEmpty() && m_sel) {
    const QString cursor = m_sel->cursorPath();
    if (!cursor.isEmpty())
      paths.append(cursor);
  }
  return paths;
}

void FileOpEngine::setClipboard(const QStringList &paths, ClipMode mode) {
  m_clipPaths = paths;
  m_clipMode = paths.isEmpty() ? ClipMode::None : mode;
  publishClipboard();
  emit clipboardChanged();
}

void FileOpEngine::clearClipboard() { setClipboard({}, ClipMode::None); }

void FileOpEngine::publishClipboard() {
  QClipboard *cb = QGuiApplication::clipboard();
  if (!cb)
    return;
  if (m_clipPaths.isEmpty())
    return;
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  urls.reserve(m_clipPaths.size());
  QString list;
  for (const QString &p : m_clipPaths) {
    const QUrl url = QUrl::fromLocalFile(p);
    urls.append(url);
    list += url.toString(QUrl::FullyEncoded);
    list += QLatin1String("\r\n");
  }
  mime->setUrls(urls);
  mime->setData(QStringLiteral("text/uri-list"), list.toUtf8());
  cb->setMimeData(mime);
}

void FileOpEngine::copySelection() {
  const QStringList paths = selectionPaths();
  if (paths.isEmpty())
    return;
  setClipboard(paths, ClipMode::Copy);
  setMessage(QStringLiteral("copied %1").arg(paths.size()));
}

void FileOpEngine::cutSelection() {
  if (inTrash()) {
    setError(QStringLiteral("cannot cut from trash"));
    return;
  }
  const QStringList paths = selectionPaths();
  if (paths.isEmpty())
    return;
  setClipboard(paths, ClipMode::Cut);
  setMessage(QStringLiteral("cut %1").arg(paths.size()));
}

void FileOpEngine::paste() {
  const QString dest = m_model ? m_model->path() : QString();
  if (m_clipPaths.isEmpty()) {
    setError(QStringLiteral("clipboard empty"));
    return;
  }
  if (m_clipMode == ClipMode::Cut)
    movePaths(m_clipPaths, dest);
  else
    copyPaths(m_clipPaths, dest);
}

void FileOpEngine::renameCursor(const QString &newName) {
  if (inTrash()) {
    setError(QStringLiteral("cannot rename here"));
    return;
  }
  QString src;
  if (m_sel)
    src = m_sel->cursorPath();
  if (src.isEmpty() && m_model) {
    const int row = m_model->currentIndex();
    src = m_model->data(m_model->index(row, 0), DirectoryModel::PathRole)
              .toString();
  }
  renamePath(src, newName);
}

void FileOpEngine::mkdirHere(const QString &name) {
  const QString parent = m_model ? m_model->path() : QString();
  makeDir(parent, name);
}

void FileOpEngine::copyPaths(const QStringList &srcs, const QString &destDir) {
  QString err;
  if (!checkDestDir(destDir, &err) || !checkSources(srcs, false, &err)) {
    setError(err);
    return;
  }
  Request req;
  req.verb = Verb::Copy;
  req.sources = srcs;
  req.destDir = destDir;
  enqueue(req);
}

void FileOpEngine::movePaths(const QStringList &srcs, const QString &destDir) {
  QString err;
  if (!checkDestDir(destDir, &err) || !checkSources(srcs, true, &err)) {
    setError(err);
    return;
  }
  Request req;
  req.verb = Verb::Move;
  req.sources = srcs;
  req.destDir = destDir;
  enqueue(req);
}

void FileOpEngine::renamePath(const QString &src, const QString &newName) {
  QString err;
  if (!validBaseName(newName, &err)) {
    setError(err);
    return;
  }
  if (src.isEmpty() || isForbiddenPath(src) || isProtectedUnlink(src)) {
    setError(QStringLiteral("refusing rename"));
    return;
  }
  const QString parent = QFileInfo(src).absolutePath();
  if (!checkDestDir(parent, &err)) {
    setError(err);
    return;
  }
  Request req;
  req.verb = Verb::Rename;
  req.sources = {src};
  req.destDir = parent;
  req.destName = newName;
  enqueue(req);
}

void FileOpEngine::makeDir(const QString &parent, const QString &name) {
  QString err;
  if (!validBaseName(name, &err) || !checkDestDir(parent, &err)) {
    setError(err);
    return;
  }
  Request req;
  req.verb = Verb::Mkdir;
  req.destDir = parent;
  req.destName = name;
  enqueue(req);
}

void FileOpEngine::trashSelection() {
  if (inTrash())
    return;
  trashPaths(effectiveSelectionPaths());
}

void FileOpEngine::restoreSelection() {
  restorePaths(effectiveSelectionPaths(), true);
}

void FileOpEngine::unlinkSelection() { unlinkPaths(effectiveSelectionPaths()); }

void FileOpEngine::emptyTrash() {
  Request req;
  req.verb = Verb::EmptyTrash;
  enqueue(req);
}

void FileOpEngine::revealCursor() {
  if (!m_model)
    return;
  const QString orig = m_model->currentOrigPath();
  if (orig.isEmpty())
    return;
  m_model->setPath(QFileInfo(orig).absolutePath(), QFileInfo(orig).fileName());
}

void FileOpEngine::trashPaths(const QStringList &srcs) {
  QString err;
  if (srcs.isEmpty()) {
    setError(QStringLiteral("nothing selected"));
    return;
  }
  for (const QString &src : srcs) {
    if (!TrashStore::canTrash(src, &err)) {
      setError(err);
      return;
    }
  }
  Request req;
  req.verb = Verb::Trash;
  req.sources = srcs;
  enqueue(req);
}

void FileOpEngine::restorePaths(const QStringList &trashFiles, bool reveal) {
  if (trashFiles.isEmpty()) {
    setError(QStringLiteral("nothing selected"));
    return;
  }
  for (const QString &src : trashFiles) {
    if (!TrashStore::isTrashItem(src)) {
      setError(QStringLiteral("not a trash item"));
      return;
    }
  }
  Request req;
  req.verb = Verb::Restore;
  req.sources = trashFiles;
  req.reveal = reveal;
  enqueue(req);
}

void FileOpEngine::unlinkPaths(const QStringList &srcs) {
  if (srcs.isEmpty()) {
    setError(QStringLiteral("nothing selected"));
    return;
  }
  for (const QString &src : srcs) {
    if (isProtectedUnlink(src)) {
      setError(QStringLiteral("refusing to delete protected path"));
      return;
    }
    if (TrashStore::isInsideTrash(src) && !TrashStore::isTrashItem(src)) {
      setError(QStringLiteral("not a trash item"));
      return;
    }
  }
  Request req;
  req.verb = Verb::Unlink;
  req.sources = srcs;
  enqueue(req);
}

void FileOpEngine::duplicatePaths(const QStringList &srcs) {
  QString err;
  if (!checkSources(srcs, false, &err)) {
    setError(err);
    return;
  }
  Request req;
  req.verb = Verb::Duplicate;
  req.sources = srcs;
  enqueue(req);
}

void FileOpEngine::undo() {
  if (m_busy || !m_undo.canUndo())
    return;
  m_pendingUndo = m_undo.pop();
  m_applyingUndo = true;
  emit undoChanged();
  Request req;
  switch (m_pendingUndo.kind) {
  case UndoRecord::Kind::Copy:
  case UndoRecord::Kind::Duplicate:
    req.verb = Verb::RemoveCreated;
    req.dests = m_pendingUndo.dests;
    break;
  case UndoRecord::Kind::Mkdir:
    req.verb = Verb::RemoveEmptyDir;
    req.dests = m_pendingUndo.dests;
    break;
  case UndoRecord::Kind::Move:
  case UndoRecord::Kind::Rename:
    req.verb = Verb::MoveBack;
    req.sources = m_pendingUndo.sources;
    req.dests = m_pendingUndo.dests;
    break;
  case UndoRecord::Kind::Trash:
    req.verb = Verb::Restore;
    req.sources = m_pendingUndo.dests;
    break;
  case UndoRecord::Kind::Restore:
    req.verb = Verb::Trash;
    req.sources = m_pendingUndo.dests;
    break;
  }
  enqueue(req);
}

void FileOpEngine::enqueue(const Request &req) {
  if (m_busy) {
    if (m_applyingUndo) {
      m_undo.push(m_pendingUndo);
      m_applyingUndo = false;
      emit undoChanged();
    }
    setError(QStringLiteral("busy"));
    return;
  }
  m_error.clear();
  emit errorStringChanged();
  m_busy = true;
  emit busyChanged();
  m_cancel.store(false);
  Request job = req;
  job.cancel = &m_cancel;
  m_inFlight = QtConcurrent::run([job] { return FileOpEngine::perform(job); });
  m_watcher.setFuture(m_inFlight);
}

void FileOpEngine::onFinished() {
  Result r;
  if (m_watcher.isCanceled()) {
    r.ok = false;
    r.error = QStringLiteral("canceled");
  } else {
    r = m_watcher.result();
  }
  m_busy = false;
  if (!r.ok) {
    if (m_applyingUndo)
      m_undo.push(m_pendingUndo);
    else
      pushCompletedUndo(r);
    setError(r.error);
  } else {
    m_error.clear();
    emit errorStringChanged();
    setMessage(r.message);
    if (!m_applyingUndo)
      pushCompletedUndo(r);
    if (r.verb == Verb::Move && m_clipMode == ClipMode::Cut && !r.dests.isEmpty())
      clearClipboard();
    maybeReveal(r);
  }
  m_applyingUndo = false;
  emit busyChanged();
  emit undoChanged();
}

void FileOpEngine::setError(const QString &error) {
  if (m_error == error)
    return;
  m_error = error;
  emit errorStringChanged();
}

void FileOpEngine::setMessage(const QString &message) {
  if (m_lastMessage == message)
    return;
  m_lastMessage = message;
  emit lastMessageChanged();
}

void FileOpEngine::maybeReveal(const Result &r) {
  if (!r.reveal || r.dests.isEmpty() || !m_model)
    return;
  const QString dest = r.dests.first();
  m_model->setPath(QFileInfo(dest).absolutePath(), QFileInfo(dest).fileName());
}

void FileOpEngine::pushCompletedUndo(const Result &r) {
  if (r.dests.isEmpty())
    return;
  UndoRecord rec;
  switch (r.verb) {
  case Verb::Copy:
    rec.kind = UndoRecord::Kind::Copy;
    break;
  case Verb::Move:
    rec.kind = UndoRecord::Kind::Move;
    break;
  case Verb::Rename:
    rec.kind = UndoRecord::Kind::Rename;
    break;
  case Verb::Mkdir:
    rec.kind = UndoRecord::Kind::Mkdir;
    break;
  case Verb::Duplicate:
    rec.kind = UndoRecord::Kind::Duplicate;
    break;
  case Verb::Trash:
    rec.kind = UndoRecord::Kind::Trash;
    break;
  case Verb::Restore:
    rec.kind = UndoRecord::Kind::Restore;
    break;
  case Verb::Unlink:
  case Verb::EmptyTrash:
  case Verb::RemoveCreated:
  case Verb::RemoveEmptyDir:
  case Verb::MoveBack:
    return;
  }
  rec.sources = r.sources;
  rec.dests = r.dests;
  m_undo.push(rec);
}

bool FileOpEngine::canceled(const Request &req) {
  return req.cancel && req.cancel->load();
}

bool FileOpEngine::checkDestDir(const QString &dir, QString *err) const {
  if (TrashStore::isTrashUrl(dir) || TrashStore::isInsideTrash(dir) ||
      dir.startsWith(QLatin1String("recent://")) ||
      dir.startsWith(QLatin1String("search://"))) {
    *err = QStringLiteral("cannot paste here");
    return false;
  }
  if (dir.isEmpty() || isForbiddenPath(dir)) {
    *err = QStringLiteral("refusing operation on /");
    return false;
  }
  const QFileInfo info(dir);
  if (!info.exists() || !info.isDir()) {
    *err = QStringLiteral("destination not a directory");
    return false;
  }
  return true;
}

bool FileOpEngine::checkSources(const QStringList &srcs, bool moving,
                                QString *err) const {
  if (srcs.isEmpty()) {
    *err = QStringLiteral("nothing selected");
    return false;
  }
  for (const QString &src : srcs) {
    if (isForbiddenPath(src)) {
      *err = QStringLiteral("refusing operation on /");
      return false;
    }
    if (moving &&
        (isProtectedUnlink(src) || TrashStore::isInsideTrash(src))) {
      *err = QStringLiteral("refusing to move protected path");
      return false;
    }
  }
  return true;
}

bool FileOpEngine::validBaseName(const QString &name, QString *err) {
  if (name.isEmpty() || name == QLatin1String(".") ||
      name == QLatin1String("..") || name.contains(QLatin1Char('/')) ||
      name.contains(QChar(0))) {
    *err = QStringLiteral("invalid name");
    return false;
  }
  return true;
}

bool FileOpEngine::destParentOk(const QString &destFile, QString *err) {
  const QString dest = QDir::cleanPath(QFileInfo(destFile).absoluteFilePath());
  if (isForbiddenPath(dest)) {
    *err = QStringLiteral("refusing operation on /");
    return false;
  }
  const QString parent = QFileInfo(dest).absolutePath();
  const QStringList parts =
      QDir::cleanPath(parent).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  if (parts.contains(QLatin1String(".."))) {
    *err = QStringLiteral("destination escapes");
    return false;
  }
#ifdef Q_OS_UNIX
  // Walk parents from /. Refuse an existing final component (O_NOFOLLOW)
  // so rename(2) cannot overwrite after a suffix miss.
  int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    *err = QStringLiteral("cannot open /");
    return false;
  }
  for (const QString &part : parts) {
    if (part == QLatin1String("..") || part == QLatin1String(".")) {
      ::close(fd);
      *err = QStringLiteral("destination escapes");
      return false;
    }
    const QByteArray name = QFile::encodeName(part);
    const int next =
        ::openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ::close(fd);
    if (next < 0) {
      *err = QStringLiteral("destination parent missing");
      return false;
    }
    fd = next;
  }
  const QByteArray leaf =
      QFile::encodeName(QFileInfo(dest).fileName());
  if (!leaf.isEmpty()) {
    const int leafFd =
        ::openat(fd, leaf.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (leafFd >= 0) {
      ::close(leafFd);
      ::close(fd);
      *err = QStringLiteral("destination exists");
      return false;
    }
    Q_UNUSED(errno);
  }
  ::close(fd);
#else
  if (existsHere(dest)) {
    *err = QStringLiteral("destination exists");
    return false;
  }
#endif
  return true;
}

bool FileOpEngine::copyTree(const QString &src, const QString &dest,
                            QString *err) {
  if (isSameOrDescendant(src, dest)) {
    *err = QStringLiteral("cannot copy a folder into itself");
    return false;
  }
  const QFileInfo info(src);
  if (info.isSymLink()) {
    if (!QFile::link(info.symLinkTarget(), dest)) {
      *err = QStringLiteral("link failed: %1").arg(dest);
      return false;
    }
    return true;
  }
  if (info.isDir()) {
    if (!QDir().mkpath(dest)) {
      *err = QStringLiteral("mkdir failed: %1").arg(dest);
      return false;
    }
    const QFileInfoList ents = QDir(src).entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &e : ents) {
      if (!copyTree(e.absoluteFilePath(), QDir(dest).filePath(e.fileName()),
                    err))
        return false;
    }
    return true;
  }
  if (!info.isFile()) {
    *err = QStringLiteral("cannot copy special file: %1").arg(src);
    return false;
  }
  if (!QFile::copy(src, dest)) {
    *err = QStringLiteral("copy failed: %1").arg(src);
    return false;
  }
  return true;
}

bool FileOpEngine::removeTree(const QString &path, QString *err) {
  if (isProtectedUnlink(path)) {
    *err = QStringLiteral("refusing to remove protected path");
    return false;
  }
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink())
    return true;
  if (info.isSymLink() || info.isFile()) {
    if (!QFile::remove(path)) {
      *err = QStringLiteral("remove failed: %1").arg(path);
      return false;
    }
    return true;
  }
  if (info.isDir()) {
    QDir dir(path);
    if (!dir.removeRecursively()) {
      *err = QStringLiteral("rmdir failed: %1").arg(path);
      return false;
    }
    return true;
  }
  return true;
}

bool FileOpEngine::moveOne(const QString &src, const QString &dest,
                           QString *err) {
  if (sameFile(src, dest))
    return true;
  if (isSameOrDescendant(src, dest)) {
    *err = QStringLiteral("cannot move a folder into itself");
    return false;
  }
  if (QFile::rename(src, dest))
    return true;
  const QFileInfo info(src);
  if (!(info.isSymLink() || info.isDir() || info.isFile())) {
    *err = QStringLiteral("cannot move special file: %1").arg(src);
    return false;
  }
  // Cross-device: copy then trash-source (not rm). Moving into/out of
  // trash already has a .trashinfo; just drop the leftover inode.
  if (!copyTree(src, dest, err)) {
    // copyTree may have mkdir'd dest before a child failed (fifo, etc.).
    if (TrashStore::isInsideTrash(dest)) {
      QString ignored;
      removeTree(dest, &ignored);
    }
    return false;
  }
  if (TrashStore::isInsideTrash(dest) || TrashStore::isTrashItem(src)) {
    if (!removeTree(src, err)) {
      QString ignored;
      removeTree(dest, &ignored);
      return false;
    }
    return true;
  }
  QString trashFile;
  QString terr;
  if (TrashStore::prepareTrash(src, &trashFile, &terr)) {
    if (QFile::rename(src, trashFile))
      return true;
    QString copyErr;
    if (copyTree(src, trashFile, &copyErr) && removeTree(src, &copyErr))
      return true;
    TrashStore::abandonPrepared(trashFile);
    QString ignored;
    removeTree(trashFile, &ignored);
    terr = copyErr.isEmpty() ? terr : copyErr;
  }
  QString ignored;
  removeTree(dest, &ignored);
  *err = terr.isEmpty() ? QStringLiteral("cannot trash source after copy")
                        : terr;
  return false;
}

FileOpEngine::Result FileOpEngine::perform(const Request &req) {
  Result r;
  r.verb = req.verb;
  auto fail = [&](const QString &e) {
    r.ok = false;
    r.error = e;
    return r;
  };

  switch (req.verb) {
  case Verb::Copy:
  case Verb::Duplicate: {
    for (const QString &src : req.sources) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      if (isForbiddenPath(src))
        return fail(QStringLiteral("refusing operation on /"));
      const QString parent =
          req.verb == Verb::Duplicate ? QFileInfo(src).absolutePath()
                                      : req.destDir;
      if (isSameOrDescendant(src, parent))
        return fail(QStringLiteral("cannot copy a folder into itself"));
      const QString destName = collisionName(parent, QFileInfo(src).fileName());
      if (destName.isEmpty())
        return fail(QStringLiteral("cannot find unique name"));
      const QString dest = QDir(parent).filePath(destName);
      QString err;
      if (!destParentOk(dest, &err))
        return fail(err);
      if (!copyTree(src, dest, &err)) {
        QString ignored;
        removeTree(dest, &ignored);
        return fail(err);
      }
      r.sources.append(src);
      r.dests.append(dest);
    }
    r.ok = true;
    r.message = req.verb == Verb::Duplicate
                    ? QStringLiteral("duplicated %1").arg(r.dests.size())
                    : QStringLiteral("copied %1").arg(r.dests.size());
    return r;
  }
  case Verb::Move: {
    for (const QString &src : req.sources) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      if (isForbiddenPath(src) || isProtectedUnlink(src))
        return fail(QStringLiteral("refusing to move protected path"));
      if (isSameOrDescendant(src, req.destDir))
        return fail(QStringLiteral("cannot move a folder into itself"));
      const QString intended =
          QDir(req.destDir).filePath(QFileInfo(src).fileName());
      if (sameFile(src, intended))
        continue;
      const QString destName =
          collisionName(req.destDir, QFileInfo(src).fileName());
      if (destName.isEmpty())
        return fail(QStringLiteral("cannot find unique name"));
      const QString dest = QDir(req.destDir).filePath(destName);
      QString err;
      if (!destParentOk(dest, &err))
        return fail(err);
      if (!moveOne(src, dest, &err))
        return fail(err);
      r.sources.append(src);
      r.dests.append(dest);
    }
    r.ok = true;
    r.message = QStringLiteral("moved %1").arg(r.dests.size());
    return r;
  }
  case Verb::Rename: {
    if (req.sources.isEmpty())
      return fail(QStringLiteral("nothing to rename"));
    const QString src = req.sources.first();
    QString err;
    if (!validBaseName(req.destName, &err))
      return fail(err);
    const QString intended = QDir(req.destDir).filePath(req.destName);
    if (sameFile(src, intended)) {
      r.ok = true;
      r.message = QStringLiteral("renamed");
      return r;
    }
    const QString destName = collisionName(req.destDir, req.destName);
    if (destName.isEmpty())
      return fail(QStringLiteral("cannot find unique name"));
    const QString dest = QDir(req.destDir).filePath(destName);
    if (!destParentOk(dest, &err))
      return fail(err);
    if (!moveOne(src, dest, &err))
      return fail(err);
    r.sources.append(src);
    r.dests.append(dest);
    r.ok = true;
    r.message = QStringLiteral("renamed");
    return r;
  }
  case Verb::Mkdir: {
    QString err;
    if (!validBaseName(req.destName, &err))
      return fail(err);
    const QString destName = collisionName(req.destDir, req.destName);
    if (destName.isEmpty())
      return fail(QStringLiteral("cannot find unique name"));
    const QString dest = QDir(req.destDir).filePath(destName);
    if (!destParentOk(dest, &err))
      return fail(err);
    if (!QDir().mkdir(dest))
      return fail(QStringLiteral("mkdir failed: %1").arg(dest));
    r.dests.append(dest);
    r.ok = true;
    r.message = QStringLiteral("created folder");
    return r;
  }
  case Verb::RemoveCreated: {
    for (const QString &p : req.dests) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      QString err;
      if (!removeTree(p, &err))
        return fail(err);
    }
    r.ok = true;
    r.message = QStringLiteral("undone");
    return r;
  }
  case Verb::RemoveEmptyDir: {
    for (const QString &p : req.dests) {
      if (isProtectedUnlink(p))
        return fail(QStringLiteral("refusing to remove protected path"));
      if (QFileInfo(p).exists() && !QDir().rmdir(p))
        return fail(QStringLiteral("folder not empty"));
    }
    r.ok = true;
    r.message = QStringLiteral("undone");
    return r;
  }
  case Verb::MoveBack: {
    const int n = qMin(req.sources.size(), req.dests.size());
    for (int i = 0; i < n; ++i) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      QString err;
      const QString from = req.dests.at(i);
      QString to = req.sources.at(i);
      const QString parent = QFileInfo(to).absolutePath();
      const QString base = QFileInfo(to).fileName();
      if (existsHere(to) && !sameFile(from, to))
        to = QDir(parent).filePath(collisionName(parent, base));
      if (!destParentOk(to, &err))
        return fail(err);
      if (!moveOne(from, to, &err))
        return fail(err);
    }
    r.ok = true;
    r.message = QStringLiteral("undone");
    return r;
  }
  case Verb::Trash: {
    for (const QString &src : req.sources) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      QString err;
      if (!TrashStore::canTrash(src, &err))
        return fail(err);
      QString dest;
      if (!TrashStore::prepareTrash(src, &dest, &err))
        return fail(err);
      if (!moveOne(src, dest, &err)) {
        TrashStore::abandonPrepared(dest);
        QString ignored;
        removeTree(dest, &ignored);
        return fail(err);
      }
      r.sources.append(src);
      r.dests.append(dest);
    }
    r.ok = true;
    r.message = QStringLiteral("trashed %1").arg(r.dests.size());
    return r;
  }
  case Verb::Restore: {
    r.reveal = req.reveal;
    for (const QString &src : req.sources) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      TrashStore::Item item;
      if (!TrashStore::itemForTrashFile(src, &item))
        return fail(QStringLiteral("not a trash item"));
      if (item.origPath.isEmpty())
        return fail(QStringLiteral("missing original path"));
      QString dest = item.origPath;
      const QString parent = QFileInfo(dest).absolutePath();
      const QString base = QFileInfo(dest).fileName();
      if (existsHere(dest) && !sameFile(src, dest)) {
        const QString destName = collisionName(parent, base);
        if (destName.isEmpty())
          return fail(QStringLiteral("cannot find unique name"));
        dest = QDir(parent).filePath(destName);
      }
      QString err;
      if (!destParentOk(dest, &err))
        return fail(err);
      if (!moveOne(src, dest, &err))
        return fail(err);
      TrashStore::removeInfoFor(src);
      r.sources.append(src);
      r.dests.append(dest);
    }
    r.ok = true;
    r.message = QStringLiteral("restored %1").arg(r.dests.size());
    return r;
  }
  case Verb::Unlink: {
    for (const QString &src : req.sources) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      if (isProtectedUnlink(src))
        return fail(QStringLiteral("refusing to delete protected path"));
      QString err;
      if (TrashStore::isTrashItem(src)) {
        if (!removeTree(src, &err))
          return fail(err);
        TrashStore::removeInfoFor(src);
        r.sources.append(src);
        continue;
      }
      if (TrashStore::isInsideTrash(src))
        return fail(QStringLiteral("not a trash item"));
      const QFileInfo info(src);
      if (info.isDir() && !info.isSymLink()) {
        if (!QDir().rmdir(src))
          return fail(QStringLiteral("refusing recursive delete"));
      } else if (!QFile::remove(src)) {
        return fail(QStringLiteral("remove failed: %1").arg(src));
      }
      r.sources.append(src);
    }
    r.ok = true;
    r.message = QStringLiteral("deleted %1").arg(r.sources.size());
    return r;
  }
  case Verb::EmptyTrash: {
    const QVector<TrashStore::Item> items = TrashStore::list();
    for (const TrashStore::Item &item : items) {
      if (canceled(req))
        return fail(QStringLiteral("canceled"));
      if (!TrashStore::isTrashItem(item.trashFile))
        return fail(QStringLiteral("not a trash item"));
      QString err;
      if (!removeTree(item.trashFile, &err))
        return fail(err);
      TrashStore::removeInfoFor(item.trashFile);
      r.sources.append(item.trashFile);
    }
    TrashStore::purgeOrphanInfos();
    r.ok = true;
    r.message = QStringLiteral("emptied trash");
    return r;
  }
  }
  return fail(QStringLiteral("unknown op"));
}
