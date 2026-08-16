#include "FilterProxy.h"

#include "DirectoryModel.h"

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

void FilterProxy::setFilter(const QString &filter) {
  if (m_filter == filter)
    return;
  auto *dm = directoryModel();
  const QString keep = dm ? dm->currentName() : QString();
  beginFilterChange();
  m_filter = filter;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
  int next = -1;
  if (!keep.isEmpty()) {
    for (int i = 0; i < rowCount(); ++i) {
      if (data(index(i, 0), DirectoryModel::NameRole).toString() == keep) {
        next = i;
        break;
      }
    }
  }
  if (next >= 0)
    setCurrentIndex(next);
  else if (rowCount() > 0)
    setCurrentIndex(0);
  emit filterChanged();
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

int FilterProxy::seekPrefix(const QString &prefix) {
  if (prefix.isEmpty() || rowCount() == 0)
    return -1;
  for (int i = 0; i < rowCount(); ++i) {
    const QString name = data(index(i, 0), DirectoryModel::NameRole).toString();
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
  const QAbstractItemModel *src = sourceModel();
  if (!src)
    return QSortFilterProxyModel::lessThan(left, right);
  const bool ld = src->data(left, DirectoryModel::IsDirRole).toBool();
  const bool rd = src->data(right, DirectoryModel::IsDirRole).toBool();
  if (ld != rd) {
    // Invert the dir bias when Qt flips lessThan for DescendingOrder.
    return QSortFilterProxyModel::sortOrder() == Qt::DescendingOrder ? rd
                                                                     : ld;
  }
  const int role = sortRole();
  if (role == DirectoryModel::SizeRole || role == DirectoryModel::MtimeRole) {
    const qint64 a = src->data(left, role).toLongLong();
    const qint64 b = src->data(right, role).toLongLong();
    if (a != b)
      return a < b;
  } else if (role == DirectoryModel::MimeRole) {
    const QString a = src->data(left, DirectoryModel::MimeRole).toString();
    const QString b = src->data(right, DirectoryModel::MimeRole).toString();
    const int cmp = QString::compare(a, b, Qt::CaseInsensitive);
    if (cmp != 0)
      return cmp < 0;
  }
  const QString an = src->data(left, DirectoryModel::NameRole).toString();
  const QString bn = src->data(right, DirectoryModel::NameRole).toString();
  return QString::localeAwareCompare(an.toLower(), bn.toLower()) < 0;
}

bool FilterProxy::filterAcceptsRow(int sourceRow,
                                   const QModelIndex &sourceParent) const {
  if (sourceParent.isValid())
    return false;
  if (m_filter.isEmpty())
    return true;
  const QAbstractItemModel *src = sourceModel();
  if (!src)
    return false;
  const QString name = src->data(src->index(sourceRow, 0, sourceParent),
                                 DirectoryModel::NameRole)
                           .toString();
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
