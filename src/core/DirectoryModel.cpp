#include "DirectoryModel.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <cstdio>

namespace {

QString expandUser(QString path) {
  path = path.trimmed();
  if (path.isEmpty() || path == QLatin1String("~"))
    return QDir::homePath();
  if (path.startsWith(QLatin1String("~/")))
    return QDir::homePath() + path.mid(1);
  return path;
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

int DirectoryModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
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
  };
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const {
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
  default:
    return {};
  }
}

const DirectoryEntry *DirectoryModel::entryAt(int visibleRow) const {
  if (visibleRow < 0 || visibleRow >= m_visible.size())
    return nullptr;
  const int all = m_visible.at(visibleRow);
  if (all < 0 || all >= m_all.size())
    return nullptr;
  return &m_all.at(all);
}

QString DirectoryModel::normalizePath(const QString &path) {
  const QString expanded = expandUser(path);
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
  m_visibleRowByAll.clear();
  m_suppressedNames.clear();
  m_currentIndex = -1;
  endResetModel();
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

  resetListing();
  m_path = resolved;
  m_error.clear();
  m_pendingActivate.clear();
  m_pendingSelect = selectName;
  m_listing = true;
  m_loggedFirst = false;
  m_lastFirstRowsMs = -1;
  m_listTimer.start();
  m_watcher.setPath(m_path);
  m_watchSerial = m_watcher.serial();
  emit pathChanged();
  emit errorStringChanged();
  emit listingChanged();
  emit listRequested(m_gen, m_path);
}

QString DirectoryModel::currentName() const {
  const DirectoryEntry *e = entryAt(m_currentIndex);
  return e ? e->name : QString();
}

void DirectoryModel::setShowHidden(bool show) {
  if (m_showHidden == show)
    return;
  m_showHidden = show;
  rebuildVisible();
  emit showHiddenChanged();
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
  maybeSelectPending();
}

void DirectoryModel::setCurrentIndex(int index) {
  int next = index;
  if (m_visible.isEmpty())
    next = -1;
  else
    next = qBound(0, index, m_visible.size() - 1);
  if (m_currentIndex == next)
    return;
  m_currentIndex = next;
  emit currentIndexChanged();
}

void DirectoryModel::moveCursor(int delta) {
  if (m_visible.isEmpty())
    return;
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  else
    setCurrentIndex(m_currentIndex + delta);
}

void DirectoryModel::activateCurrent() {
  const DirectoryEntry *e = entryAt(m_currentIndex);
  if (!e)
    return;
  if (e->dirKind == QLatin1String("pending")) {
    m_pendingActivate = e->name;
    return;
  }
  if (e->isDir)
    setPath(e->path);
  else
    emit fileActivated(e->path, e->mime);
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

void DirectoryModel::applyEntry(const DirectoryEntry &entry) {
  const auto it = m_indexByName.constFind(entry.name);
  if (it == m_indexByName.cend())
    return;
  const int allIndex = it.value();
  if (allIndex < 0 || allIndex >= m_all.size())
    return;
  m_all[allIndex] = entry;

  const auto vis = m_visibleRowByAll.constFind(allIndex);
  if (vis == m_visibleRowByAll.cend())
    return;
  const QModelIndex idx = index(vis.value());
  emit dataChanged(idx, idx);
}

void DirectoryModel::onStatsReady(quint64 generation,
                                  const QVector<DirectoryEntry> &batch,
                                  bool priority) {
  Q_UNUSED(priority);
  if (generation != m_gen)
    return;
  for (const DirectoryEntry &e : batch)
    applyEntry(e);
  maybeActivatePending();
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
  if (!live.isEmpty())
    emit statRequested(m_gen, m_path, live);
}
