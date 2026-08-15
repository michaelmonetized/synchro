#include "FilterProxy.h"

#include "DirectoryModel.h"

FilterProxy::FilterProxy(QObject *parent) : QSortFilterProxyModel(parent) {
  setDynamicSortFilter(true);
  setFilterCaseSensitivity(Qt::CaseInsensitive);
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
}

void FilterProxy::setFilter(const QString &filter) {
  if (m_filter == filter)
    return;
  const QString keep = currentName();
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
  if (next < 0 && rowCount() > 0)
    next = 0;
  if (next >= 0)
    setCurrentIndex(next);
  else if (auto *dm = directoryModel()) {
    m_syncing = true;
    dm->setCurrentIndex(-1);
    m_syncing = false;
    emit currentIndexChanged();
  }
  emit filterChanged();
  emit countChanged();
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
    if (dm->currentIndex() != -1) {
      m_syncing = true;
      dm->setCurrentIndex(-1);
      m_syncing = false;
      emit currentIndexChanged();
    }
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
