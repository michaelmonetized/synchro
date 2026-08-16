#include "SearchModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
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

SearchModel::SearchModel(QObject *parent) : QAbstractListModel(parent) {
  connect(&m_service, &SearchService::hit, this, &SearchModel::onHit);
  connect(&m_service, &SearchService::finished, this, &SearchModel::onFinished);
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

void SearchModel::start(const QString &query, const QString &root,
                        bool hidden) {
  m_service.cancel();
  resetEntries();
  if (m_query != query) {
    m_query = query;
    emit queryChanged();
  }
  if (m_root != root) {
    m_root = root;
    emit rootChanged();
  }
  if (m_hidden != hidden) {
    m_hidden = hidden;
    emit showHiddenChanged();
  }
  setError(QString());
  m_loggedFirst = false;
  m_lastFirstRowsMs = -1;
  if (query.isEmpty() || root.isEmpty()) {
    setListing(false);
    return;
  }
  setListing(true);
  emit pathChanged();
  m_service.start(query, root, hidden);
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
    start(m_query, m_root, m_hidden);
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
  if (m_entries.size() >= SearchService::kMaxResults)
    return;
  DirectoryEntry e = makeEntry(path);
  if (e.path.isEmpty() || m_indexByPath.contains(e.path))
    return;
  const int row = m_entries.size();
  beginInsertRows(QModelIndex(), row, row);
  m_indexByPath.insert(e.path, row);
  m_entries.append(std::move(e));
  endInsertRows();
  emit countChanged();
  if (m_currentIndex < 0)
    setCurrentIndex(0);
  if (!m_loggedFirst) {
    m_loggedFirst = true;
    m_lastFirstRowsMs = m_service.firstLineMs();
    std::fprintf(stderr, "synchro: search %s first_rows %d %lldms%s\n",
                 qPrintable(m_query), static_cast<int>(m_entries.size()),
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
  emit countChanged();
  emit currentIndexChanged();
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
  if (e.isDir) {
    e.mime = QStringLiteral("inode/directory");
    e.iconName = QStringLiteral("folder");
    return e;
  }
  QMimeDatabase db;
  const QMimeType mime =
      db.mimeTypeForFile(e.path, QMimeDatabase::MatchExtension);
  e.mime = mime.name();
  e.iconName = mime.genericIconName();
  if (e.iconName.isEmpty())
    e.iconName = mime.iconName();
  if (e.iconName.isEmpty())
    e.iconName = e.isSymlink ? QStringLiteral("emblem-symbolic-link")
                             : QStringLiteral("text-x-generic");
  return e;
}


