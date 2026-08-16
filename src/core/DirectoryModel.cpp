#include "DirectoryModel.h"

#include "RecentStore.h"
#include "SearchModel.h"
#include "TrashStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QUrl>
#include <QVariantMap>

#include <cstdio>
#include <sys/stat.h>

namespace {

QString expandUser(QString path) {
  path = path.trimmed();
  if (path.isEmpty() || path == QLatin1String("~"))
    return QDir::homePath();
  if (path.startsWith(QLatin1String("~/")))
    return QDir::homePath() + path.mid(1);
  return path;
}

int fileMode(const QString &path) {
  if (path.isEmpty())
    return 0;
#ifdef Q_OS_UNIX
  struct stat st {};
  if (::lstat(QFile::encodeName(path).constData(), &st) != 0)
    return 0;
  return static_cast<int>(st.st_mode & 07777);
#else
  Q_UNUSED(path);
  return 0;
#endif
}

QString formatPerm(int mode, bool isDir, bool isSymlink) {
  QString s;
  s.reserve(10);
  if (isDir)
    s += QLatin1Char('d');
  else if (isSymlink)
    s += QLatin1Char('l');
  else
    s += QLatin1Char('-');
  const int bits[] = {S_IRUSR, S_IWUSR, S_IXUSR, S_IRGRP, S_IWGRP,
                      S_IXGRP, S_IROTH, S_IWOTH, S_IXOTH};
  const char letters[] = {'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x'};
  for (int i = 0; i < 9; ++i)
    s += (mode & bits[i]) ? QLatin1Char(letters[i]) : QLatin1Char('-');
  return s;
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent) : QAbstractListModel(parent) {
  qRegisterMetaType<DirectoryEntry>();
  qRegisterMetaType<QVector<DirectoryEntry>>();

  m_lister = new DirectoryLister;
  m_lister->moveToThread(&m_thread);
  connect(this, &DirectoryModel::listRequested, m_lister,
          &DirectoryLister::requestList, Qt::QueuedConnection);
  connect(this, &DirectoryModel::statRequested, m_lister,
          &DirectoryLister::requestStatNames, Qt::QueuedConnection);
  connect(m_lister, &DirectoryLister::batchReady, this,
          &DirectoryModel::onBatchReady, Qt::QueuedConnection);
  connect(m_lister, &DirectoryLister::statsReady, this,
          &DirectoryModel::onStatsReady, Qt::QueuedConnection);
  connect(m_lister, &DirectoryLister::finished, this,
          &DirectoryModel::onFinished, Qt::QueuedConnection);
  connect(&m_watcher, &DirectoryWatcher::eventsReady, this,
          &DirectoryModel::onWatchEvents);
  m_thumbs = new ThumbnailService(this);
  connect(m_thumbs, &ThumbnailService::thumbnailReady, this,
          &DirectoryModel::onThumbnailReady);
  m_thread.start();
}

DirectoryModel::~DirectoryModel() {
  ++m_gen;
  if (m_lister)
    m_lister->abandon(m_gen);
  m_thread.quit();
  m_thread.wait();
  delete m_lister;
  m_lister = nullptr;
}

bool DirectoryModel::isVirtualPath(const QString &path) {
  return path.startsWith(QLatin1String("search:")) ||
         path.startsWith(QLatin1String("trash:")) ||
         path.startsWith(QLatin1String("recent:"));
}

bool DirectoryModel::isSearchPath(const QString &path) {
  return path.startsWith(QLatin1String("search:"));
}

bool DirectoryModel::isTrashPath(const QString &path) {
  return TrashStore::isTrashUrl(path);
}

bool DirectoryModel::isRecentPath(const QString &path) {
  return path.startsWith(QLatin1String("recent:"));
}

bool DirectoryModel::isTrash() const { return isTrashPath(m_path); }

bool DirectoryModel::isRecent() const { return isRecentPath(m_path); }

int DirectoryModel::currentIndex() const {
  if (searching())
    return m_search->currentIndex();
  return m_currentIndex;
}

int DirectoryModel::count() const {
  if (searching())
    return m_search->rowCount();
  return m_visible.size();
}

int DirectoryModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  if (searching())
    return m_search->rowCount();
  return m_visible.size();
}

QHash<int, QByteArray> DirectoryModel::roleNames() const {
  return {
      {NameRole, "name"},
      {PathRole, "path"},
      {UriRole, "uri"},
      {IsDirRole, "isDir"},
      {SizeRole, "size"},
      {MtimeRole, "mtime"},
      {MimeRole, "mime"},
      {IconNameRole, "iconName"},
      {ThumbnailRole, "thumbnail"},
      {IsHiddenRole, "isHidden"},
      {IsSymlinkRole, "isSymlink"},
      {DirKindRole, "dirKind"},
      {OrigPathRole, "origPath"},
      {PermRole, "perm"},
  };
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const {
  if (searching())
    return m_search->data(m_search->index(index.row(), 0), role);
  const DirectoryEntry *e = entryAt(index.row());
  if (!index.isValid() || !e)
    return {};
  switch (role) {
  case Qt::DisplayRole:
  case NameRole:
    return e->name;
  case PathRole:
    return e->path;
  case UriRole:
    return e->uri;
  case IsDirRole:
    return e->isDir;
  case SizeRole:
    return e->size;
  case MtimeRole:
    return e->mtime;
  case MimeRole:
    return e->mime;
  case IconNameRole:
    return e->iconName;
  case ThumbnailRole:
    return e->thumbnail;
  case IsHiddenRole:
    return e->isHidden;
  case IsSymlinkRole:
    return e->isSymlink;
  case DirKindRole:
    return e->dirKind;
  case OrigPathRole:
    return e->origPath;
  case PermRole:
    return formatPerm(e->perm, e->isDir, e->isSymlink);
  default:
    return {};
  }
}

const DirectoryEntry *DirectoryModel::entryAt(int visibleRow) const {
  if (searching())
    return m_search->entryAt(visibleRow);
  if (visibleRow < 0 || visibleRow >= m_visible.size())
    return nullptr;
  const int all = m_visible.at(visibleRow);
  if (all < 0 || all >= m_all.size())
    return nullptr;
  return &m_all.at(all);
}

QString DirectoryModel::normalizePath(const QString &path) {
  if (isSearchPath(path))
    return path;
  if (isRecentPath(path))
    return QStringLiteral("recent://");
  if (TrashStore::isTrashUrl(path))
    return TrashStore::normalizeUrl(path);
  const QString expanded = expandUser(path);
  const QString abs = QDir::cleanPath(QFileInfo(expanded).absoluteFilePath());
  if (TrashStore::mapsToTrashView(abs) || TrashStore::mapsToTrashView(expanded))
    return QString::fromUtf8(TrashStore::kUrl);
  QFileInfo info(expanded);
  if (info.isDir()) {
    const QString canon = info.canonicalFilePath();
    return canon.isEmpty() ? info.absoluteFilePath() : canon;
  }
  if (info.exists()) {
    const QFileInfo parent(info.absolutePath());
    const QString canon = parent.canonicalFilePath();
    return canon.isEmpty() ? parent.absoluteFilePath() : canon;
  }
  return QDir::cleanPath(info.absoluteFilePath());
}

void DirectoryModel::resetListing() {
  beginResetModel();
  m_all.clear();
  m_visible.clear();
  m_indexByName.clear();
  m_indexByPath.clear();
  m_visibleRowByAll.clear();
  m_suppressedNames.clear();
  m_currentIndex = -1;
  endResetModel();
  emit countChanged();
  emit currentIndexChanged();
}

void DirectoryModel::setSearchModel(SearchModel *model) {
  if (m_search == model)
    return;
  unbindSearch();
  m_search = model;
  bindSearch();
  if (searching())
    adoptSearchRows();
}

void DirectoryModel::setRecentStore(RecentStore *store) {
  if (m_recents == store)
    return;
  if (m_recents)
    disconnect(m_recents, nullptr, this, nullptr);
  m_recents = store;
  if (m_recents)
    connect(m_recents, &RecentStore::entriesChanged, this, [this] {
      if (isRecent())
        reload();
    });
  if (isRecent())
    reload();
}

void DirectoryModel::bindSearch() {
  if (!m_search)
    return;
  connect(m_search, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
    if (m_searching)
      beginResetModel();
  });
  connect(m_search, &QAbstractItemModel::modelReset, this, [this] {
    if (!m_searching)
      return;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
  });
  connect(m_search, &QAbstractItemModel::rowsAboutToBeInserted, this,
          [this](const QModelIndex &, int first, int last) {
            if (m_searching)
              beginInsertRows(QModelIndex(), first, last);
          });
  connect(m_search, &QAbstractItemModel::rowsInserted, this, [this] {
    if (!m_searching)
      return;
    endInsertRows();
    emit countChanged();
  });
  connect(m_search, &QAbstractItemModel::dataChanged, this,
          [this](const QModelIndex &tl, const QModelIndex &br,
                 const QList<int> &roles) {
            if (!m_searching)
              return;
            emit dataChanged(index(tl.row()), index(br.row()), roles);
          });
  connect(m_search, &SearchModel::currentIndexChanged, this, [this] {
    if (m_searching) {
      emit currentIndexChanged();
      emitCurrentStat();
    }
  });
  connect(m_search, &SearchModel::listingChanged, this, [this] {
    if (!m_searching)
      return;
    const bool on = m_search->listing();
    if (m_listing == on)
      return;
    m_listing = on;
    emit listingChanged();
  });
  connect(m_search, &SearchModel::errorStringChanged, this, [this] {
    if (!m_searching)
      return;
    m_error = m_search->errorString();
    emit errorStringChanged();
  });
  connect(m_search, &SearchModel::fileActivated, this,
          &DirectoryModel::fileActivated);
  connect(m_search, &SearchModel::navigateRequested, this,
          [this](const QString &path) { setPath(path); });
  connect(m_search, &SearchModel::firstRowsInserted, this,
          [this](qint64 ms, int rows) {
            if (!m_searching)
              return;
            m_lastFirstRowsMs = ms;
            emit firstRowsInserted(ms, rows);
          });
}

void DirectoryModel::unbindSearch() {
  if (m_search)
    disconnect(m_search, nullptr, this, nullptr);
}

void DirectoryModel::adoptSearchRows() {
  const int n = m_search ? m_search->rowCount() : 0;
  if (n <= 0)
    return;
  beginInsertRows(QModelIndex(), 0, n - 1);
  endInsertRows();
  emit countChanged();
  emit currentIndexChanged();
}

void DirectoryModel::setPath(const QString &path, const QString &selectName,
                             bool force) {
  const QString resolved = normalizePath(path);
  if (!force && resolved == m_path && m_error.isEmpty()) {
    if (!selectName.isEmpty()) {
      m_pendingSelect = selectName;
      maybeSelectPending();
    }
    return;
  }

  if (!force)
    emit aboutToNavigate();

  ++m_gen;
  if (m_lister)
    m_lister->abandon(m_gen);
  if (m_thumbs)
    m_thumbs->cancelAll();
  m_thumbFirst = -1;
  m_thumbLast = -1;

  if (!isSearchPath(resolved) && m_search)
    m_search->cancel();

  m_searching = false;
  resetListing();
  if (!isVirtualPath(m_path) && !m_path.isEmpty() && isVirtualPath(resolved))
    m_returnPath = m_path;
  m_path = resolved;
  m_error.clear();
  m_pendingActivate.clear();
  m_pendingSelect = selectName;
  m_loggedFirst = false;
  m_lastFirstRowsMs = -1;
  m_listTimer.start();

  if (isVirtualPath(resolved)) {
    m_searching = isSearchPath(resolved) && m_search;
    m_listing = m_searching && m_search->listing();
    emit pathChanged();
    emit errorStringChanged();
    emit listingChanged();
    emitCurrentStat();
    if (isTrashPath(resolved)) {
      QString err;
      if (!TrashStore::ensureDirs(&err)) {
        m_error = err;
        emit errorStringChanged();
      }
      loadTrashListing();
      m_watcher.setPath(TrashStore::filesDir());
      m_watchSerial = m_watcher.serial();
      return;
    }
    m_watcher.setPath(QString());
    m_watchSerial = m_watcher.serial();
    if (isRecentPath(resolved)) {
      loadRecentListing();
      return;
    }
    if (m_searching)
      adoptSearchRows();
    return;
  }

  m_listing = true;
  m_watcher.setPath(m_path);
  m_watchSerial = m_watcher.serial();
  emit pathChanged();
  emit errorStringChanged();
  emit listingChanged();
  emitCurrentStat();
  emit listRequested(m_gen, m_path);
}

QString DirectoryModel::currentName() const {
  const DirectoryEntry *e = entryAt(currentIndex());
  return e ? e->name : QString();
}

bool DirectoryModel::currentIsDir() const {
  const DirectoryEntry *e = entryAt(currentIndex());
  return e && e->isDir;
}

QString DirectoryModel::currentOrigPath() const {
  const DirectoryEntry *e = entryAt(currentIndex());
  if (!e)
    return {};
  return e->origPath.isEmpty() ? e->path : e->origPath;
}

QVariantMap DirectoryModel::currentStat() const {
  const DirectoryEntry *e = entryAt(currentIndex());
  return e ? entryToMap(*e) : QVariantMap{};
}

void DirectoryModel::emitCurrentStat() { emit currentStatChanged(); }

QVariantMap DirectoryModel::entryToMap(const DirectoryEntry &e) const {
  QVariantMap m;
  m.insert(QStringLiteral("name"), e.name);
  m.insert(QStringLiteral("path"), e.path);
  m.insert(QStringLiteral("uri"), e.uri);
  m.insert(QStringLiteral("isDir"), e.isDir);
  m.insert(QStringLiteral("size"), e.size);
  m.insert(QStringLiteral("mtime"), e.mtime);
  m.insert(QStringLiteral("mime"), e.mime);
  m.insert(QStringLiteral("isSymlink"), e.isSymlink);
  m.insert(QStringLiteral("origPath"), e.origPath);
  m.insert(QStringLiteral("perm"), formatPerm(e.perm, e.isDir, e.isSymlink));
  m.insert(QStringLiteral("mode"), e.perm);
  return m;
}

QVariantMap DirectoryModel::cachedStat(const QString &path) const {
  if (searching())
    return m_search->cachedStat(path);
  if (path.isEmpty())
    return {};
  auto it = m_indexByPath.constFind(path);
  if (it == m_indexByPath.cend())
    it = m_indexByPath.constFind(QDir::cleanPath(path));
  if (it == m_indexByPath.cend())
    return {};
  const int all = it.value();
  if (all < 0 || all >= m_all.size())
    return {};
  return entryToMap(m_all.at(all));
}

void DirectoryModel::requestStatPath(const QString &path) {
  if (searching())
    return;
  if (path.isEmpty() || m_path.isEmpty())
    return;
  const QFileInfo fi(path);
  const QString parent = QDir::cleanPath(fi.absolutePath());
  if (parent != QDir::cleanPath(m_path))
    return;
  emit statRequested(m_gen, m_path, QStringList{fi.fileName()});
}

void DirectoryModel::setShowHidden(bool show) {
  if (m_showHidden == show)
    return;
  m_showHidden = show;
  emit showHiddenChanged();
  if (searching()) {
    m_search->setShowHidden(show);
    return;
  }
  rebuildVisible();
}

void DirectoryModel::rebuildVisible() {
  const QString currentName =
      entryAt(m_currentIndex) ? entryAt(m_currentIndex)->name : QString();
  beginResetModel();
  m_visible.clear();
  m_visibleRowByAll.clear();
  m_visible.reserve(m_all.size());
  for (int i = 0; i < m_all.size(); ++i) {
    if (m_all.at(i).name.isEmpty())
      continue;
    if (m_showHidden || !m_all.at(i).isHidden) {
      m_visibleRowByAll.insert(i, m_visible.size());
      m_visible.append(i);
    }
  }
  endResetModel();
  int next = -1;
  if (!currentName.isEmpty()) {
    for (int i = 0; i < m_visible.size(); ++i) {
      if (m_all.at(m_visible.at(i)).name == currentName) {
        next = i;
        break;
      }
    }
  }
  if (next < 0 && !m_visible.isEmpty())
    next = 0;
  m_currentIndex = next;
  emit countChanged();
  emit currentIndexChanged();
  emitCurrentStat();
  maybeSelectPending();
}

void DirectoryModel::setCurrentIndex(int index) {
  if (searching()) {
    m_search->setCurrentIndex(index);
    return;
  }
  int next = index;
  if (m_visible.isEmpty())
    next = -1;
  else
    next = qBound(0, index, m_visible.size() - 1);
  if (m_currentIndex == next)
    return;
  m_currentIndex = next;
  emit currentIndexChanged();
  emitCurrentStat();
}

void DirectoryModel::moveCursor(int delta) {
  if (searching()) {
    m_search->moveCursor(delta);
    return;
  }
  if (m_visible.isEmpty())
    return;
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  else
    setCurrentIndex(m_currentIndex + delta);
}

void DirectoryModel::activateCurrent() {
  if (searching()) {
    m_search->activateCurrent();
    return;
  }
  const DirectoryEntry *e = entryAt(m_currentIndex);
  if (!e)
    return;
  if (e->dirKind == QLatin1String("trash") || isTrash()) {
    restoreCurrent();
    return;
  }
  if (e->dirKind == QLatin1String("pending")) {
    m_pendingActivate = e->name;
    return;
  }
  if (e->isDir)
    setPath(e->path);
  else
    emit fileActivated(e->path, e->mime);
}

bool DirectoryModel::restoreCurrent() {
  const DirectoryEntry *e = entryAt(currentIndex());
  if (!e || e->path.isEmpty())
    return false;
  QString dest;
  QString err;
  if (!TrashStore::restore(e->path, &dest, &err)) {
    m_error = err;
    emit errorStringChanged();
    return false;
  }
  const QFileInfo fi(dest);
  setPath(fi.absolutePath(), fi.fileName());
  return true;
}

bool DirectoryModel::emptyTrash() {
  if (!isTrash())
    return false;
  QString err;
  if (!TrashStore::empty(&err)) {
    m_error = err;
    emit errorStringChanged();
    return false;
  }
  reload();
  return true;
}

DirectoryEntry DirectoryModel::makeTrashEntry(const QString &name) const {
  DirectoryEntry e;
  e.name = name;
  e.dirKind = QStringLiteral("trash");
  e.isHidden = false;
  e.size = -1;
  e.mtime = 0;
  e.iconName = QStringLiteral("text-x-generic");
  TrashStore::Item item;
  if (TrashStore::itemForName(name, &item)) {
    e.path = item.trashFile;
    e.origPath = item.origPath;
    e.isDir = item.isDir;
    e.isSymlink = item.isSymlink;
    e.size = item.size;
    if (item.deletedAt.isValid())
      e.mtime = item.deletedAt.toMSecsSinceEpoch();
    e.perm = fileMode(item.trashFile);
  } else {
    e.path = QDir(TrashStore::filesDir()).filePath(name);
  }
  e.uri = QUrl(QStringLiteral("trash:///") +
               QString::fromUtf8(QUrl::toPercentEncoding(name)));
  if (e.isDir) {
    e.iconName = QStringLiteral("folder");
    e.mime = QStringLiteral("inode/directory");
  } else if (!e.path.isEmpty()) {
    const QMimeDatabase db;
    const QMimeType mime =
        db.mimeTypeForFile(e.path, QMimeDatabase::MatchExtension);
    e.mime = mime.name();
    e.iconName = mime.genericIconName();
    if (e.iconName.isEmpty())
      e.iconName = mime.iconName();
    if (e.iconName.isEmpty())
      e.iconName = e.isSymlink ? QStringLiteral("emblem-symbolic-link")
                               : QStringLiteral("text-x-generic");
  }
  return e;
}

void DirectoryModel::loadTrashListing() {
  const QVector<TrashStore::Item> items = TrashStore::list();
  QVector<DirectoryEntry> batch;
  batch.reserve(items.size());
  for (const TrashStore::Item &item : items)
    batch.append(makeTrashEntry(item.name));
  onBatchReady(m_gen, batch);
  onFinished(m_gen, true, QString());
}

DirectoryEntry DirectoryModel::makeRecentEntry(const QString &path,
                                               const QString &mime,
                                               const QString &ts) const {
  DirectoryEntry e;
  const QFileInfo fi(path);
  e.name = fi.fileName();
  if (e.name.isEmpty())
    e.name = path;
  e.path = fi.exists() ? fi.absoluteFilePath() : path;
  e.origPath = e.path;
  e.uri = QUrl::fromLocalFile(e.path);
  e.isDir = fi.isDir();
  e.isSymlink = fi.isSymLink();
  e.isHidden = !e.name.isEmpty() && e.name[0] == QLatin1Char('.');
  e.size = e.isDir ? -1 : (fi.exists() ? fi.size() : -1);
  const QDateTime when = QDateTime::fromString(ts, Qt::ISODate);
  e.mtime = when.isValid() ? when.toMSecsSinceEpoch()
                           : (fi.exists() ? fi.lastModified().toMSecsSinceEpoch()
                                          : 0);
  e.dirKind = QStringLiteral("recent");
  if (fi.exists())
    e.perm = fileMode(e.path);
  if (e.isDir) {
    e.mime = QStringLiteral("inode/directory");
    e.iconName = QStringLiteral("folder");
    return e;
  }
  e.mime = mime;
  if (e.mime.isEmpty() && !e.path.isEmpty()) {
    QMimeDatabase db;
    e.mime = db.mimeTypeForFile(e.path, QMimeDatabase::MatchExtension).name();
  }
  e.iconName = QStringLiteral("text-x-generic");
  return e;
}

void DirectoryModel::loadRecentListing() {
  QVector<DirectoryEntry> batch;
  if (m_recents) {
    const auto entries = m_recents->uniqueNewest();
    batch.reserve(entries.size());
    for (const RecentStore::Entry &item : entries)
      batch.append(makeRecentEntry(item.path, item.mime, item.ts));
  }
  onBatchReady(m_gen, batch);
  onFinished(m_gen, true, QString());
}

void DirectoryModel::onBatchReady(quint64 generation,
                                  const QVector<DirectoryEntry> &batch) {
  if (generation != m_gen)
    return;

  QVector<int> added;
  added.reserve(batch.size());
  m_all.reserve(m_all.size() + batch.size());
  for (int i = 0; i < batch.size(); ++i) {
    const DirectoryEntry &e = batch.at(i);
    if (e.name.isEmpty() || m_indexByName.contains(e.name) ||
        m_suppressedNames.contains(e.name))
      continue;
    const int allIndex = m_all.size();
    m_all.append(e);
    m_indexByName.insert(e.name, allIndex);
    if (!e.path.isEmpty())
      m_indexByPath.insert(e.path, allIndex);
    if (m_showHidden || !e.isHidden)
      added.append(allIndex);
  }

  if (!added.isEmpty()) {
    const int from = m_visible.size();
    beginInsertRows(QModelIndex(), from, from + added.size() - 1);
    for (int i = 0; i < added.size(); ++i)
      m_visibleRowByAll.insert(added.at(i), from + i);
    m_visible += added;
    endInsertRows();
    if (m_currentIndex < 0)
      setCurrentIndex(0);
    emit countChanged();
  }
  maybeSelectPending();

  if (!m_loggedFirst) {
    m_loggedFirst = true;
    m_lastFirstRowsMs = m_listTimer.elapsed();
    std::fprintf(stderr, "synchro: open %s first_rows %d %lldms%s\n",
                 qPrintable(m_path), static_cast<int>(m_visible.size()),
                 static_cast<long long>(m_lastFirstRowsMs),
                 m_lastFirstRowsMs > 80 ? " SLOW" : "");
    emit firstRowsInserted(m_lastFirstRowsMs, m_visible.size());
  }
  emitCurrentStat();
}

void DirectoryModel::applyEntry(const DirectoryEntry &entry) {
  const auto it = m_indexByName.constFind(entry.name);
  if (it == m_indexByName.cend())
    return;
  const int allIndex = it.value();
  if (allIndex < 0 || allIndex >= m_all.size())
    return;
  const QString oldThumb = m_all[allIndex].thumbnail;
  const QString oldPath = m_all[allIndex].path;
  m_all[allIndex] = entry;
  if (m_all[allIndex].thumbnail.isEmpty())
    m_all[allIndex].thumbnail = oldThumb;
  if (oldPath != entry.path) {
    if (!oldPath.isEmpty())
      m_indexByPath.remove(oldPath);
    if (!entry.path.isEmpty())
      m_indexByPath.insert(entry.path, allIndex);
  }

  const auto vis = m_visibleRowByAll.constFind(allIndex);
  if (vis == m_visibleRowByAll.cend())
    return;
  const QModelIndex idx = index(vis.value());
  emit dataChanged(idx, idx);
  if (vis.value() == m_currentIndex)
    emitCurrentStat();
}

void DirectoryModel::onStatsReady(quint64 generation,
                                  const QVector<DirectoryEntry> &batch,
                                  bool priority) {
  Q_UNUSED(priority);
  if (generation != m_gen)
    return;
  for (const DirectoryEntry &e : batch) {
    applyEntry(e);
    emit entryStatReady(e.path, entryToMap(e));
  }
  maybeActivatePending();
  if (m_thumbFirst >= 0)
    requestVisibleThumbs(m_thumbFirst, m_thumbLast, m_thumbSizePx);
}

void DirectoryModel::maybeActivatePending() {
  if (m_pendingActivate.isEmpty())
    return;
  const auto it = m_indexByName.constFind(m_pendingActivate);
  if (it == m_indexByName.cend()) {
    m_pendingActivate.clear();
    return;
  }
  const DirectoryEntry &e = m_all.at(it.value());
  if (e.dirKind == QLatin1String("pending"))
    return;
  const bool isDir = e.isDir;
  const QString dest = e.path;
  const QString mime = e.mime;
  m_pendingActivate.clear();
  if (isDir)
    setPath(dest);
  else
    emit fileActivated(dest, mime);
}

void DirectoryModel::onFinished(quint64 generation, bool ok,
                                const QString &error) {
  if (generation != m_gen)
    return;
  m_listing = false;
  if (!ok) {
    m_error = error;
    emit errorStringChanged();
  }
  emit listingChanged();
  maybeSelectPending();
  m_pendingSelect.clear();
}

void DirectoryModel::maybeSelectPending() {
  if (m_pendingSelect.isEmpty())
    return;
  const auto it = m_indexByName.constFind(m_pendingSelect);
  if (it == m_indexByName.cend())
    return;
  const auto vis = m_visibleRowByAll.constFind(it.value());
  if (vis == m_visibleRowByAll.cend())
    return;
  setCurrentIndex(vis.value());
  m_pendingSelect.clear();
}

DirectoryEntry DirectoryModel::makePlaceholder(const QString &name,
                                               bool isDir) const {
  DirectoryEntry e;
  e.name = name;
  e.path = QDir(m_path).filePath(name);
  e.uri = QUrl::fromLocalFile(e.path);
  e.isHidden = !name.isEmpty() && name[0] == QLatin1Char('.');
  e.size = -1;
  e.mtime = 0;
  if (isDir) {
    e.isDir = true;
    e.dirKind = QStringLiteral("posix");
    e.iconName = QStringLiteral("folder");
    e.mime = QStringLiteral("inode/directory");
  } else {
    e.dirKind = QStringLiteral("pending");
    e.iconName = QStringLiteral("text-x-generic");
  }
  return e;
}

void DirectoryModel::insertVisible(int allIndex) {
  if (m_visibleRowByAll.contains(allIndex))
    return;
  const int vis = m_visible.size();
  beginInsertRows(QModelIndex(), vis, vis);
  m_visible.append(allIndex);
  m_visibleRowByAll.insert(allIndex, vis);
  endInsertRows();
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  emit countChanged();
}

void DirectoryModel::removeVisible(int allIndex) {
  const auto it = m_visibleRowByAll.constFind(allIndex);
  if (it == m_visibleRowByAll.cend())
    return;
  const int visRow = it.value();
  beginRemoveRows(QModelIndex(), visRow, visRow);
  m_visible.removeAt(visRow);
  m_visibleRowByAll.remove(allIndex);
  for (auto i = m_visibleRowByAll.begin(); i != m_visibleRowByAll.end(); ++i) {
    if (i.value() > visRow)
      --(i.value());
  }
  endRemoveRows();
  if (m_currentIndex == visRow) {
    if (m_visible.isEmpty())
      setCurrentIndex(-1);
    else
      setCurrentIndex(qMin(visRow, m_visible.size() - 1));
  } else if (m_currentIndex > visRow) {
    setCurrentIndex(m_currentIndex - 1);
  }
  emit countChanged();
}

void DirectoryModel::insertPlaceholder(const QString &name, bool isDir) {
  if (name.isEmpty() || m_indexByName.contains(name))
    return;
  const DirectoryEntry e = makePlaceholder(name, isDir);
  const int allIndex = m_all.size();
  m_all.append(e);
  m_indexByName.insert(name, allIndex);
  if (m_showHidden || !e.isHidden)
    insertVisible(allIndex);
}

void DirectoryModel::removeByName(const QString &name) {
  if (m_pendingActivate == name)
    m_pendingActivate.clear();
  const auto it = m_indexByName.constFind(name);
  if (it == m_indexByName.cend())
    return;
  const int allIndex = it.value();
  removeVisible(allIndex);
  m_indexByName.remove(name);
  if (allIndex >= 0 && allIndex < m_all.size())
    m_all[allIndex] = DirectoryEntry{};
}

void DirectoryModel::renameEntry(const QString &from, const QString &to) {
  if (from == to || to.isEmpty())
    return;
  const auto it = m_indexByName.constFind(from);
  if (it == m_indexByName.cend()) {
    insertPlaceholder(to, false);
    return;
  }
  const int allIndex = it.value();
  if (m_indexByName.contains(to))
    removeByName(to);
  DirectoryEntry &e = m_all[allIndex];
  e.name = to;
  e.path = QDir(m_path).filePath(to);
  e.uri = QUrl::fromLocalFile(e.path);
  e.isHidden = !to.isEmpty() && to[0] == QLatin1Char('.');
  m_indexByName.remove(from);
  m_indexByName.insert(to, allIndex);
  const bool wantVis = m_showHidden || !e.isHidden;
  const bool haveVis = m_visibleRowByAll.contains(allIndex);
  if (wantVis && !haveVis)
    insertVisible(allIndex);
  else if (!wantVis && haveVis)
    removeVisible(allIndex);
  else if (haveVis) {
    const QModelIndex idx = index(m_visibleRowByAll.value(allIndex));
    emit dataChanged(idx, idx);
  }
}

void DirectoryModel::navigateToExistingParent() {
  if (isTrash() || isRecent() || isSearchPath(m_path)) {
    const QString dest =
        m_returnPath.isEmpty() ? QDir::homePath() : m_returnPath;
    setPath(dest, QString(), true);
    return;
  }
  QString p = m_path;
  const QString vanished = QFileInfo(QDir::cleanPath(p)).fileName();
  for (int i = 0; i < 64; ++i) {
    QDir dir(p);
    if (!dir.cdUp())
      break;
    p = dir.absolutePath();
    if (QFileInfo(p).isDir()) {
      setPath(p, vanished, true);
      return;
    }
  }
}

void DirectoryModel::reload() {
  if (m_path.isEmpty())
    return;
  const QString path = m_path;
  const QString name = currentName();
  setPath(path, name, true);
}

void DirectoryModel::onWatchEvents(const QVector<DirectoryWatchEvent> &events) {
  if (events.isEmpty())
    return;

  QStringList needStat;
  needStat.reserve(events.size());
  for (const DirectoryWatchEvent &ev : events) {
    if (ev.serial != m_watchSerial)
      continue;
    if (ev.kind == DirectoryWatchEvent::Gone) {
      navigateToExistingParent();
      return;
    }
    if (ev.kind == DirectoryWatchEvent::Overflow) {
      reload();
      return;
    }
    if (ev.name.isEmpty() && ev.kind != DirectoryWatchEvent::Renamed)
      continue;
    switch (ev.kind) {
    case DirectoryWatchEvent::Deleted:
      m_suppressedNames.insert(ev.name);
      removeByName(ev.name);
      break;
    case DirectoryWatchEvent::Created:
      m_suppressedNames.remove(ev.name);
      insertPlaceholder(ev.name, ev.isDir);
      needStat.append(ev.name);
      break;
    case DirectoryWatchEvent::Attrib:
      if (!m_indexByName.contains(ev.name) &&
          !m_suppressedNames.contains(ev.name))
        insertPlaceholder(ev.name, ev.isDir);
      if (m_indexByName.contains(ev.name))
        needStat.append(ev.name);
      break;
    case DirectoryWatchEvent::Renamed:
      m_suppressedNames.insert(ev.name);
      m_suppressedNames.remove(ev.newName);
      renameEntry(ev.name, ev.newName);
      needStat.append(ev.newName);
      break;
    case DirectoryWatchEvent::Gone:
    case DirectoryWatchEvent::Overflow:
      break;
    }
  }

  needStat.removeDuplicates();
  QStringList live;
  live.reserve(needStat.size());
  for (const QString &name : needStat) {
    if (m_indexByName.contains(name))
      live.append(name);
  }
  if (isTrash()) {
    reload();
    return;
  }
  if (!live.isEmpty())
    emit statRequested(m_gen, m_path, live);
}

void DirectoryModel::requestVisibleThumbs(int first, int last, int sizePx) {
  if (sizePx <= 0)
    sizePx = 128;
  m_thumbSizePx = sizePx;
  m_thumbFirst = first;
  m_thumbLast = last;
  if (!m_thumbs)
    return;
  QVector<ThumbnailJob> jobs;
  const int rows = rowCount();
  if (rows > 0 && last >= 0) {
    first = qBound(0, first, rows - 1);
    last = qBound(0, last, rows - 1);
    if (last < first)
      qSwap(first, last);
    const int center = first + (last - first) / 2;
    jobs.reserve(last - first + 1);
    for (int i = first; i <= last; ++i) {
      const DirectoryEntry *e = entryAt(i);
      if (!e || e->isDir || e->path.isEmpty() || e->mtime <= 0)
        continue;
      if (!e->thumbnail.isEmpty())
        continue;
      ThumbnailJob job;
      job.path = e->path;
      job.mime = e->mime;
      job.mtime = e->mtime;
      job.sizePx = sizePx;
      job.priority = qAbs(i - center);
      jobs.append(job);
    }
  }
  m_thumbs->requestVisible(jobs);
}

void DirectoryModel::onThumbnailReady(const QString &path, const QString &url) {
  if (searching()) {
    m_search->setThumbnail(path, url);
    return;
  }
  if (url.isEmpty() || path.isEmpty())
    return;
  const auto it = m_indexByPath.constFind(path);
  if (it == m_indexByPath.cend())
    return;
  const int allIndex = it.value();
  if (allIndex < 0 || allIndex >= m_all.size())
    return;
  if (m_all[allIndex].thumbnail == url)
    return;
  m_all[allIndex].thumbnail = url;
  const auto vis = m_visibleRowByAll.constFind(allIndex);
  if (vis == m_visibleRowByAll.cend())
    return;
  const QModelIndex idx = index(vis.value());
  emit dataChanged(idx, idx, {ThumbnailRole});
}
