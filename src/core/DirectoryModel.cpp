#include "DirectoryModel.h"

#include "FsnLayout.h"
#include "RecentStore.h"
#include "SearchModel.h"
#include "TrashStore.h"
#include "VolumeStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>
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
  m_collator.setCaseSensitivity(Qt::CaseInsensitive);
  m_collator.setNumericMode(true); // natural sort: file2 before file10

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
  connect(&VolumeStore::instance(), &VolumeStore::changed, this, [this] {
    if (isVolumes())
      reload();
    emit volumeHintChanged();
  });
  connect(this, &DirectoryModel::pathChanged, this,
          &DirectoryModel::volumeHintChanged);
  m_thread.start();
}

DirectoryModel::~DirectoryModel() {
  ++m_gen;
  ++m_fsnGen;
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
         path.startsWith(QLatin1String("recent:")) ||
         path.startsWith(QLatin1String("volumes:"));
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

bool DirectoryModel::isVolumesPath(const QString &path) {
  return path.startsWith(QLatin1String("volumes:"));
}

bool DirectoryModel::isTrash() const { return isTrashPath(m_path); }

bool DirectoryModel::isRecent() const { return isRecentPath(m_path); }

bool DirectoryModel::isVolumes() const { return isVolumesPath(m_path); }

bool DirectoryModel::isSearch() const { return isSearchPath(m_path); }

QString DirectoryModel::searchQuery() const {
  return m_search ? m_search->query() : QString();
}

bool DirectoryModel::isContentSearch() const {
  return searching() && m_search && m_search->contentSearch();
}

QString DirectoryModel::searchRoot() const {
  return m_search ? m_search->root() : QString();
}

QVariantList DirectoryModel::folderGroups() const {
  return searching() && m_search ? m_search->folderGroups() : QVariantList();
}

QAbstractItemModel *DirectoryModel::folderGroupModel() const {
  return searching() && m_search ? m_search->folderGroupModel() : nullptr;
}

QVariantMap DirectoryModel::rowMap(int row) const {
  if (searching() && m_search)
    return m_search->rowMap(row);
  const DirectoryEntry *e = entryAt(row);
  if (!e)
    return {};
  QVariantMap m;
  m.insert(QStringLiteral("index"), row);
  m.insert(QStringLiteral("name"), e->name);
  m.insert(QStringLiteral("path"), e->path);
  m.insert(QStringLiteral("isDir"), e->isDir);
  m.insert(QStringLiteral("isSymlink"), e->isSymlink);
  m.insert(QStringLiteral("thumbnail"), e->thumbnail);
  m.insert(QStringLiteral("detail"), e->detail);
  m.insert(QStringLiteral("used"), e->used);
  m.insert(QStringLiteral("total"), e->total);
  m.insert(QStringLiteral("percent"), e->percent);
  m.insert(QStringLiteral("parentPath"), e->parentPath);
  m.insert(QStringLiteral("parentLabel"), e->parentPath);
  return m;
}

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
      {DetailRole, "detail"},
      {UsedRole, "used"},
      {TotalRole, "total"},
      {PercentRole, "percent"},
      {ParentPathRole, "parentPath"},
      {ParentLabelRole, "parentLabel"},
      {TypeLabelRole, "typeLabel"},
  };
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const {
  if (searching()) {
    const QVariant v = m_search->data(m_search->index(index.row(), 0), role);
    if (v.isValid())
      return v;
    switch (role) {
    case DetailRole:
      return QString();
    case UsedRole:
    case TotalRole:
      return QVariant::fromValue(qint64(-1));
    case PercentRole:
      return -1;
    case ParentPathRole:
    case ParentLabelRole:
    case TypeLabelRole:
      return QString();
    default:
      return v;
    }
  }
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
    return e->uri.isEmpty() && !e->path.isEmpty()
               ? QUrl::fromLocalFile(e->path)
               : e->uri;
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
  case DetailRole:
    return e->detail;
  case UsedRole:
    return e->used;
  case TotalRole:
    return e->total;
  case PercentRole:
    return e->percent;
  case ParentPathRole:
    return e->parentPath.isEmpty() && !e->path.isEmpty()
               ? QFileInfo(e->path).absolutePath()
               : e->parentPath;
  case ParentLabelRole:
    return e->parentPath;
  case TypeLabelRole:
    if (!e->typeLabel.isEmpty())
      return e->typeLabel;
    return e->isDir ? QStringLiteral("Folder") : e->mime;
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

int DirectoryModel::compareNamesForRows(int leftVisibleRow,
                                        int rightVisibleRow) const {
  if (searching()) {
    const DirectoryEntry *l = entryAt(leftVisibleRow);
    const DirectoryEntry *r = entryAt(rightVisibleRow);
    if (!l || !r)
      return 0;
    return QString::compare(l->name, r->name, Qt::CaseInsensitive);
  }
  if (leftVisibleRow < 0 || leftVisibleRow >= m_visible.size() ||
      rightVisibleRow < 0 || rightVisibleRow >= m_visible.size())
    return 0;
  const int la = m_visible.at(leftVisibleRow);
  const int ra = m_visible.at(rightVisibleRow);
  if (la < m_sortKeys.size() && ra < m_sortKeys.size())
    return m_sortKeys.at(la).compare(m_sortKeys.at(ra));
  return QString::compare(m_all.at(la).name, m_all.at(ra).name,
                          Qt::CaseInsensitive);
}

QString DirectoryModel::normalizePath(const QString &path) {
  if (isSearchPath(path))
    return path;
  if (isRecentPath(path))
    return QStringLiteral("recent://");
  if (isVolumesPath(path))
    return QStringLiteral("volumes://");
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
  m_sortKeys.clear();
  m_visible.clear();
  m_indexByName.clear();
  m_indexByPath.clear();
  m_visibleRowByAll.clear();
  m_suppressedNames.clear();
  m_pendingThumbs.clear();
  m_thumbRows.clear();
  m_thumbFirst = -1;
  m_thumbLast = -1;
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
    connect(
        m_recents, &RecentStore::entriesChanged, this,
        [this] {
          if (isRecent())
            reload();
        },
        Qt::QueuedConnection);
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
  connect(m_search, &SearchModel::queryChanged, this,
          &DirectoryModel::searchQueryChanged);
  connect(m_search, &SearchModel::kindChanged, this,
          &DirectoryModel::searchQueryChanged);
  connect(m_search, &SearchModel::rootChanged, this,
          &DirectoryModel::searchQueryChanged);
  connect(m_search, &SearchModel::folderGroupsChanged, this,
          &DirectoryModel::folderGroupsChanged);
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
  const QString previous = m_path;
  if (!isVirtualPath(m_path) && !m_path.isEmpty() && isVirtualPath(resolved))
    m_returnPath = m_path;
  updateVolumeRoot(previous, resolved);
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
    emit folderGroupsChanged();
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
    if (isVolumesPath(resolved)) {
      loadVolumesListing();
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
  emit folderGroupsChanged();
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
  m.insert(QStringLiteral("uri"),
           e.uri.isEmpty() && !e.path.isEmpty() ? QUrl::fromLocalFile(e.path)
                                                : e.uri);
  m.insert(QStringLiteral("isDir"), e.isDir);
  m.insert(QStringLiteral("size"), e.size);
  m.insert(QStringLiteral("mtime"), e.mtime);
  m.insert(QStringLiteral("mime"), e.mime);
  m.insert(QStringLiteral("isSymlink"), e.isSymlink);
  m.insert(QStringLiteral("origPath"), e.origPath);
  m.insert(QStringLiteral("perm"), formatPerm(e.perm, e.isDir, e.isSymlink));
  m.insert(QStringLiteral("typeLabel"),
           !e.typeLabel.isEmpty()
               ? e.typeLabel
               : (e.isDir ? QStringLiteral("Folder") : e.mime));
  m.insert(QStringLiteral("mode"), e.perm);
  m.insert(QStringLiteral("detail"), e.detail);
  m.insert(QStringLiteral("used"), e.used);
  m.insert(QStringLiteral("total"), e.total);
  m.insert(QStringLiteral("percent"), e.percent);
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

void DirectoryModel::refreshFsn(const QString &view) {
  if (!view.isEmpty())
    m_fsnView = view;
  const FsnLayout::View fsnView = m_fsnView == QLatin1String("map")
                                      ? FsnLayout::MapView
                                      : FsnLayout::TreeVView;
  ++m_fsnGen;
  const quint64 gen = m_fsnGen;
  if (isVirtualPath(m_path)) {
    QVector<FsnLayout::Item> items;
    items.reserve(m_visible.size());
    for (int vis : m_visible) {
      if (vis < 0 || vis >= m_all.size())
        continue;
      const DirectoryEntry &e = m_all.at(vis);
      FsnLayout::Item b;
      b.name = e.name;
      b.path = e.path;
      b.isDir = e.isDir;
      b.bytes = e.size > 0 ? e.size : (e.isDir ? 32768 : 1);
      items.append(b);
    }
    QString rootName = m_path;
    const int schemeEnd = rootName.indexOf(QLatin1String("://"));
    if (schemeEnd > 0)
      rootName = rootName.left(schemeEnd);
    m_fsnBoxes = FsnLayout::toVariantList(
        FsnLayout::buildFromItems(rootName, m_path, items, fsnView));
    m_fsnListing = false;
    emit fsnBoxesChanged();
    emit fsnListingChanged();
    return;
  }
  m_fsnListing = true;
  emit fsnListingChanged();
  const QString path = m_path;
  const bool hidden = m_showHidden;
  QPointer<DirectoryModel> self(this);
  (void)QtConcurrent::run([self, gen, path, hidden, fsnView] {
    const QVariantList boxes =
        FsnLayout::toVariantList(FsnLayout::build(path, hidden, fsnView));
    if (!self)
      return;
    QMetaObject::invokeMethod(self.data(), "applyFsnBoxes",
                              Qt::QueuedConnection, Q_ARG(quint64, gen),
                              Q_ARG(QVariantList, boxes));
  });
}

void DirectoryModel::applyFsnBoxes(quint64 gen, const QVariantList &boxes) {
  if (gen != m_fsnGen)
    return;
  m_fsnBoxes = boxes;
  m_fsnListing = false;
  emit fsnBoxesChanged();
  emit fsnListingChanged();
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

int DirectoryModel::stepSearchGrid(int index, int dx, int dy,
                                   int columns) const {
  if (!searching() || !m_search || !m_search->folderGroupModel())
    return index + dy * qMax(1, columns) + dx;
  return m_search->folderGroupModel()->stepVisual(index, dx, dy, columns);
}

void DirectoryModel::activateIndex(int sourceRow) {
  setCurrentIndex(sourceRow);
  activateCurrent();
}

void DirectoryModel::requestRestore(const QStringList &trashFiles) {
  if (trashFiles.isEmpty())
    return;
  if (receivers(SIGNAL(restoreRequested(QStringList))) > 0) {
    emit restoreRequested(trashFiles);
    return;
  }
  QString dest;
  QString err;
  if (!TrashStore::restore(trashFiles.first(), &dest, &err)) {
    m_error = err;
    emit errorStringChanged();
    return;
  }
  const QFileInfo fi(dest);
  setPath(fi.absolutePath(), fi.fileName());
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
    requestRestore({e->path});
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

void DirectoryModel::updateVolumeRoot(const QString &previous,
                                      const QString &next) {
  if (isVolumesPath(previous) && !isVirtualPath(next)) {
    const auto v = VolumeStore::instance().findMount(next);
    m_volumeRoot = v.extra ? v.mountPoint : QString();
    if (v.extra)
      m_returnPath = QStringLiteral("volumes://");
    return;
  }
  if (isVirtualPath(next)) {
    if (!isVolumesPath(next))
      m_volumeRoot.clear();
    return;
  }
  const auto root = VolumeStore::instance().extraRoot(next);
  if (root.extra)
    m_volumeRoot = root.mountPoint;
  else
    m_volumeRoot.clear();
}

QString DirectoryModel::volumeHint() const {
  if (isVolumes())
    return {};
  const QString path = isVirtualPath(m_path) ? QString() : m_path;
  const auto v = VolumeStore::instance().containing(path);
  if (v.mountPoint.isEmpty() || v.total <= 0)
    return {};
  return v.label + QStringLiteral("  ") + VolumeStore::formatBytes(v.free) +
         QStringLiteral(" free / ") + VolumeStore::formatBytes(v.total);
}

void DirectoryModel::loadVolumesListing() {
  QVector<DirectoryEntry> batch;
  const auto vols = VolumeStore::instance().volumes();
  batch.reserve(vols.size());
  QSet<QString> names;
  for (const auto &v : vols) {
    DirectoryEntry e;
    e.name = v.label;
    if (names.contains(e.name))
      e.name = v.label + QLatin1Char(' ') + QFileInfo(v.mountPoint).fileName();
    names.insert(e.name);
    e.path = v.mountPoint;
    e.uri = QUrl::fromLocalFile(v.mountPoint);
    e.isDir = true;
    e.size = v.free;
    e.used = v.used;
    e.total = v.total;
    e.percent = v.total > 0
                    ? qBound(0, int((v.used * 100) / v.total), 100)
                    : -1;
    e.mime = QStringLiteral("inode/directory");
    e.iconName = QStringLiteral("folder");
    e.dirKind = QStringLiteral("volume");
    e.detail = VolumeStore::detailText(v);
    e.origPath = v.mountPoint;
    batch.append(e);
  }
  onBatchReady(m_gen, batch);
  onFinished(m_gen, true, QString());
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
    if (e.name.isEmpty())
      continue;
    // recent:// is unique by full path; two README.md must both show.
    if (e.dirKind == QLatin1String("recent")) {
      if (e.path.isEmpty() || m_indexByPath.contains(e.path))
        continue;
    } else if (m_indexByName.contains(e.name) ||
               m_suppressedNames.contains(e.name)) {
      continue;
    }
    const int allIndex = m_all.size();
    m_all.append(e);
    m_sortKeys.append(m_collator.sortKey(e.name));
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
}

int DirectoryModel::applyEntry(const DirectoryEntry &entry) {
  const auto it = m_indexByName.constFind(entry.name);
  if (it == m_indexByName.cend())
    return -1;
  const int allIndex = it.value();
  if (allIndex < 0 || allIndex >= m_all.size())
    return -1;
  const QString oldThumb = m_all[allIndex].thumbnail;
  const QString oldPath = m_all[allIndex].path;
  m_all[allIndex] = entry;
  if (m_all[allIndex].thumbnail.isEmpty())
    m_all[allIndex].thumbnail = oldThumb;
  if (oldPath != entry.path && !oldPath.isEmpty())
    m_indexByPath.remove(oldPath);
  if (!entry.path.isEmpty())
    m_indexByPath.insert(entry.path, allIndex);
  return m_visibleRowByAll.value(allIndex, -1);
}

void DirectoryModel::onStatsReady(quint64 generation,
                                  const QVector<DirectoryEntry> &batch,
                                  bool priority) {
  Q_UNUSED(priority);
  if (generation != m_gen)
    return;
  QStringList thumbs;
  thumbs.reserve(batch.size());
  QStringList applied;
  applied.reserve(batch.size());
  QVector<int> changedRows;
  changedRows.reserve(batch.size());
  bool currentTouched = false;
  for (const DirectoryEntry &e : batch) {
    qint64 oldMtime = 0;
    bool hadThumb = false;
    const auto it = m_indexByName.constFind(e.name);
    if (it != m_indexByName.cend()) {
      const int allIndex = it.value();
      if (allIndex >= 0 && allIndex < m_all.size()) {
        oldMtime = m_all.at(allIndex).mtime;
        hadThumb = !m_all.at(allIndex).thumbnail.isEmpty();
      }
    }
    const int visRow = applyEntry(e);
    if (visRow >= 0) {
      changedRows.append(visRow);
      if (visRow == m_currentIndex)
        currentTouched = true;
    }
    if (e.path.isEmpty())
      continue;
    applied.append(e.path);
    const bool dirTouched =
        e.isDir && oldMtime > 0 && e.mtime != oldMtime && hadThumb;
    const bool pending = m_pendingThumbs.remove(e.path);
    const bool onScreen = visRow >= 0 && m_thumbRows.contains(visRow);
    if (dirTouched || pending || (!hadThumb && onScreen))
      thumbs.append(e.path);
  }
  // One dataChanged per contiguous run instead of one per entry: 50k
  // per-entry signals made the sort proxy re-place rows one at a time.
  if (!changedRows.isEmpty()) {
    std::sort(changedRows.begin(), changedRows.end());
    int runStart = changedRows.first();
    int prev = runStart;
    for (int i = 1; i <= changedRows.size(); ++i) {
      const int row = i < changedRows.size() ? changedRows.at(i) : -2;
      if (row == prev || row == prev + 1) {
        prev = row;
        continue;
      }
      emit dataChanged(index(runStart), index(prev));
      runStart = prev = row;
    }
  }
  if (currentTouched)
    emitCurrentStat();
  if (!applied.isEmpty())
    emit statsApplied(applied);
  maybeActivatePending();
  if (!thumbs.isEmpty())
    refreshThumbs(thumbs);
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
  e.isHidden = !name.isEmpty() && name[0] == QLatin1Char('.');
  e.size = -1;
  e.mtime = 0;
  if (isDir) {
    e.isDir = true;
    e.dirKind = QStringLiteral("posix");
    e.iconName = QStringLiteral("folder");
    e.mime = QStringLiteral("inode/directory");
    e.typeLabel = QStringLiteral("Folder");
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
  m_sortKeys.append(m_collator.sortKey(name));
  m_indexByName.insert(name, allIndex);
  if (!e.path.isEmpty())
    m_indexByPath.insert(e.path, allIndex);
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
  if (allIndex >= 0 && allIndex < m_all.size()) {
    if (!m_all[allIndex].path.isEmpty())
      m_indexByPath.remove(m_all[allIndex].path);
    m_all[allIndex] = DirectoryEntry{};
  }
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
  if (!e.path.isEmpty())
    m_indexByPath.remove(e.path);
  e.path = QDir(m_path).filePath(to);
  if (!e.path.isEmpty())
    m_indexByPath.insert(e.path, allIndex);
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
  if (!m_volumeRoot.isEmpty() &&
      (m_path == m_volumeRoot || !QFileInfo(m_volumeRoot).isDir() ||
       !QFileInfo(m_path).isDir())) {
    m_volumeRoot.clear();
    setPath(QStringLiteral("volumes://"), QString(), true);
    return;
  }
  if (isTrash() || isRecent() || isSearch() || isVolumes()) {
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
  QVector<int> rows;
  const int count = rowCount();
  if (count > 0 && last >= 0) {
    first = qBound(0, first, count - 1);
    last = qBound(0, last, count - 1);
    if (last < first)
      qSwap(first, last);
    rows.reserve(last - first + 1);
    for (int i = first; i <= last; ++i)
      rows.append(i);
  }
  requestSourceThumbs(rows, sizePx);
}

void DirectoryModel::requestSourceThumbs(const QVector<int> &sourceRows,
                                         int sizePx) {
  if (sizePx <= 0)
    sizePx = 128;
  m_thumbSizePx = sizePx;
  m_thumbRows = sourceRows;
  if (!sourceRows.isEmpty()) {
    m_thumbFirst = sourceRows.first();
    m_thumbLast = sourceRows.last();
  } else {
    m_thumbFirst = -1;
    m_thumbLast = -1;
  }
  if (!m_thumbs)
    return;
  QVector<ThumbnailJob> jobs;
  jobs.reserve(sourceRows.size());
  const int n = sourceRows.size();
  const int center = n / 2;
  for (int i = 0; i < n; ++i) {
    const DirectoryEntry *e = entryAt(sourceRows.at(i));
    if (!e || e->path.isEmpty())
      continue;
    if (!e->thumbnail.isEmpty())
      continue;
    qint64 mtime = e->mtime;
    if (mtime <= 0)
      mtime = QFileInfo(e->path).lastModified().toMSecsSinceEpoch();
    if (mtime <= 0)
      continue;
    ThumbnailJob job;
    job.path = e->path;
    job.mime = e->mime;
    job.mtime = mtime;
    job.sizePx = sizePx;
    job.priority = qAbs(i - center);
    jobs.append(job);
  }
  m_thumbs->requestVisible(jobs);
}

void DirectoryModel::refreshThumbs(const QStringList &paths) {
  if (!m_thumbs || paths.isEmpty())
    return;
  const int sizePx = m_thumbSizePx > 0 ? m_thumbSizePx : 128;
  QVector<ThumbnailJob> jobs;
  jobs.reserve(paths.size());
  QSet<QString> seen;
  for (const QString &raw : paths) {
    const QString path = QDir::cleanPath(raw);
    if (path.isEmpty() || seen.contains(path))
      continue;
    seen.insert(path);
    auto it = m_indexByPath.constFind(path);
    if (it == m_indexByPath.cend()) {
      m_pendingThumbs.insert(path);
      continue;
    }
    const int allIndex = it.value();
    if (allIndex < 0 || allIndex >= m_all.size())
      continue;
    DirectoryEntry &e = m_all[allIndex];
    if (e.path.isEmpty())
      continue;
    qint64 mtime = QFileInfo(e.path).lastModified().toMSecsSinceEpoch();
    if (mtime <= 0)
      mtime = QDateTime::currentMSecsSinceEpoch();
    if (!e.thumbnail.isEmpty())
      mtime = qMax(mtime + 1, QDateTime::currentMSecsSinceEpoch());
    ThumbnailJob job;
    job.path = e.path;
    job.mime = e.mime;
    job.mtime = mtime;
    job.sizePx = sizePx;
    jobs.append(job);
  }
  if (!jobs.isEmpty())
    m_thumbs->request(jobs);
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
