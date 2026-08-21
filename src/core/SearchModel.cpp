#include "SearchModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <cstdio>

namespace {

QVariantMap toMap(const DirectoryEntry &e) {
  QVariantMap m;
  m.insert(QStringLiteral("name"), e.name);
  m.insert(QStringLiteral("path"), e.path);
  m.insert(QStringLiteral("uri"), e.uri);
  m.insert(QStringLiteral("isDir"), e.isDir);
  m.insert(QStringLiteral("size"), e.size);
  m.insert(QStringLiteral("mtime"), e.mtime);
  m.insert(QStringLiteral("mime"), e.mime);
  m.insert(QStringLiteral("isSymlink"), e.isSymlink);
  return m;
}

} // namespace

SearchFolderModel::SearchFolderModel(QObject *parent)
    : QAbstractListModel(parent) {}

int SearchFolderModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_groups.size();
}

QHash<int, QByteArray> SearchFolderModel::roleNames() const {
  return {
      {PathRole, "path"},
      {LabelRole, "label"},
      {FirstRole, "first"},
      {CountRole, "count"},
  };
}

QVariant SearchFolderModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
    return {};
  const Group &g = m_groups.at(index.row());
  switch (role) {
  case PathRole:
    return g.path;
  case LabelRole:
    return g.label;
  case FirstRole:
    return g.first;
  case CountRole:
    return g.count;
  default:
    return {};
  }
}

void SearchFolderModel::clear() {
  if (m_groups.isEmpty())
    return;
  beginResetModel();
  m_groups.clear();
  endResetModel();
}

void SearchFolderModel::noteInsert(int entryRow, const QString &parentPath,
                                   const QString &label) {
  int g = 0;
  for (; g < m_groups.size(); ++g) {
    if (m_groups.at(g).path == parentPath) {
      ++m_groups[g].count;
      const QModelIndex idx = index(g);
      emit dataChanged(idx, idx, {CountRole});
      for (int i = g + 1; i < m_groups.size(); ++i) {
        ++m_groups[i].first;
        const QModelIndex later = index(i);
        emit dataChanged(later, later, {FirstRole});
      }
      return;
    }
    if (QString::compare(parentPath, m_groups.at(g).path) < 0)
      break;
  }
  beginInsertRows(QModelIndex(), g, g);
  Group ng;
  ng.path = parentPath;
  ng.label = label;
  ng.first = entryRow;
  ng.count = 1;
  m_groups.insert(g, std::move(ng));
  endInsertRows();
  for (int i = g + 1; i < m_groups.size(); ++i) {
    ++m_groups[i].first;
    const QModelIndex later = index(i);
    emit dataChanged(later, later, {FirstRole});
  }
}

int SearchFolderModel::stepVisual(int index, int dx, int dy,
                                  int columns) const {
  if (m_groups.isEmpty())
    return index;
  const int cols = qMax(1, columns);
  int n = 0;
  for (const Group &g : m_groups)
    n += g.count;
  if (n <= 0)
    return index;
  int cur = index;
  if (cur < 0)
    cur = 0;
  if (dx != 0)
    cur = qBound(0, cur + dx, n - 1);

  const int dir = dy > 0 ? 1 : -1;
  const int steps = qAbs(dy);
  for (int s = 0; s < steps; ++s) {
    int gi = -1;
    for (int i = 0; i < m_groups.size(); ++i) {
      const Group &g = m_groups.at(i);
      if (cur >= g.first && cur < g.first + g.count) {
        gi = i;
        break;
      }
    }
    if (gi < 0)
      break;
    const Group &g = m_groups.at(gi);
    const int local = cur - g.first;
    const int col = local % cols;
    const int row = local / cols;
    const int rows = (g.count + cols - 1) / cols;
    int next = cur;
    if (dir > 0) {
      if (row + 1 < rows) {
        const int cand = (row + 1) * cols + col;
        if (cand < g.count) {
          next = g.first + cand;
        } else if (gi + 1 < m_groups.size()) {
          const Group &ng = m_groups.at(gi + 1);
          next = ng.first + qMin(col, ng.count - 1);
        }
      } else if (gi + 1 < m_groups.size()) {
        const Group &ng = m_groups.at(gi + 1);
        next = ng.first + qMin(col, ng.count - 1);
      }
    } else {
      if (row > 0) {
        next = g.first + (row - 1) * cols + col;
      } else if (gi > 0) {
        const Group &pg = m_groups.at(gi - 1);
        const int prow = (pg.count + cols - 1) / cols - 1;
        for (int r = prow; r >= 0; --r) {
          const int cand = r * cols + col;
          if (cand < pg.count) {
            next = pg.first + cand;
            break;
          }
        }
      }
    }
    if (next == cur)
      break;
    cur = next;
  }
  return cur;
}

QVariantList SearchFolderModel::toVariantList() const {
  QVariantList out;
  out.reserve(m_groups.size());
  for (const Group &g : m_groups) {
    QVariantMap m;
    m.insert(QStringLiteral("path"), g.path);
    m.insert(QStringLiteral("label"), g.label);
    m.insert(QStringLiteral("first"), g.first);
    m.insert(QStringLiteral("count"), g.count);
    out.append(m);
  }
  return out;
}

SearchModel::SearchModel(QObject *parent)
    : QAbstractListModel(parent), m_folderModel(new SearchFolderModel(this)) {
  connect(&m_service, &SearchService::hit, this, &SearchModel::onHit);
  connect(&m_service, &SearchService::finished, this, &SearchModel::onFinished);
}

QVariantList SearchModel::folderGroups() const {
  return m_folderModel->toVariantList();
}

int SearchModel::rowCount(const QModelIndex &parent) const {
  if (parent.isValid())
    return 0;
  return m_entries.size();
}

QHash<int, QByteArray> SearchModel::roleNames() const {
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
  };
}

QVariant SearchModel::data(const QModelIndex &index, int role) const {
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
    return QString();
  case DetailRole:
    return e->detail;
  case UsedRole:
    return e->used;
  case TotalRole:
    return e->total;
  case PercentRole:
    return e->percent;
  case ParentPathRole:
    return e->parentPath;
  case ParentLabelRole:
    return folderLabel(e->parentPath, m_root);
  default:
    return {};
  }
}

const DirectoryEntry *SearchModel::entryAt(int row) const {
  if (row < 0 || row >= m_entries.size())
    return nullptr;
  return &m_entries.at(row);
}

QVariantMap SearchModel::cachedStat(const QString &path) const {
  const auto it = m_indexByPath.constFind(path);
  if (it == m_indexByPath.cend())
    return {};
  return toMap(m_entries.at(it.value()));
}

void SearchModel::start(const QString &query, const QString &root, bool hidden,
                        bool content) {
  m_service.cancel();
  if (m_query != query) {
    m_query = query;
    emit queryChanged();
  }
  const QString absRoot = SearchService::resolveRoot(root);
  if (m_root != absRoot) {
    m_root = absRoot;
    emit rootChanged();
  }
  if (m_hidden != hidden) {
    m_hidden = hidden;
    emit showHiddenChanged();
  }
  if (m_content != content) {
    m_content = content;
    emit kindChanged();
  }
  setError(QString());
  m_loggedFirst = false;
  m_lastFirstRowsMs = -1;
  // Drop the previous query immediately. Waiting for the first new hit
  // left the old rows on screen; ListView sections then painted both.
  resetEntries();
  if (query.isEmpty() || absRoot.isEmpty()) {
    setListing(false);
    return;
  }
  setListing(true);
  emit pathChanged();
  m_service.start(query, absRoot, hidden,
                  content ? SearchService::Kind::Content
                          : SearchService::Kind::Name);
}

void SearchModel::cancel() {
  m_service.cancel();
  setListing(false);
}

void SearchModel::clear() {
  m_service.cancel();
  resetEntries();
  if (!m_query.isEmpty()) {
    m_query.clear();
    emit queryChanged();
  }
  setListing(false);
  setError(QString());
}

void SearchModel::setShowHidden(bool hidden) {
  if (m_hidden == hidden)
    return;
  m_hidden = hidden;
  emit showHiddenChanged();
  if (!m_query.isEmpty() && !m_root.isEmpty())
    start(m_query, m_root, m_hidden, m_content);
}

void SearchModel::setCurrentIndex(int index) {
  int next = index;
  if (m_entries.isEmpty())
    next = -1;
  else
    next = qBound(0, index, m_entries.size() - 1);
  if (m_currentIndex == next)
    return;
  m_currentIndex = next;
  emit currentIndexChanged();
}

void SearchModel::moveCursor(int delta) {
  if (m_entries.isEmpty())
    return;
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  else
    setCurrentIndex(m_currentIndex + delta);
}

void SearchModel::activateCurrent() {
  const DirectoryEntry *e = entryAt(m_currentIndex);
  if (!e || e->path.isEmpty())
    return;
  if (e->isDir)
    emit navigateRequested(e->path);
  else
    emit fileActivated(e->path, e->mime);
}

QString SearchModel::currentName() const {
  const DirectoryEntry *e = entryAt(m_currentIndex);
  return e ? e->name : QString();
}

bool SearchModel::currentIsDir() const {
  const DirectoryEntry *e = entryAt(m_currentIndex);
  return e && e->isDir;
}

void SearchModel::setThumbnail(const QString &path, const QString &url) {
  if (path.isEmpty() || url.isEmpty())
    return;
  const auto it = m_indexByPath.constFind(path);
  if (it == m_indexByPath.cend())
    return;
  const int row = it.value();
  if (m_entries.at(row).thumbnail == url)
    return;
  m_entries[row].thumbnail = url;
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx, {ThumbnailRole});
}

void SearchModel::onHit(const QString &path) {
  const int cap = m_content ? SearchService::kMaxContentResults
                            : SearchService::kMaxResults;
  if (m_entries.size() >= cap)
    return;
  DirectoryEntry e = makeEntry(path);
  if (e.path.isEmpty() || m_indexByPath.contains(e.path))
    return;
  int lo = 0;
  int hi = m_entries.size();
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    const DirectoryEntry &cur = m_entries.at(mid);
    const int byFolder = QString::compare(e.parentPath, cur.parentPath);
    if (byFolder < 0 ||
        (byFolder == 0 &&
         QString::localeAwareCompare(e.name, cur.name) < 0))
      hi = mid;
    else
      lo = mid + 1;
  }
  const int row = lo;
  beginInsertRows(QModelIndex(), row, row);
  m_entries.insert(row, std::move(e));
  for (int i = row; i < m_entries.size(); ++i)
    m_indexByPath.insert(m_entries.at(i).path, i);
  endInsertRows();
  const int groupsBefore = m_folderModel->rowCount();
  m_folderModel->noteInsert(row, m_entries.at(row).parentPath,
                            folderLabel(m_entries.at(row).parentPath, m_root));
  if (m_folderModel->rowCount() != groupsBefore)
    emit folderGroupsChanged();
  emit countChanged();
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  if (!m_loggedFirst) {
    m_loggedFirst = true;
    m_lastFirstRowsMs = m_service.firstLineMs();
    std::fprintf(stderr, "synchro: search %s%s root=%s first_rows %d %lldms%s\n",
                 m_content ? "??" : "?", qPrintable(m_query),
                 qPrintable(m_root), static_cast<int>(m_entries.size()),
                 static_cast<long long>(m_lastFirstRowsMs),
                 m_lastFirstRowsMs > 200 ? " SLOW" : "");
    emit firstRowsInserted(m_lastFirstRowsMs, m_entries.size());
  }
}

void SearchModel::onFinished(bool ok, const QString &error) {
  setListing(false);
  if (!ok)
    setError(error);
}

void SearchModel::resetEntries() {
  beginResetModel();
  m_entries.clear();
  m_indexByPath.clear();
  m_currentIndex = -1;
  endResetModel();
  m_folderModel->clear();
  emit folderGroupsChanged();
  emit countChanged();
  emit currentIndexChanged();
}

QVariantMap SearchModel::rowMap(int row) const {
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
  m.insert(QStringLiteral("parentLabel"), folderLabel(e->parentPath, m_root));
  return m;
}

void SearchModel::setListing(bool on) {
  if (m_listing == on)
    return;
  m_listing = on;
  emit listingChanged();
}

void SearchModel::setError(const QString &error) {
  if (m_error == error)
    return;
  m_error = error;
  emit errorStringChanged();
}

QString SearchModel::folderLabel(const QString &parentPath,
                                 const QString &root) {
  if (parentPath.isEmpty())
    return {};
  if (!root.isEmpty() && parentPath == root)
    return QStringLiteral("this folder");
  if (!root.isEmpty() && parentPath.startsWith(root + QLatin1Char('/')))
    return parentPath.mid(root.size() + 1);
  const QString home = QDir::homePath();
  if (parentPath == home)
    return QStringLiteral("~");
  if (parentPath.startsWith(home + QLatin1Char('/')))
    return QLatin1Char('~') + parentPath.mid(home.size());
  return parentPath;
}

DirectoryEntry SearchModel::makeEntry(const QString &path) const {
  DirectoryEntry e;
  const QFileInfo fi(path);
  e.name = fi.fileName();
  e.path = fi.absoluteFilePath();
  if (e.path.isEmpty())
    e.path = path;
  e.uri = QUrl::fromLocalFile(e.path);
  e.isDir = fi.isDir();
  e.isSymlink = fi.isSymLink();
  e.isHidden = !e.name.isEmpty() && e.name[0] == QLatin1Char('.');
  e.size = e.isDir ? -1 : fi.size();
  e.mtime = fi.lastModified().toMSecsSinceEpoch();
  e.dirKind = QStringLiteral("search");
  e.parentPath = QFileInfo(e.path).absolutePath();
  if (e.isDir) {
    e.mime = QStringLiteral("inode/directory");
    e.iconName = QStringLiteral("folder");
    return e;
  }
  e.iconName = QStringLiteral("text-x-generic");
  return e;
}


