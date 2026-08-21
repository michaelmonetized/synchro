#include "FilterProxy.h"

#include "DirectoryModel.h"

#include <QDir>
#include <QMimeDatabase>

FilterProxy::FilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setSortCaseSensitivity(Qt::CaseInsensitive);
  applySort();
}

void FilterProxy::setDirectoryModel(DirectoryModel *model) {
  bindSource(model);
}

DirectoryModel *FilterProxy::directoryModel() const {
  return qobject_cast<DirectoryModel *>(sourceModel());
}

void FilterProxy::bindSource(DirectoryModel *model) {
  auto *current = directoryModel();
  if (current == model)
    return;
  if (current)
    disconnect(current, nullptr, this, nullptr);
  setSourceModel(model);
  if (!model)
    return;
  connect(model, &DirectoryModel::currentIndexChanged, this, [this] {
    if (!m_syncing)
      emit currentIndexChanged();
  });
  connect(model, &DirectoryModel::countChanged, this,
          &FilterProxy::countChanged);
  connect(this, &QAbstractItemModel::rowsInserted, this, [this] {
    snapCursorIfHidden();
    emit countChanged();
  });
  connect(this, &QAbstractItemModel::rowsRemoved, this, [this] {
    snapCursorIfHidden();
    emit countChanged();
  });
  connect(this, &QAbstractItemModel::modelReset, this, [this] {
    snapCursorIfHidden();
    emit countChanged();
    emit currentIndexChanged();
  });
  connect(model, &DirectoryModel::pathChanged, this, [this] { applySort(); });
  applySort();
}

void FilterProxy::setPortalRules(const QVector<PortalFilterRule> &rules) {
  if (m_portalRules.size() == rules.size()) {
    bool same = true;
    for (int i = 0; i < rules.size(); ++i) {
      if (m_portalRules.at(i).type != rules.at(i).type ||
          m_portalRules.at(i).pattern != rules.at(i).pattern) {
        same = false;
        break;
      }
    }
    if (same)
      return;
  }
  beginFilterChange();
  m_portalRules = rules;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  snapCursorIfHidden();
  emit countChanged();
  emit currentIndexChanged();
}

void FilterProxy::setFilter(const QString &filter) {
  if (m_filter == filter)
    return;
  auto *dm = directoryModel();
  const QString keep = dm ? dm->currentName() : QString();
  beginFilterChange();
  m_filter = filter;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  int next = -1;
  if (!keep.isEmpty() && dm && dm->currentIndex() >= 0) {
    // The source cursor row is unchanged; if it still passes the filter,
    // one mapFromSource finds it (the old linear scan boxed every name).
    const QModelIndex mapped =
        mapFromSource(dm->index(dm->currentIndex(), 0));
    if (mapped.isValid())
      next = mapped.row();
  }
  if (next >= 0)
    setCurrentIndex(next);
  else if (rowCount() > 0)
    setCurrentIndex(0);
  emit filterChanged();
  emit countChanged();
  emit currentIndexChanged();
}

bool FilterProxy::kindAccepts(bool isDir) const {
  if (m_kindFilter == QLatin1String("files"))
    return !isDir;
  if (m_kindFilter == QLatin1String("folders"))
    return isDir;
  return true;
}

// All / Files / Folders: session-sticky (never persisted), so a fresh
// launch always shows everything.
void FilterProxy::setKindFilter(const QString &kind) {
  QString next = QStringLiteral("all");
  if (kind == QLatin1String("files") || kind == QLatin1String("folders"))
    next = kind;
  if (m_kindFilter == next)
    return;
  auto *dm = directoryModel();
  beginFilterChange();
  m_kindFilter = next;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  int nextRow = -1;
  if (dm && dm->currentIndex() >= 0) {
    const QModelIndex mapped = mapFromSource(dm->index(dm->currentIndex(), 0));
    if (mapped.isValid())
      nextRow = mapped.row();
  }
  if (nextRow >= 0)
    setCurrentIndex(nextRow);
  else if (rowCount() > 0)
    setCurrentIndex(0);
  emit kindFilterChanged();
  emit countChanged();
  emit currentIndexChanged();
}

int FilterProxy::currentIndex() const {
  auto *dm = directoryModel();
  if (!dm || dm->currentIndex() < 0)
    return -1;
  const QModelIndex mapped = mapFromSource(dm->index(dm->currentIndex(), 0));
  return mapped.isValid() ? mapped.row() : -1;
}

void FilterProxy::setCurrentIndex(int proxyRow) {
  auto *dm = directoryModel();
  if (!dm)
    return;
  if (rowCount() == 0) {
    emit currentIndexChanged();
    return;
  }
  const int next = qBound(0, proxyRow, rowCount() - 1);
  const QModelIndex src = mapToSource(index(next, 0));
  if (!src.isValid())
    return;
  if (dm->currentIndex() == src.row() && currentIndex() == next)
    return;
  m_syncing = true;
  dm->setCurrentIndex(src.row());
  m_syncing = false;
  emit currentIndexChanged();
}

void FilterProxy::selectRow(int proxyRow) { setCurrentIndex(proxyRow); }

void FilterProxy::moveCursor(int delta) {
  if (rowCount() == 0)
    return;
  int cur = currentIndex();
  if (cur < 0)
    setCurrentIndex(0);
  else
    setCurrentIndex(cur + delta);
}

void FilterProxy::activateCurrent() {
  auto *dm = directoryModel();
  if (!dm)
    return;
  const int proxyRow = currentIndex();
  if (proxyRow < 0)
    return;
  setCurrentIndex(proxyRow);
  dm->activateCurrent();
}

QString FilterProxy::currentName() const {
  const int row = currentIndex();
  if (row < 0)
    return {};
  return data(index(row, 0), DirectoryModel::NameRole).toString();
}

QVariantMap FilterProxy::rowMap(int proxyRow) const {
  auto *dm = directoryModel();
  if (!dm)
    return {};
  const QModelIndex src = mapToSource(index(proxyRow, 0));
  if (!src.isValid())
    return {};
  return dm->rowMap(src.row());
}

void FilterProxy::requestVisibleThumbs(int first, int last, int sizePx) {
  auto *dm = directoryModel();
  if (!dm)
    return;
  const int rows = rowCount();
  QVector<int> srcRows;
  if (rows > 0 && last >= 0) {
    first = qBound(0, first, rows - 1);
    last = qBound(0, last, rows - 1);
    if (last < first)
      qSwap(first, last);
    srcRows.reserve(last - first + 1);
    for (int i = first; i <= last; ++i) {
      const QModelIndex src = mapToSource(index(i, 0));
      if (src.isValid())
        srcRows.append(src.row());
    }
  }
  dm->requestSourceThumbs(srcRows, sizePx);
}

int FilterProxy::seekPrefix(const QString &prefix) {
  if (prefix.isEmpty() || rowCount() == 0)
    return -1;
  auto *dm = directoryModel();
  for (int i = 0; i < rowCount(); ++i) {
    const QModelIndex src = mapToSource(index(i, 0));
    const DirectoryEntry *e =
        dm && src.isValid() ? dm->entryAt(src.row()) : nullptr;
    const QString name =
        e ? e->name
          : data(index(i, 0), DirectoryModel::NameRole).toString();
    if (name.startsWith(prefix, Qt::CaseInsensitive)) {
      setCurrentIndex(i);
      return i;
    }
  }
  return -1;
}

int FilterProxy::roleFromName(const QString &name) {
  if (name == QLatin1String("size"))
    return DirectoryModel::SizeRole;
  if (name == QLatin1String("mtime"))
    return DirectoryModel::MtimeRole;
  if (name == QLatin1String("type"))
    return DirectoryModel::MimeRole;
  return DirectoryModel::NameRole;
}

bool FilterProxy::keepSourceOrder() const {
  auto *dm = directoryModel();
  if (!dm)
    return false;
  // Recents is newest-first; search keeps fd order. Do not persist a
  // different global sort when entering those views.
  return dm->isRecent() || DirectoryModel::isSearchPath(dm->path());
}

void FilterProxy::applySort() {
  if (keepSourceOrder()) {
    sort(-1);
    return;
  }
  setSortRole(roleFromName(m_sortRole));
  sort(0, m_sortOrder == QLatin1String("desc") ? Qt::DescendingOrder
                                               : Qt::AscendingOrder);
}

void FilterProxy::setSortRoleName(const QString &role) {
  QString next = QStringLiteral("name");
  if (role == QLatin1String("size") || role == QLatin1String("mtime") ||
      role == QLatin1String("type"))
    next = role;
  if (m_sortRole == next)
    return;
  m_sortRole = next;
  applySort();
  emit sortChanged();
}

void FilterProxy::setSortOrder(const QString &order) {
  const QString next =
      order == QLatin1String("desc") ? QStringLiteral("desc")
                                     : QStringLiteral("asc");
  if (m_sortOrder == next)
    return;
  m_sortOrder = next;
  applySort();
  emit sortChanged();
}

bool FilterProxy::lessThan(const QModelIndex &left,
                           const QModelIndex &right) const {
  // Hot path: no data()/QVariant round trips, and name order comes from
  // collation keys precomputed in the model (locale-aware, numeric,
  // case-insensitive) — 313ms -> ~23ms for a 50k-entry sort.
  auto *dm = directoryModel();
  const DirectoryEntry *l = dm ? dm->entryAt(left.row()) : nullptr;
  const DirectoryEntry *r = dm ? dm->entryAt(right.row()) : nullptr;
  if (!l || !r)
    return QSortFilterProxyModel::lessThan(left, right);
  if (l->isDir != r->isDir) {
    // Invert the dir bias when Qt flips lessThan for DescendingOrder.
    return QSortFilterProxyModel::sortOrder() == Qt::DescendingOrder
               ? r->isDir
               : l->isDir;
  }
  const int role = sortRole();
  if (role == DirectoryModel::SizeRole) {
    if (l->size != r->size)
      return l->size < r->size;
  } else if (role == DirectoryModel::MtimeRole) {
    if (l->mtime != r->mtime)
      return l->mtime < r->mtime;
  } else if (role == DirectoryModel::MimeRole) {
    // Sort the Type column by what the user reads, not the raw mime id.
    const QString &la = l->typeLabel.isEmpty() ? l->mime : l->typeLabel;
    const QString &ra = r->typeLabel.isEmpty() ? r->mime : r->typeLabel;
    const int cmp = QString::compare(la, ra, Qt::CaseInsensitive);
    if (cmp != 0)
      return cmp < 0;
  }
  return dm->compareNamesForRows(left.row(), right.row()) < 0;
}

bool FilterProxy::matchesPortal(const QString &name, const QString &path,
                                bool isDir, const QString &mime) const {
  if (m_portalRules.isEmpty())
    return true;
  if (isDir)
    return true;
  QString resolved = mime;
  if (resolved.isEmpty()) {
    const QString hint = path.isEmpty() ? name : path;
    resolved = QMimeDatabase()
                   .mimeTypeForFile(hint, QMimeDatabase::MatchExtension)
                   .name();
  }
  for (const PortalFilterRule &rule : m_portalRules) {
    if (rule.type == 0) {
      if (QDir::match(rule.pattern, name))
        return true;
    } else if (rule.type == 1) {
      if (rule.pattern.endsWith(QLatin1String("/*"))) {
        const QString prefix = rule.pattern.left(rule.pattern.size() - 1);
        if (resolved.startsWith(prefix, Qt::CaseInsensitive))
          return true;
      } else if (resolved.compare(rule.pattern, Qt::CaseInsensitive) == 0) {
        return true;
      }
    }
  }
  return false;
}

bool FilterProxy::filterAcceptsRow(int sourceRow,
                                   const QModelIndex &sourceParent) const {
  if (sourceParent.isValid())
    return false;
  auto *dm = directoryModel();
  if (dm) {
    // entryAt covers both directory and search listings without boxing
    // four roles through QVariant per row per keystroke.
    const DirectoryEntry *e = dm->entryAt(sourceRow);
    if (e) {
      if (!kindAccepts(e->isDir))
        return false;
      if (!matchesPortal(e->name, e->path, e->isDir, e->mime))
        return false;
      if (m_filter.isEmpty())
        return true;
      return e->name.contains(m_filter, Qt::CaseInsensitive);
    }
  }
  const QAbstractItemModel *src = sourceModel();
  if (!src)
    return false;
  const QModelIndex idx = src->index(sourceRow, 0, sourceParent);
  const QString name = src->data(idx, DirectoryModel::NameRole).toString();
  const bool isDir = src->data(idx, DirectoryModel::IsDirRole).toBool();
  const QString path = src->data(idx, DirectoryModel::PathRole).toString();
  const QString mime = src->data(idx, DirectoryModel::MimeRole).toString();
  if (!kindAccepts(isDir))
    return false;
  if (!matchesPortal(name, path, isDir, mime))
    return false;
  if (m_filter.isEmpty())
    return true;
  return name.contains(m_filter, Qt::CaseInsensitive);
}

void FilterProxy::snapCursorIfHidden() {
  if (m_syncing)
    return;
  if (rowCount() == 0)
    return;
  if (currentIndex() < 0)
    setCurrentIndex(0);
}

FilesOnlyProxy::FilesOnlyProxy(QObject *parent)
    : QSortFilterProxyModel(parent) {
  connect(this, &QAbstractItemModel::modelReset, this,
          &FilesOnlyProxy::countChanged);
  connect(this, &QAbstractItemModel::rowsInserted, this,
          &FilesOnlyProxy::countChanged);
  connect(this, &QAbstractItemModel::rowsRemoved, this,
          &FilesOnlyProxy::countChanged);
}

void FilesOnlyProxy::setListing(FilterProxy *src) {
  if (m_listing == src)
    return;
  if (m_listing)
    disconnect(m_listing, nullptr, this, nullptr);
  m_listing = src;
  setSourceModel(src);
  if (m_listing) {
    connect(m_listing, &FilterProxy::currentIndexChanged, this,
            &FilesOnlyProxy::currentIndexChanged);
  }
  emit currentIndexChanged();
  emit countChanged();
}

int FilesOnlyProxy::currentIndex() const {
  if (!m_listing)
    return -1;
  const int srcRow = m_listing->currentIndex();
  if (srcRow < 0)
    return -1;
  const QModelIndex mapped = mapFromSource(m_listing->index(srcRow, 0));
  return mapped.isValid() ? mapped.row() : -1;
}

void FilesOnlyProxy::setCurrentIndex(int proxyRow) {
  if (!m_listing || rowCount() == 0)
    return;
  const int next = qBound(0, proxyRow, rowCount() - 1);
  const QModelIndex src = mapToSource(index(next, 0));
  if (!src.isValid())
    return;
  m_listing->setCurrentIndex(src.row());
}

void FilesOnlyProxy::requestVisibleThumbs(int first, int last, int sizePx) {
  if (!m_listing || rowCount() <= 0)
    return;
  first = qBound(0, first, rowCount() - 1);
  last = qBound(0, last, rowCount() - 1);
  if (last < first)
    qSwap(first, last);
  const QModelIndex a = mapToSource(index(first, 0));
  const QModelIndex b = mapToSource(index(last, 0));
  if (!a.isValid() || !b.isValid())
    return;
  m_listing->requestVisibleThumbs(qMin(a.row(), b.row()),
                                  qMax(a.row(), b.row()), sizePx);
}

bool FilesOnlyProxy::filterAcceptsRow(int sourceRow,
                                      const QModelIndex &sourceParent) const {
  if (sourceParent.isValid() || !sourceModel())
    return false;
  return !sourceModel()
              ->data(sourceModel()->index(sourceRow, 0),
                     DirectoryModel::IsDirRole)
              .toBool();
}
