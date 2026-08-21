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
#include <QRegularExpression>
#include <QUrl>
#include <QVariant>
#include <QtConcurrent>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
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

QString formatBytes(qint64 n) {
  if (n < 1024)
    return QString::number(n) + QLatin1Char('B');
  if (n < 1024 * 1024)
    return QString::number(n / 1024.0, 'f', 1) + QLatin1Char('K');
  if (n < 1024ll * 1024 * 1024)
    return QString::number(n / (1024.0 * 1024.0), 'f', 1) + QLatin1Char('M');
  return QString::number(n / (1024.0 * 1024.0 * 1024.0), 'f', 1) +
         QLatin1Char('G');
}

QStringList urlsToPaths(const QList<QUrl> &urls) {
  QStringList paths;
  for (const QUrl &url : urls) {
    if (!url.isLocalFile())
      continue;
    const QString path = url.toLocalFile();
    if (!path.isEmpty())
      paths.append(path);
  }
  return paths;
}

QStringList uriListToPaths(const QByteArray &raw) {
  QList<QUrl> urls;
  const QString text = QString::fromUtf8(raw);
  const QStringList lines =
      text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                 Qt::SkipEmptyParts);
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
      continue;
    if (line.startsWith(QLatin1String("file:")) ||
        line.contains(QLatin1String("://"))) {
      urls.append(QUrl::fromEncoded(line.toUtf8()));
      continue;
    }
    if (line.startsWith(QLatin1Char('/')))
      urls.append(QUrl::fromLocalFile(line));
  }
  return urlsToPaths(urls);
}

constexpr auto kGnomeCopied = "x-special/gnome-copied-files";

} // namespace

FileOpEngine::FileOpEngine(QObject *parent) : QObject(parent), m_undo(this) {
  connect(&m_watcher, &QFutureWatcher<Result>::finished, this,
          &FileOpEngine::onFinished);
  m_progressTimer.setInterval(50);
  connect(&m_progressTimer, &QTimer::timeout, this,
          &FileOpEngine::refreshProgress);
  if (QClipboard *cb = QGuiApplication::clipboard()) {
    connect(cb, &QClipboard::dataChanged, this,
            &FileOpEngine::hydrateClipboardFromOs);
    connect(cb, &QClipboard::changed, this, [this](QClipboard::Mode mode) {
      if (mode == QClipboard::Clipboard)
        hydrateClipboardFromOs();
    });
  }
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

QString FileOpEngine::uriListFromPaths(const QStringList &paths) {
  QString list;
  for (const QString &p : paths) {
    list += QUrl::fromLocalFile(p).toString(QUrl::FullyEncoded);
    list += QLatin1String("\r\n");
  }
  return list;
}

QString FileOpEngine::gnomeCopiedFromPaths(const QStringList &paths,
                                           const QString &mode) {
  const QByteArray head = (mode == QLatin1String("cut") ||
                           mode == QLatin1String("move"))
                              ? QByteArray("cut")
                              : QByteArray("copy");
  QByteArray gnome = head;
  gnome += '\n';
  for (const QString &p : paths) {
    gnome += QUrl::fromLocalFile(p).toString(QUrl::FullyEncoded).toUtf8();
    gnome += '\n';
  }
  return QString::fromUtf8(gnome);
}

bool FileOpEngine::sameDevice(const QString &a, const QString &b) {
#ifdef Q_OS_UNIX
  if (a.isEmpty() || b.isEmpty())
    return false;
  struct stat sa {};
  struct stat sb {};
  if (::stat(QFile::encodeName(a).constData(), &sa) != 0)
    return false;
  if (::stat(QFile::encodeName(b).constData(), &sb) != 0)
    return false;
  return sa.st_dev == sb.st_dev;
#else
  Q_UNUSED(a);
  Q_UNUSED(b);
  return false;
#endif
}

void FileOpEngine::publishClipboard() {
  QClipboard *cb = QGuiApplication::clipboard();
  if (!cb)
    return;
  if (m_clipPaths.isEmpty()) {
    cb->clear();
    return;
  }
  auto *mime = new QMimeData;
  QList<QUrl> urls;
  urls.reserve(m_clipPaths.size());
  for (const QString &p : m_clipPaths)
    urls.append(QUrl::fromLocalFile(p));
  const QString list = uriListFromPaths(m_clipPaths);
  const QString mode =
      m_clipMode == ClipMode::Cut ? QStringLiteral("cut") : QStringLiteral("copy");
  mime->setUrls(urls);
  mime->setData(QStringLiteral("text/uri-list"), list.toUtf8());
  mime->setData(QStringLiteral("text/plain"), list.toUtf8());
  mime->setData(QString::fromLatin1(kGnomeCopied),
                gnomeCopiedFromPaths(m_clipPaths, mode).toUtf8());
  cb->setMimeData(mime, QClipboard::Clipboard);
}

QVariantMap FileOpEngine::dragMime(const QStringList &paths) const {
  QVariantMap out;
  if (paths.isEmpty())
    return out;
  const QString list = uriListFromPaths(paths);
  out.insert(QStringLiteral("text/uri-list"), list);
  out.insert(QStringLiteral("text/plain"), list);
  out.insert(QStringLiteral("x-special/gnome-copied-files"),
             gnomeCopiedFromPaths(paths, QStringLiteral("copy")));
  out.insert(QStringLiteral("application/x-synchro-drop"), QStringLiteral("1"));
  return out;
}

QStringList FileOpEngine::pathsFromDrop(const QVariantList &urls,
                                        const QString &uriList,
                                        const QString &gnome) const {
  QMimeData mime;
  if (!gnome.isEmpty())
    mime.setData(QString::fromLatin1(kGnomeCopied), gnome.toUtf8());
  if (!uriList.isEmpty())
    mime.setData(QStringLiteral("text/uri-list"), uriList.toUtf8());
  QList<QUrl> parsed;
  parsed.reserve(urls.size());
  for (const QVariant &v : urls) {
    if (v.canConvert<QUrl>()) {
      const QUrl url = v.toUrl();
      if (!url.isEmpty())
        parsed.append(url);
    } else if (v.canConvert<QString>()) {
      const QString s = v.toString();
      if (s.isEmpty())
        continue;
      parsed.append(QUrl(s));
    }
  }
  if (!parsed.isEmpty())
    mime.setUrls(parsed);
  QStringList paths;
  QString mode;
  if (!readClipboardMime(&mime, &paths, &mode))
    return {};
  return paths;
}

bool FileOpEngine::canDropOn(const QString &destDir) const {
  QString err;
  return checkDestDir(destDir, &err);
}

bool FileOpEngine::canAcceptDrop(const QStringList &srcs,
                                 const QString &destDir) const {
  QString err;
  if (!checkDestDir(destDir, &err))
    return false;
  for (const QString &src : srcs) {
    if (src.isEmpty())
      continue;
    if (sameFile(src, destDir) || isSameOrDescendant(src, destDir))
      return false;
  }
  return true;
}

void FileOpEngine::dropOn(const QStringList &srcs, const QString &destDir,
                          const QString &action) {
  QString act = action.toLower();
  if (act != QLatin1String("copy") && act != QLatin1String("move") &&
      act != QLatin1String("auto"))
    act = QStringLiteral("copy");
  if (act == QLatin1String("auto")) {
    bool same = !srcs.isEmpty();
    for (const QString &src : srcs) {
      if (!sameDevice(src, destDir)) {
        same = false;
        break;
      }
    }
    act = same ? QStringLiteral("move") : QStringLiteral("copy");
  }

  QStringList filtered;
  filtered.reserve(srcs.size());
  for (const QString &src : srcs) {
    if (src.isEmpty())
      continue;
    if (sameFile(src, destDir) || isSameOrDescendant(src, destDir)) {
      setError(QStringLiteral("cannot drop a folder into itself"));
      return;
    }
    const QString intended =
        QDir(destDir).filePath(QFileInfo(src).fileName());
    if (act == QLatin1String("move") && sameFile(src, intended))
      continue;
    filtered.append(src);
  }
  if (filtered.isEmpty()) {
    setMessage(QStringLiteral("already there"));
    return;
  }
  if (act == QLatin1String("move"))
    movePaths(filtered, destDir);
  else
    copyPaths(filtered, destDir);
}

bool FileOpEngine::readClipboardMime(const QMimeData *mime, QStringList *paths,
                                     QString *mode) {
  if (!mime || !paths || !mode)
    return false;
  paths->clear();
  *mode = QStringLiteral("copy");
  // Wayland fills formats lazily; touch the list before reading bodies.
  const QStringList offered = mime->formats();
  Q_UNUSED(offered);
  const QString gnomeType = QString::fromLatin1(kGnomeCopied);
  if (mime->hasFormat(gnomeType)) {
    const QByteArray raw = mime->data(gnomeType);
    if (!raw.isEmpty()) {
      const QString text = QString::fromUtf8(raw);
      const QStringList lines =
          text.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                     Qt::SkipEmptyParts);
      if (!lines.isEmpty()) {
        const QString head = lines.first().trimmed().toLower();
        if (head == QLatin1String("cut") || head == QLatin1String("copy")) {
          *mode = head;
          QByteArray rest;
          for (int i = 1; i < lines.size(); ++i) {
            rest += lines.at(i).trimmed().toUtf8();
            rest += '\n';
          }
          *paths = uriListToPaths(rest);
          if (!paths->isEmpty())
            return true;
        }
      }
    }
  }
  if (mime->hasFormat(QStringLiteral("text/uri-list"))) {
    *paths = uriListToPaths(mime->data(QStringLiteral("text/uri-list")));
    if (!paths->isEmpty())
      return true;
  }
  if (mime->hasUrls()) {
    *paths = urlsToPaths(mime->urls());
    if (!paths->isEmpty())
      return true;
  }
  if (mime->hasText()) {
    *paths = uriListToPaths(mime->text().toUtf8());
    if (!paths->isEmpty())
      return true;
  }
  return false;
}

void FileOpEngine::hydrateClipboardFromOs() {
  QClipboard *cb = QGuiApplication::clipboard();
  if (!cb)
    return;
  const QMimeData *mime = cb->mimeData(QClipboard::Clipboard);
  QStringList paths;
  QString mode;
  if (!readClipboardMime(mime, &paths, &mode))
    return;
  if (paths == m_clipPaths &&
      ((mode == QLatin1String("cut") && m_clipMode == ClipMode::Cut) ||
       (mode == QLatin1String("copy") && m_clipMode == ClipMode::Copy)))
    return;
  m_clipPaths = paths;
  m_clipMode = mode == QLatin1String("cut") ? ClipMode::Cut : ClipMode::Copy;
  emit clipboardChanged();
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
  hydrateClipboardFromOs();
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
  resetProgress();
  switch (req.verb) {
  case Verb::Copy:
  case Verb::Duplicate:
    m_busyLabel = QStringLiteral("copying");
    break;
  case Verb::Move:
    m_busyLabel = QStringLiteral("moving");
    break;
  default:
    m_busyLabel = QStringLiteral("working");
    break;
  }
  m_busy = true;
  emit busyChanged();
  m_cancel.store(false);
  Request job = req;
  job.cancel = &m_cancel;
  job.progress = &m_xfer;
  m_progressTimer.start();
  refreshProgress();
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
  m_progressTimer.stop();
  m_busy = false;
  resetProgress();
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
    if (m_model) {
      QStringList thumbs = r.dests;
      for (const QString &dest : r.dests) {
        const QString parent = QFileInfo(dest).absolutePath();
        if (!parent.isEmpty())
          thumbs.append(parent);
      }
      m_model->refreshThumbs(thumbs);
    }
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

void FileOpEngine::resetProgress() {
  m_xfer.bytesDone.store(0);
  m_xfer.bytesTotal.store(0);
  m_xfer.itemsDone.store(0);
  m_xfer.itemsTotal.store(0);
  if (m_progress != 0 || !m_progressText.isEmpty()) {
    m_progress = 0;
    m_progressText.clear();
    emit progressChanged();
  }
}

void FileOpEngine::refreshProgress() {
  const qint64 done = m_xfer.bytesDone.load();
  const qint64 total = m_xfer.bytesTotal.load();
  const int items = m_xfer.itemsDone.load();
  const int n = m_xfer.itemsTotal.load();
  int pct = 0;
  if (total > 0)
    pct = int((done * 100) / total);
  else if (n > 0)
    pct = (items * 100) / n;
  else if (!m_busy)
    pct = 0;
  QString text;
  if (m_busy) {
    if (total > 0)
      text = m_busyLabel + QLatin1Char(' ') + formatBytes(done) +
             QStringLiteral(" / ") + formatBytes(total);
    else if (n > 0)
      text = m_busyLabel + QLatin1Char(' ') + QString::number(items) +
             QLatin1Char('/') + QString::number(n);
    else
      text = m_busyLabel + QStringLiteral("…");
  }
  if (pct == m_progress && text == m_progressText)
    return;
  m_progress = pct;
  m_progressText = text;
  emit progressChanged();
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
      dir.startsWith(QLatin1String("search://")) ||
      dir.startsWith(QLatin1String("volumes://"))) {
    *err = QStringLiteral("cannot write here");
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

void FileOpEngine::addTreeStats(const QString &path, qint64 *bytes,
                                int *items) {
  if (!bytes || !items)
    return;
  const QFileInfo info(path);
  if (info.isSymLink()) {
    *items += 1;
    return;
  }
  if (info.isDir()) {
    const QFileInfoList ents = QDir(path).entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &e : ents)
      addTreeStats(e.absoluteFilePath(), bytes, items);
    return;
  }
  if (info.isFile()) {
    *items += 1;
    *bytes += info.size();
  }
}

bool FileOpEngine::copyTree(const QString &src, const QString &dest,
                            QString *err, TransferProgress *prog,
                            std::atomic<bool> *cancel) {
  if (cancel && cancel->load()) {
    *err = QStringLiteral("canceled");
    return false;
  }
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
    if (prog)
      prog->itemsDone.fetch_add(1);
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
                    err, prog, cancel))
        return false;
    }
    return true;
  }
  if (!info.isFile()) {
    *err = QStringLiteral("cannot copy special file: %1").arg(src);
    return false;
  }
  QFile in(src);
  QFile out(dest);
  if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
    *err = QStringLiteral("copy failed: %1").arg(src);
    return false;
  }
  char buf[256 * 1024];
  while (!in.atEnd()) {
    if (cancel && cancel->load()) {
      *err = QStringLiteral("canceled");
      return false;
    }
    const qint64 n = in.read(buf, sizeof(buf));
    if (n < 0 || out.write(buf, n) != n) {
      *err = QStringLiteral("copy failed: %1").arg(src);
      return false;
    }
    if (prog)
      prog->bytesDone.fetch_add(n);
  }
  out.setPermissions(in.permissions());
  if (prog)
    prog->itemsDone.fetch_add(1);
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
                           QString *err, TransferProgress *prog,
                           std::atomic<bool> *cancel) {
  if (sameFile(src, dest))
    return true;
  if (isSameOrDescendant(src, dest)) {
    *err = QStringLiteral("cannot move a folder into itself");
    return false;
  }
  if (QFile::rename(src, dest)) {
    if (prog)
      prog->itemsDone.fetch_add(1);
    return true;
  }
  const QFileInfo info(src);
  if (!(info.isSymLink() || info.isDir() || info.isFile())) {
    *err = QStringLiteral("cannot move special file: %1").arg(src);
    return false;
  }
  // Cross-device: copy then trash-source (not rm). Moving into/out of
  // trash already has a .trashinfo; just drop the leftover inode.
  if (!copyTree(src, dest, err, prog, cancel)) {
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
    if (copyTree(src, trashFile, &copyErr, nullptr, cancel) &&
        removeTree(src, &copyErr))
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
    if (req.progress) {
      qint64 bytes = 0;
      int items = 0;
      for (const QString &src : req.sources)
        addTreeStats(src, &bytes, &items);
      req.progress->bytesTotal.store(bytes);
      req.progress->itemsTotal.store(items);
    }
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
      if (!copyTree(src, dest, &err, req.progress, req.cancel)) {
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
    if (req.progress) {
      qint64 bytes = 0;
      int items = 0;
      for (const QString &src : req.sources)
        addTreeStats(src, &bytes, &items);
      req.progress->bytesTotal.store(bytes);
      req.progress->itemsTotal.store(items);
    }
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
      if (!moveOne(src, dest, &err, req.progress, req.cancel))
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
    if (!moveOne(src, dest, &err, req.progress, req.cancel))
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
