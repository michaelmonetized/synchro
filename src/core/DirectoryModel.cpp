#include "DirectoryModel.h"

#include <QDir>
#include <QFileInfo>

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
  connect(m_lister, &DirectoryLister::batchReady, this,
          &DirectoryModel::onBatchReady, Qt::QueuedConnection);
  connect(m_lister, &DirectoryLister::statsReady, this,
          &DirectoryModel::onStatsReady, Qt::QueuedConnection);
  connect(m_lister, &DirectoryLister::finished, this,
          &DirectoryModel::onFinished, Qt::QueuedConnection);
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
  m_currentIndex = -1;
  endResetModel();
  emit countChanged();
  emit currentIndexChanged();
}

void DirectoryModel::setPath(const QString &path) {
  const QString resolved = normalizePath(path);
  ++m_gen;
  if (m_lister)
    m_lister->abandon(m_gen);

  resetListing();
  m_path = resolved;
  m_error.clear();
  m_pendingActivate.clear();
  m_listing = true;
  m_loggedFirst = false;
  m_lastFirstRowsMs = -1;
  m_listTimer.start();
  emit pathChanged();
  emit errorStringChanged();
  emit listingChanged();
  emit listRequested(m_gen, m_path);
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
}

void DirectoryModel::onBatchReady(quint64 generation,
                                  const QVector<DirectoryEntry> &batch) {
  if (generation != m_gen)
    return;

  QVector<int> added;
  added.reserve(batch.size());
  const int oldAll = m_all.size();
  m_all.reserve(oldAll + batch.size());
  for (int i = 0; i < batch.size(); ++i) {
    const DirectoryEntry &e = batch.at(i);
    const int allIndex = oldAll + i;
    m_indexByName.insert(e.name, allIndex);
    if (m_showHidden || !e.isHidden)
      added.append(allIndex);
  }
  m_all += batch;

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
  if (it == m_indexByName.cend())
    return;
  const DirectoryEntry &e = m_all.at(it.value());
  if (e.dirKind == QLatin1String("pending"))
    return;
  const bool isDir = e.isDir;
  const QString dest = e.path;
  m_pendingActivate.clear();
  if (isDir)
    setPath(dest);
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
}
