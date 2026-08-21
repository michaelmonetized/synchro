#include "HostApi.h"

#include "DirectoryModel.h"
#include "FileOpEngine.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "ArchiveMeta.h"
#include "DbPreview.h"
#include "ParquetMeta.h"
#include "SelectionModel.h"
#include "SyntaxHighlight.h"
#include "ThumbCache.h"
#include "ThumbnailService.h"
#include "XdgOpen.h"

#include <QAbstractItemModel>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QGuiApplication>
#include <QImageReader>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>

#include <cstdio>

HostApi::HostApi(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
                 HandlerRegistry *registry, HandlerLoader *loader, XdgOpen *xdg,
                 MimeMap *mime, QQmlEngine *engine, QObject *parent)
    : PeekHost(parent), m_model(model), m_proxy(proxy), m_nav(nav),
      m_registry(registry), m_loader(loader), m_xdg(xdg), m_mime(mime),
      m_engine(engine), m_actions(registry, &m_exec) {
  if (m_model) {
    connect(m_model, &DirectoryModel::pathChanged, this, [this] {
      close();
      closeAction();
      refreshListingChrome();
    });
    connect(m_model, &DirectoryModel::statsApplied, this,
            &HostApi::onStatsApplied);
    connect(m_model, &DirectoryModel::searchQueryChanged, this,
            &HostApi::peekFindQueryChanged);
  }
  refreshListingChrome();
}

void HostApi::onStatsApplied(const QStringList &paths) {
  // Handlers only exist inside an open preview; with nothing open there is
  // nobody to notify, and skipping keeps big listings cheap (the old
  // per-entry path built 50k QVariantMaps per directory).
  if (!m_open && !m_previewItem)
    return;
  for (const QString &path : paths)
    emit statReady(QUrl::fromLocalFile(path), m_model->cachedStat(path));
  if (m_open && !m_previewItem) {
    const QString current = currentItem().path;
    if (paths.contains(current))
      reloadPreview();
  }
}

Manifest::Item HostApi::itemAt(int proxyRow) const {
  Manifest::Item item;
  const QAbstractItemModel *src = m_proxy
                                      ? static_cast<QAbstractItemModel *>(m_proxy)
                                      : static_cast<QAbstractItemModel *>(m_model);
  if (!src || proxyRow < 0 || proxyRow >= src->rowCount())
    return item;
  const QModelIndex idx = src->index(proxyRow, 0);
  item.path = src->data(idx, DirectoryModel::PathRole).toString();
  item.uri = src->data(idx, DirectoryModel::UriRole).toUrl();
  item.mime = src->data(idx, DirectoryModel::MimeRole).toString();
  item.isDir = src->data(idx, DirectoryModel::IsDirRole).toBool();
  if (item.uri.isEmpty() && !item.path.isEmpty())
    item.uri = QUrl::fromLocalFile(item.path);
  if (item.mime.isEmpty() && m_mime && !item.path.isEmpty())
    item.mime = m_mime->mimeForFile(item.path);
  return item;
}

Manifest::Item HostApi::currentItem() const {
  if (m_proxy)
    return itemAt(m_proxy->currentIndex());
  if (m_model)
    return itemAt(m_model->currentIndex());
  return {};
}

QVector<Manifest::Item> HostApi::currentItems() const {
  if (m_sel && m_sel->selectedCount() > 1) {
    QVector<Manifest::Item> out;
    const QStringList paths = m_sel->selectedPaths();
    out.reserve(paths.size());
    for (int i = 0; i < paths.size(); ++i) {
      Manifest::Item item;
      item.path = paths.at(i);
      item.uri = QUrl::fromLocalFile(item.path);
      if (m_mime)
        item.mime = m_mime->mimeForFile(item.path);
      item.isDir = QFileInfo(item.path).isDir();
      out.append(item);
    }
    return out;
  }
  const Manifest::Item item = currentItem();
  if (item.path.isEmpty())
    return {};
  return {item};
}

void HostApi::setCurrentFromModel() {
  const Manifest::Item item = currentItem();
  const QUrl file = item.uri.isEmpty() ? QUrl::fromLocalFile(item.path)
                                       : item.uri;
  if (m_file != file) {
    m_file = file;
    emit fileChanged();
  }
  QVariantMap row;
  row.insert(QStringLiteral("path"), item.path);
  row.insert(QStringLiteral("uri"), item.uri);
  row.insert(QStringLiteral("mime"), item.mime);
  row.insert(QStringLiteral("isDir"), item.isDir);
  QVariantList sel;
  if (!item.path.isEmpty())
    sel.append(row);
  if (m_selection != sel) {
    m_selection = sel;
    emit selectionChanged();
  }
}

void HostApi::destroyFilePreview() {
  if (!m_filePreviewItem)
    return;
  if (m_previewItem == m_filePreviewItem)
    m_previewItem = m_folderPreviewItem;
  m_filePreviewItem->deleteLater();
  m_filePreviewItem = nullptr;
  emit previewItemChanged();
}

void HostApi::destroyPreview() {
  destroyFilePreview();
  if (m_folderPreviewItem) {
    if (m_previewItem == m_folderPreviewItem)
      m_previewItem = nullptr;
    m_folderPreviewItem->deleteLater();
    m_folderPreviewItem = nullptr;
    emit folderListingItemChanged();
  }
  if (m_previewItem) {
    m_previewItem->deleteLater();
    m_previewItem = nullptr;
  }
  m_loadedId.clear();
  emit previewItemChanged();
}

bool HostApi::loadPreviewFor(const Manifest::Item &item, QObject **slot) {
  if (!slot || !m_registry || !m_loader || item.path.isEmpty())
    return false;
  auto matches = m_registry->resolve(QStringLiteral("preview"), {item});
  if (matches.isEmpty() && !item.isDir &&
      MimeMap::isProbablyText(item.path, item.mime) &&
      m_registry->contains(QStringLiteral("synchro.preview.text"))) {
    HandlerRegistry::Match fallback;
    fallback.id = QStringLiteral("synchro.preview.text");
    matches.append(fallback);
  }
  if (matches.isEmpty())
    return false;
  const auto &best = matches.constFirst();
  if (*slot) {
    HandlerRegistry::Record rec = m_registry->handler(best.id);
    Q_UNUSED(rec);
    if (auto *qi = qobject_cast<QQuickItem *>(*slot)) {
      qi->setProperty("file", m_file);
      qi->setProperty("selection", m_selection);
    }
    return true;
  }
  HandlerRegistry::Record rec = m_registry->handler(best.id);
  QQuickItem *itemQml =
      m_loader->create(m_engine, rec, QStringLiteral("preview"), this, m_file,
                       m_selection);
  if (!itemQml) {
    std::fprintf(stderr, "synchro: peek %s: %s\n", qPrintable(best.id),
                 qPrintable(m_loader->lastError()));
    return false;
  }
  *slot = itemQml;
  m_loadedId = best.id;
  return true;
}

void HostApi::applyPeekSelection(const Manifest::Item &item) {
  const QUrl file =
      item.uri.isEmpty() ? QUrl::fromLocalFile(item.path) : item.uri;
  if (m_file != file) {
    m_file = file;
    emit fileChanged();
  }
  QVariantMap row;
  row.insert(QStringLiteral("path"), item.path);
  row.insert(QStringLiteral("uri"), item.uri);
  row.insert(QStringLiteral("mime"), item.mime);
  row.insert(QStringLiteral("isDir"), item.isDir);
  QVariantList sel;
  if (!item.path.isEmpty())
    sel.append(row);
  if (m_selection != sel) {
    m_selection = sel;
    emit selectionChanged();
  }
}

QUrl HostApi::panelSource(const QString &id) const {
  if (!m_registry || !m_registry->contains(id))
    return QUrl();
  const HandlerRegistry::Record rec = m_registry->handler(id);
  if (!rec.enabled || !rec.manifest.kinds.contains(QStringLiteral("panel")))
    return QUrl();
  const QString entry =
      rec.manifest.entryPoints.value(QStringLiteral("panel"));
  if (entry.isEmpty())
    return QUrl();
  const QString path = QDir(rec.sourceDir).filePath(entry);
  if (!QFileInfo::exists(path))
    return QUrl();
  return QUrl::fromLocalFile(path);
}

QString HostApi::processCwd(int pid) const {
  if (pid <= 0)
    return QString();
  return QFile::symLinkTarget(QStringLiteral("/proc/%1/cwd").arg(pid));
}

QString HostApi::defaultShell() const {
  const QString env = qEnvironmentVariable("SHELL");
  return env.isEmpty() ? QStringLiteral("/bin/bash") : env;
}

void HostApi::reloadPreview() {
  Manifest::Item item;
  if (!m_folderStack.isEmpty() && m_filePeekFromFolder)
    item = peekCurrentItem();
  else if (!m_folderStack.isEmpty()) {
    item.path = m_folderStack.constLast();
    item.uri = QUrl::fromLocalFile(item.path);
    item.mime = QStringLiteral("inode/directory");
    item.isDir = true;
  } else {
    setCurrentFromModel();
    item = currentItem();
  }

  if (m_folderStack.isEmpty()) {
    applyPeekSelection(item);
    if (!m_registry || item.path.isEmpty()) {
      destroyPreview();
      return;
    }
    const auto matches = m_registry->resolve(QStringLiteral("preview"), {item});
    if (matches.isEmpty()) {
      destroyPreview();
      return;
    }
    const auto &best = matches.constFirst();
    if (m_previewItem && m_loadedId == best.id) {
      if (auto *qi = qobject_cast<QQuickItem *>(m_previewItem)) {
        qi->setProperty("file", m_file);
        qi->setProperty("selection", m_selection);
      }
      return;
    }
    destroyPreview();
    QObject *slot = nullptr;
    if (!loadPreviewFor(item, &slot))
      return;
    m_previewItem = slot;
    emit previewItemChanged();
    return;
  }

  if (m_filePeekFromFolder)
    showPeekFile(item);
  else
    showFolderListing();
}

void HostApi::close() {
  if (!m_open && !m_previewItem)
    return;
  setPeekPreviewFocused(false);
  destroyPreview();
  endFolderPeek();
  if (!m_open)
    return;
  m_open = false;
  emit openChanged();
}

bool HostApi::openCurrent() {
  const Manifest::Item item = currentItem();
  if (item.path.isEmpty())
    return false;
  if (item.isDir) {
    startFolderPeek(item.path);
    return true;
  }
  endFolderPeek();
  if (!m_open) {
    m_open = true;
    emit openChanged();
  }
  reloadPreview();
  return true;
}

bool HostApi::toggle() {
  if (m_open) {
    close();
    return false;
  }
  return openCurrent();
}

void HostApi::step(int delta) {
  if (!m_open || delta == 0)
    return;
  if (!m_folderStack.isEmpty()) {
    if (m_filePeekFromFolder)
      stepPeekFiles(delta);
    else if (m_peekProxy)
      m_peekProxy->moveCursor(delta);
    return;
  }
  QAbstractItemModel *src = m_proxy
                                ? static_cast<QAbstractItemModel *>(m_proxy)
                                : static_cast<QAbstractItemModel *>(m_model);
  if (!src)
    return;
  const int count = src->rowCount();
  int cur = m_proxy ? m_proxy->currentIndex()
                    : (m_model ? m_model->currentIndex() : -1);
  if (cur < 0)
    return;
  for (int i = cur + delta; i >= 0 && i < count; i += delta) {
    const Manifest::Item item = itemAt(i);
    if (item.isDir)
      continue;
    if (m_proxy)
      m_proxy->setCurrentIndex(i);
    else if (m_model)
      m_model->setCurrentIndex(i);
    reloadPreview();
    return;
  }
}

void HostApi::openExternal(const QUrl &url) {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  if (path.isEmpty())
    return;
  QString mime;
  if (m_mime)
    mime = m_mime->mimeForFile(path);
  if (!openFile(path, mime)) {
    std::fprintf(stderr, "synchro: openExternal %s: %s\n", qPrintable(path),
                 qPrintable(m_error));
  }
}

void HostApi::reveal(const QUrl &url) {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  if (path.isEmpty() || !m_model)
    return;
  const QFileInfo fi(path);
  close();
  m_model->setPath(fi.absolutePath(), fi.fileName());
}

void HostApi::navigate(const QUrl &url) {
  if (!m_model)
    return;
  close();
  if (url.isLocalFile())
    m_model->setPath(url.toLocalFile());
  else if (!url.scheme().isEmpty() && url.scheme() != QLatin1String("file"))
    m_model->setPath(url.toString());
  else
    m_model->setPath(url.toString());
}

void HostApi::setTitle(const QString &title) {
  if (m_title == title)
    return;
  m_title = title;
  emit titleChanged();
}

QVariantMap HostApi::stat(const QUrl &url) const {
  if (!m_model)
    return {};
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  const QVariantMap cached = m_model->cachedStat(path);
  if (cached.isEmpty())
    m_model->requestStatPath(path);
  return cached;
}

void HostApi::registerSurface(QObject *surface) {
  if (!surface)
    return;
  connect(surface, SIGNAL(requestClose()), this, SLOT(close()),
          Qt::UniqueConnection);
}

void HostApi::refreshOpenCandidates() {
  const QVariantList next = m_actions.openCandidates(activeItems());
  if (m_openCandidates == next)
    return;
  m_openCandidates = next;
  emit openCandidatesChanged();
}

void HostApi::destroyActionItem() {
  if (!m_actionItem)
    return;
  m_actionItem->deleteLater();
  m_actionItem = nullptr;
  emit actionItemChanged();
}

void HostApi::destroyAction() {
  destroyActionItem();
  destroyDoContent();
  const bool wasOpen = m_actionOpen;
  m_doItems.clear();
  m_doSelection.clear();
  m_doVerbs.clear();
  m_doIndex = -1;
  m_doParamsFocused = false;
  if (m_actionOpen) {
    m_actionOpen = false;
    emit actionOpenChanged();
  }
  emit doVerbsChanged();
  emit doIndexChanged();
  emit doFocusChanged();
  if (wasOpen)
    emit doHintChanged();
}

void HostApi::closeAction() { destroyAction(); }

namespace {
QString posixWorkingDirectory(const QString &listingCwd,
                              const QString &filePath) {
  if (!listingCwd.isEmpty() && !DirectoryModel::isVirtualPath(listingCwd) &&
      QFileInfo(listingCwd).isDir())
    return listingCwd;
  const QString parent = QFileInfo(filePath).absolutePath();
  if (QFileInfo(parent).isDir())
    return parent;
  return QDir::homePath();
}
} // namespace

namespace {

constexpr int kPreviewCap = 256 * 1024;
constexpr int kFindHitCap = 500;
constexpr qint64 kFindLoadCap = 16 * 1024 * 1024;

bool needleHasUpper(const QString &s) {
  for (const QChar c : s) {
    if (c.isUpper())
      return true;
  }
  return false;
}

char foldAscii(char c) {
  if (c >= 'A' && c <= 'Z')
    return static_cast<char>(c - 'A' + 'a');
  return c;
}

int findBytes(const QByteArray &hay, const QByteArray &needle, int from,
              bool sensitive) {
  if (sensitive)
    return hay.indexOf(needle, from);
  const int n = needle.size();
  const int h = hay.size();
  if (n <= 0 || from > h - n)
    return -1;
  for (int i = from; i <= h - n; ++i) {
    int k = 0;
    for (; k < n; ++k) {
      if (foldAscii(hay.at(i + k)) != foldAscii(needle.at(k)))
        break;
    }
    if (k == n)
      return i;
  }
  return -1;
}

void walkLines(const QString &text, int from, int to, int *line, int *col) {
  for (int i = from; i < to && i < text.size(); ++i) {
    if (text.at(i) == QLatin1Char('\n')) {
      ++(*line);
      *col = 1;
    } else {
      ++(*col);
    }
  }
}

void walkLinesBytes(const QByteArray &buf, int from, int to, int *line,
                    int *col) {
  for (int i = from; i < to && i < buf.size(); ++i) {
    if (buf.at(i) == '\n') {
      ++(*line);
      *col = 1;
    } else {
      ++(*col);
    }
  }
}

QString snippetAround(const QString &text, int at, int len) {
  const int a = qMax(0, at - 24);
  const int b = qMin(text.size(), at + len + 32);
  return text.mid(a, b - a).simplified();
}

QVariantMap makeHit(int line, int column, qint64 offset, int length,
                    const QString &snippet) {
  QVariantMap m;
  m.insert(QStringLiteral("line"), line);
  m.insert(QStringLiteral("column"), column);
  m.insert(QStringLiteral("offset"), offset);
  m.insert(QStringLiteral("length"), length);
  m.insert(QStringLiteral("snippet"), snippet);
  return m;
}

} // namespace

QVariantMap HostApi::readPreview(const QUrl &url, int maxBytes,
                                 qint64 startByte) const {
  QVariantMap out;
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  const QFileInfo fi(path);
  out.insert(QStringLiteral("path"), path);
  out.insert(QStringLiteral("name"), fi.fileName());
  out.insert(QStringLiteral("size"), fi.size());
  if (maxBytes <= 0)
    maxBytes = 65536;
  if (maxBytes > kPreviewCap)
    maxBytes = kPreviewCap;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), QStringLiteral("unreadable"));
    return out;
  }
  qint64 start = qMax(qint64(0), startByte);
  if (start > fi.size())
    start = fi.size();
  if (start > 0) {
    const qint64 back = qMin(start, qint64(1024));
    f.seek(start - back);
    const QByteArray pre = f.read(back);
    const int nl = pre.lastIndexOf('\n');
    if (nl >= 0)
      start = start - back + nl + 1;
    f.seek(start);
  }
  QByteArray raw = f.read(maxBytes + 1);
  const bool tailCut = raw.size() > maxBytes;
  if (tailCut)
    raw.chop(1);
  const int probe = qMin(raw.size(), 4096);
  const bool binary = raw.left(probe).contains('\0');
  out.insert(QStringLiteral("binary"), binary);
  out.insert(QStringLiteral("startByte"), start);
  out.insert(QStringLiteral("byteLength"), raw.size());
  out.insert(QStringLiteral("truncated"), tailCut || start > 0);
  out.insert(QStringLiteral("truncatedHead"), start > 0);
  out.insert(QStringLiteral("truncatedTail"), tailCut);
  if (binary) {
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("text"), QString());
    out.insert(QStringLiteral("error"), QStringLiteral("binary"));
    return out;
  }
  out.insert(QStringLiteral("ok"), true);
  const QString text = QString::fromUtf8(raw);
  out.insert(QStringLiteral("text"), text);
  if (start == 0) {
    const HighlightedText hl = SyntaxHighlight::highlight(path, text);
    out.insert(QStringLiteral("highlighted"), hl.ok);
    out.insert(QStringLiteral("language"), hl.language);
    if (hl.ok)
      out.insert(QStringLiteral("html"), hl.html);
  } else {
    out.insert(QStringLiteral("highlighted"), false);
  }
  return out;
}

QVariantList HostApi::findInFile(const QUrl &url, const QString &needle,
                                 int maxHits) const {
  QVariantList hits;
  if (needle.isEmpty())
    return hits;
  if (maxHits <= 0)
    maxHits = 200;
  if (maxHits > kFindHitCap)
    maxHits = kFindHitCap;
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return hits;
  const qint64 size = f.size();
  const QByteArray needleUtf8 = needle.toUtf8();
  if (needleUtf8.isEmpty())
    return hits;
  const bool sensitive = needleHasUpper(needle);

  auto takeHit = [&](int line, int column, qint64 offset, int length,
                     const QString &snippet) {
    hits.append(makeHit(line, column, offset, length, snippet));
  };

  if (size <= kFindLoadCap) {
    const QByteArray raw = f.readAll();
    if (raw.left(qMin(raw.size(), 4096)).contains('\0'))
      return hits;
    const QString text = QString::fromUtf8(raw);
    const Qt::CaseSensitivity cs =
        sensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    int from = 0;
    int scanned = 0;
    int line = 1;
    int col = 1;
    qint64 bytes = 0;
    while (hits.size() < maxHits) {
      const int at = text.indexOf(needle, from, cs);
      if (at < 0)
        break;
      walkLines(text, scanned, at, &line, &col);
      bytes += text.mid(scanned, at - scanned).toUtf8().size();
      const int matchChars = needle.size();
      const int matchBytes = text.mid(at, matchChars).toUtf8().size();
      takeHit(line, col, bytes, matchBytes, snippetAround(text, at, matchChars));
      scanned = at;
      from = at + qMax(1, matchChars);
    }
    return hits;
  }

  QByteArray probe = f.peek(4096);
  if (probe.contains('\0'))
    return hits;
  int line = 1;
  int col = 1;
  int walked = 0;
  QByteArray carry;
  qint64 produced = 0;
  const int overlap = needleUtf8.size() - 1;
  while (hits.size() < maxHits) {
    const QByteArray chunk = f.read(256 * 1024);
    if (chunk.isEmpty() && carry.isEmpty())
      break;
    const QByteArray hay = carry + chunk;
    const qint64 hayStart = produced - carry.size();
    int searchFrom = 0;
    while (hits.size() < maxHits) {
      const int at = findBytes(hay, needleUtf8, searchFrom, sensitive);
      if (at < 0)
        break;
      walkLinesBytes(hay, walked, at, &line, &col);
      walked = at;
      const QString snip = QString::fromUtf8(
          hay.mid(qMax(0, at - 24), needleUtf8.size() + 48));
      takeHit(line, col, hayStart + at, needleUtf8.size(), snip.simplified());
      searchFrom = at + needleUtf8.size();
    }
    const int keep = qMax(0, overlap);
    const int consume = qMax(0, hay.size() - keep);
    walkLinesBytes(hay, walked, consume, &line, &col);
    walked = 0;
    carry = hay.right(keep);
    produced += chunk.size();
    if (chunk.isEmpty())
      break;
  }
  return hits;
}

QString HostApi::markFindHits(const QString &html, const QString &plain,
                              const QString &needle, int currentLocal,
                              const QColor &matchFill,
                              const QColor &currentFill) const {
  return SyntaxHighlight::markFinds(html, plain, needle, currentLocal,
                                    matchFill, currentFill);
}

QString HostApi::peekFindQuery() const {
  if (!m_model || !m_model->isContentSearch())
    return {};
  return m_model->searchQuery();
}

QVariantMap HostApi::readParquet(const QUrl &url, int maxRows) const {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  return ParquetMeta::toMap(ParquetMeta::parseWithSample(path, maxRows));
}

QVariantMap HostApi::readDatabase(const QUrl &url, const QString &engine,
                                  const QString &table, int offset,
                                  int limit) const {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  return DbPreview::inspect(path, engine, table, offset, limit);
}

QVariantMap HostApi::readArchive(const QUrl &url, int maxEntries) const {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  return ArchiveMeta::toMap(ArchiveMeta::inspect(path, maxEntries));
}

void HostApi::setPeekPreviewFocused(bool on) {
  if (m_peekPreviewFocused == on)
    return;
  m_peekPreviewFocused = on;
  emit peekFocusChanged();
}

bool HostApi::deliverPeekKey(int key, int modifiers) {
  QObject *item = m_filePreviewItem ? m_filePreviewItem : m_previewItem;
  if (!item || item == m_folderPreviewItem)
    return false;
  QVariant consumed;
  if (QMetaObject::invokeMethod(item, "peekKey", Qt::DirectConnection,
                                Q_RETURN_ARG(QVariant, consumed),
                                Q_ARG(QVariant, key),
                                Q_ARG(QVariant, modifiers)))
    return consumed.toBool();
  if (QMetaObject::invokeMethod(item, "peekKey", Qt::DirectConnection,
                                Q_RETURN_ARG(QVariant, consumed),
                                Q_ARG(int, key), Q_ARG(int, modifiers)))
    return consumed.toBool();
  return false;
}

void HostApi::setGridMode(bool on) {
  if (m_gridMode == on)
    return;
  m_gridMode = on;
  emit gridModeChanged();
}

int HostApi::peekGridStride() const {
  return m_gridMode ? qMax(1, m_peekGridStride) : 1;
}

void HostApi::setPeekGridStride(int columns) {
  const int next = qMax(1, columns);
  if (m_peekGridStride == next)
    return;
  m_peekGridStride = next;
  emit peekGridStrideChanged();
}

QString HostApi::folderPath() const {
  return m_folderStack.isEmpty() ? QString() : m_folderStack.constLast();
}

QObject *HostApi::peekModel() const { return m_peekModel; }

QObject *HostApi::peekProxy() const {
  if (!m_folderStack.isEmpty() && m_peekProxy)
    return m_peekProxy;
  return m_proxy;
}

QObject *HostApi::peekFileProxy() {
  if (!m_fileOnlyProxy)
    m_fileOnlyProxy = new FilesOnlyProxy(this);
  FilterProxy *src = nullptr;
  if (!m_folderStack.isEmpty() && m_peekProxy)
    src = m_peekProxy;
  else
    src = m_proxy;
  m_fileOnlyProxy->setListing(src);
  return m_fileOnlyProxy;
}

void HostApi::setChooserMode(bool on) { m_chooserMode = on; }

void HostApi::ensurePeekListing() {
  if (m_peekModel)
    return;
  m_peekModel = new DirectoryModel(this);
  m_peekProxy = new FilterProxy(this);
  m_peekProxy->setDirectoryModel(m_peekModel);
  connect(m_peekModel, &DirectoryModel::pathChanged, this, [this] {
    if (m_peekNavigating || m_folderStack.isEmpty())
      return;
    const QString path = m_peekModel->path();
    if (path.isEmpty() || path == m_folderStack.constLast())
      return;
    m_folderStack.append(path);
    emit folderPeekChanged();
    setTitle(path);
  });
  connect(m_peekModel, &DirectoryModel::fileActivated, this,
          [this](const QString &path, const QString &mime) {
            Manifest::Item item;
            item.path = path;
            item.uri = QUrl::fromLocalFile(path);
            item.mime = mime;
            if (item.mime.isEmpty() && m_mime)
              item.mime = m_mime->mimeForFile(path);
            item.isDir = false;
            showPeekFile(item);
          });
  emit folderPeekChanged();
}

void HostApi::endFolderPeek() {
  const bool had = !m_folderStack.isEmpty() || m_filePeekFromFolder;
  m_folderStack.clear();
  m_filePeekFromFolder = false;
  m_peekNavigating = false;
  if (had)
    emit folderPeekChanged();
}

void HostApi::startFolderPeek(const QString &path) {
  if (path.isEmpty())
    return;
  ensurePeekListing();
  m_filePeekFromFolder = false;
  m_peekNavigating = true;
  m_folderStack = {path};
  m_peekModel->setPath(path);
  m_peekNavigating = false;
  setTitle(path);
  if (!m_open) {
    m_open = true;
    emit openChanged();
  }
  emit folderPeekChanged();
  showFolderListing();
}

void HostApi::showFolderListing() {
  const bool leavingFile = m_filePeekFromFolder;
  m_filePeekFromFolder = false;
  destroyFilePreview();
  if (m_folderStack.isEmpty())
    return;
  Manifest::Item item;
  item.path = m_folderStack.constLast();
  item.uri = QUrl::fromLocalFile(item.path);
  item.mime = QStringLiteral("inode/directory");
  item.isDir = true;
  applyPeekSelection(item);
  if (!m_folderPreviewItem) {
    if (!loadPreviewFor(item, &m_folderPreviewItem))
      return;
    emit folderListingItemChanged();
  } else if (auto *qi = qobject_cast<QQuickItem *>(m_folderPreviewItem)) {
    qi->setProperty("file", m_file);
    qi->setProperty("selection", m_selection);
  }
  m_previewItem = m_folderPreviewItem;
  m_loadedId = QStringLiteral("synchro.preview.folder");
  emit previewItemChanged();
  if (leavingFile)
    emit folderPeekChanged();
}

void HostApi::showPeekFile(const Manifest::Item &item) {
  if (item.path.isEmpty())
    return;
  const bool entered = !m_filePeekFromFolder;
  m_filePeekFromFolder = true;
  applyPeekSelection(item);
  destroyFilePreview();
  if (!loadPreviewFor(item, &m_filePreviewItem)) {
    // Stay in file-peek so W/S can keep walking files. Falling back to
    // the folder listing mid-stride hid regular files until the layer
    // was torn down.
    m_previewItem = nullptr;
    emit previewItemChanged();
    if (entered)
      emit folderPeekChanged();
    return;
  }
  m_previewItem = m_filePreviewItem;
  emit previewItemChanged();
  if (entered)
    emit folderPeekChanged();
}

Manifest::Item HostApi::peekItemAt(int proxyRow) const {
  Manifest::Item item;
  if (!m_peekProxy || proxyRow < 0 || proxyRow >= m_peekProxy->rowCount())
    return item;
  const QModelIndex idx = m_peekProxy->index(proxyRow, 0);
  item.path = m_peekProxy->data(idx, DirectoryModel::PathRole).toString();
  item.uri = m_peekProxy->data(idx, DirectoryModel::UriRole).toUrl();
  item.mime = m_peekProxy->data(idx, DirectoryModel::MimeRole).toString();
  item.isDir = m_peekProxy->data(idx, DirectoryModel::IsDirRole).toBool();
  if (item.uri.isEmpty() && !item.path.isEmpty())
    item.uri = QUrl::fromLocalFile(item.path);
  if (item.mime.isEmpty() && m_mime && !item.path.isEmpty())
    item.mime = m_mime->mimeForFile(item.path);
  return item;
}

QString HostApi::peekCursorPath() const {
  if (m_filePeekFromFolder)
    return m_file.toLocalFile();
  if (!m_folderStack.isEmpty())
    return peekCurrentItem().path;
  return m_file.toLocalFile();
}

QString HostApi::peekFileName() const {
  return QFileInfo(peekCursorPath()).fileName();
}

bool HostApi::peekCursorIsDir() const {
  if (m_filePeekFromFolder)
    return false;
  if (!m_folderStack.isEmpty())
    return peekCurrentItem().isDir;
  return false;
}

Manifest::Item HostApi::peekCurrentItem() const {
  if (!m_peekProxy)
    return {};
  return peekItemAt(m_peekProxy->currentIndex());
}

void HostApi::peekMove(int delta) {
  if (m_folderStack.isEmpty() || m_filePeekFromFolder || !m_peekProxy)
    return;
  m_peekProxy->moveCursor(delta);
}

void HostApi::peekActivate() {
  if (m_folderStack.isEmpty()) {
    const Manifest::Item item = currentItem();
    if (item.path.isEmpty())
      return;
    if (item.isDir) {
      startFolderPeek(item.path);
      return;
    }
    applyPeekSelection(item);
    reloadPreview();
    return;
  }
  const Manifest::Item item = peekCurrentItem();
  if (item.path.isEmpty())
    return;
  if (item.isDir) {
    m_peekNavigating = true;
    m_folderStack.append(item.path);
    m_peekModel->setPath(item.path);
    m_peekNavigating = false;
    setTitle(item.path);
    emit folderPeekChanged();
    showFolderListing();
    return;
  }
  showPeekFile(item);
}

void HostApi::commitPeek() {
  if (m_chooserMode) {
    emit peekCommitRequested();
    return;
  }
  QString path = peekCursorPath();
  if (path.isEmpty() && folderListing())
    path = folderPath();
  if (path.isEmpty())
    return;
  const bool dir = QFileInfo(path).isDir();
  QString mime;
  if (m_mime)
    mime = m_mime->mimeForFile(path);
  close();
  if (dir) {
    if (m_model)
      m_model->setPath(path);
    return;
  }
  if (!openFile(path, mime)) {
    std::fprintf(stderr, "synchro: commit %s: %s\n", qPrintable(path),
                 qPrintable(m_error));
    return;
  }
  emit fileCommitted(path, mime);
}

void HostApi::peekBack() {
  if (m_folderStack.isEmpty()) {
    close();
    return;
  }
  if (m_filePeekFromFolder) {
    showFolderListing();
    return;
  }
  if (m_folderStack.size() <= 1) {
    close();
    return;
  }
  m_folderStack.removeLast();
  m_peekNavigating = true;
  m_peekModel->setPath(m_folderStack.constLast());
  m_peekNavigating = false;
  setTitle(m_folderStack.constLast());
  emit folderPeekChanged();
  showFolderListing();
}

void HostApi::stepPeekFiles(int delta) {
  if (!m_peekProxy || delta == 0)
    return;
  const int count = m_peekProxy->rowCount();
  int cur = m_peekProxy->currentIndex();
  if (cur < 0)
    return;
  for (int i = cur + delta; i >= 0 && i < count; i += delta) {
    const Manifest::Item item = peekItemAt(i);
    if (item.isDir || item.path.isEmpty())
      continue;
    m_peekProxy->setCurrentIndex(i);
    showPeekFile(item);
    return;
  }
}

QUrl HostApi::rasterUrl(const QUrl &url) const {
  const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
  if (path.isEmpty())
    return url;
  QImageReader reader(path);
  reader.setAutoTransform(true);
  if (reader.canRead())
    return url;
  const QString png = ThumbnailService::ensureRasterPng(path, 0, 4096);
  if (png.isEmpty())
    return url;
  return QUrl::fromLocalFile(png);
}

bool HostApi::openFile(const QString &path, const QString &mime) {
  m_error.clear();
  Manifest::Item item;
  item.path = path;
  item.uri = QUrl::fromLocalFile(path);
  item.mime = mime;
  if (item.mime.isEmpty() && m_mime)
    item.mime = m_mime->mimeForFile(path);
  item.isDir = false;
  const QString listing = m_model ? m_model->path() : QString();
  const QString cwd = posixWorkingDirectory(listing, path);
  if (m_actions.openBest({item}, cwd))
    return true;
  // Registry miss (disabled xdg, no match): last-ditch desktop default.
  if (m_xdg && m_xdg->open(path, item.mime, cwd))
    return true;
  m_error = m_actions.lastError();
  if (m_error.isEmpty() && m_xdg)
    m_error = m_xdg->lastError();
  return false;
}

bool HostApi::runOpen(const QString &handlerId) {
  m_error.clear();
  const QVector<Manifest::Item> items = activeItems();
  if (items.isEmpty()) {
    m_error = QStringLiteral("nothing selected");
    return false;
  }
  const QString listing = m_model ? m_model->path() : QString();
  const QString cwd = posixWorkingDirectory(
      listing, items.isEmpty() ? QString() : items.first().path);
  if (!m_actions.openWith(handlerId, items, cwd)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: open-with %s: %s\n", qPrintable(handlerId),
                 qPrintable(m_error));
    return false;
  }
  closeAction();
  return true;
}

bool HostApi::runTerminal() {
  m_error.clear();
  close();
  closeAction();
  const QString cwd = m_model ? m_model->path() : QString();
  if (HandlerActions::isVirtualLocation(cwd)) {
    m_error = QStringLiteral("terminal is disabled on virtual locations");
    std::fprintf(stderr, "synchro: %s\n", qPrintable(m_error));
    return false;
  }
  QVector<Manifest::Item> items = currentItems();
  if (items.isEmpty()) {
    Manifest::Item cwdItem;
    cwdItem.path = cwd;
    cwdItem.uri = QUrl::fromLocalFile(cwd);
    cwdItem.mime = QStringLiteral("inode/directory");
    cwdItem.isDir = true;
    items.append(cwdItem);
  }
  if (!m_actions.runTerminal(items, cwd)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: terminal: %s\n", qPrintable(m_error));
    return false;
  }
  return true;
}

bool HostApi::runTrash() {
  m_error.clear();
  if (m_ops) {
    m_ops->trashSelection();
    if (!m_ops->errorString().isEmpty()) {
      m_error = m_ops->errorString();
      std::fprintf(stderr, "synchro: trash: %s\n", qPrintable(m_error));
      return false;
    }
    return true;
  }
  const QVector<Manifest::Item> items = currentItems();
  if (items.isEmpty()) {
    m_error = QStringLiteral("nothing selected");
    return false;
  }
  if (!m_actions.runTrash(items)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: trash: %s\n", qPrintable(m_error));
    return false;
  }
  return true;
}

bool HostApi::restoreTrash() {
  m_error.clear();
  if (!m_model || !m_model->isTrash()) {
    m_error = QStringLiteral("restore is only available in trash");
    return false;
  }
  if (!m_model->restoreCurrent()) {
    m_error = m_model->errorString().isEmpty()
                  ? QStringLiteral("restore failed")
                  : m_model->errorString();
    return false;
  }
  return true;
}

bool HostApi::emptyTrash() {
  // Shared XDG trash — never wipe without the KeyMachine y/n confirm.
  m_error = QStringLiteral("empty requires confirm");
  return false;
}

bool HostApi::runAction(const QString &handlerId) {
  m_error.clear();
  if (!m_registry) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  const HandlerRegistry::Record rec = m_registry->handler(handlerId);
  if (rec.manifest.id.isEmpty() || !rec.enabled) {
    m_error = QStringLiteral("unknown action '%1'").arg(handlerId);
    return false;
  }
  if (!rec.manifest.hasKind(QStringLiteral("action"))) {
    m_error = QStringLiteral("handler '%1' is not an action").arg(handlerId);
    return false;
  }
  close();
  const HandlerActions::Kind kind =
      HandlerActions::classify(rec.manifest, QStringLiteral("action"));
  if (kind == HandlerActions::Kind::Qml)
    return openDoLayer(handlerId);
  setCurrentFromModel();
  const QVector<Manifest::Item> items = activeItems();
  const QString cwd = m_model ? m_model->path() : QString();
  if (!m_actions.runAction(handlerId, items, cwd)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: :%s: %s\n", qPrintable(handlerId),
                 qPrintable(m_error));
    return false;
  }
  return true;
}

bool HostApi::openWithPalette() { return openDoLayer(); }

QVariantMap HostApi::itemToMap(const Manifest::Item &item) {
  QVariantMap row;
  row.insert(QStringLiteral("path"), item.path);
  row.insert(QStringLiteral("uri"), item.uri);
  row.insert(QStringLiteral("mime"), item.mime);
  row.insert(QStringLiteral("isDir"), item.isDir);
  return row;
}

QVector<Manifest::Item> HostApi::activeItems() const {
  if (m_actionOpen && !m_doItems.isEmpty())
    return m_doItems;
  return currentItems();
}

QVector<Manifest::Item> HostApi::snapshotDoItems() const {
  if (folderPeek()) {
    Manifest::Item item = peekCurrentItem();
    if (item.path.isEmpty() && m_filePeekFromFolder && m_file.isValid()) {
      item.path = m_file.isLocalFile() ? m_file.toLocalFile() : m_file.toString();
      item.uri = m_file;
      item.mime = m_mime ? m_mime->mimeForFile(item.path) : QString();
      item.isDir = false;
    }
    if (!item.path.isEmpty())
      return {item};
  }
  return currentItems();
}

QVariantList HostApi::doVerbs() const {
  QVariantList out;
  out.reserve(m_doVerbs.size());
  for (const DoVerb &v : m_doVerbs) {
    QVariantMap row;
    row.insert(QStringLiteral("id"), v.id);
    row.insert(QStringLiteral("name"), v.name);
    row.insert(QStringLiteral("description"), v.description);
    row.insert(QStringLiteral("runtime"), v.runtime);
    row.insert(QStringLiteral("group"), v.group);
    row.insert(QStringLiteral("hasParams"), v.hasParams);
    out.append(row);
  }
  return out;
}

QString HostApi::doCaption() const {
  if (m_doItems.size() == 1)
    return QStringLiteral("do · %1")
        .arg(QFileInfo(m_doItems.constFirst().path).fileName());
  if (m_doItems.size() > 1)
    return QStringLiteral("do · %1 files").arg(m_doItems.size());
  if (m_model && !m_model->path().isEmpty()) {
    const QFileInfo fi(m_model->path());
    const QString name = fi.fileName().isEmpty() ? m_model->path() : fi.fileName();
    return QStringLiteral("do · %1").arg(name);
  }
  return QStringLiteral("do");
}

QString HostApi::doBriefTitle() const {
  if (m_doIndex < 0 || m_doIndex >= m_doVerbs.size())
    return {};
  return m_doVerbs.at(m_doIndex).name;
}

QString HostApi::doBriefBody() const {
  if (m_doIndex < 0 || m_doIndex >= m_doVerbs.size())
    return {};
  return m_doVerbs.at(m_doIndex).description;
}

bool HostApi::doHasParams() const {
  return m_doIndex >= 0 && m_doIndex < m_doVerbs.size() &&
         m_doVerbs.at(m_doIndex).hasParams;
}

QString HostApi::listHint() const {
  return QStringLiteral("Enter open · Ctrl+Enter do");
}

QString HostApi::doHint() const {
  if (!m_actionOpen)
    return {};
  if (doHasParams())
    return QStringLiteral("W/S verbs · A/D params · Enter run · Esc leave");
  if (m_doPreviewItem)
    return QStringLiteral("W/S verbs · D preview · Enter run · Esc leave");
  return QStringLiteral("W/S verbs · Enter run · Esc leave");
}

void HostApi::setDoParamsFocused(bool on) {
  if (on && !doHasParams() && !m_doPreviewItem)
    on = false;
  if (m_doParamsFocused == on)
    return;
  m_doParamsFocused = on;
  emit doFocusChanged();
  emit doHintChanged();
}

void HostApi::rebuildDoVerbs() {
  m_doVerbs.clear();
  const QVector<Manifest::Item> items = m_doItems;
  const QString cwd = m_model ? m_model->path() : QString();
  const bool virt = HandlerActions::isVirtualLocation(cwd);

  if (!items.isEmpty()) {
    DoVerb open;
    open.id = QStringLiteral("synchro.do.open");
    open.name = QStringLiteral("Open");
    open.runtime = QStringLiteral("builtin");
    open.group = QStringLiteral("open");
    if (items.size() == 1 && items.constFirst().isDir)
      open.description = QStringLiteral("Enter this folder.");
    else if (items.size() == 1)
      open.description = QStringLiteral("Open with the default handler.");
    else
      open.description = QStringLiteral("Open each selected file.");
    m_doVerbs.append(open);
  }

  const QVector<HandlerRegistry::Match> actions = m_actions.actionMatches(items);
  auto appendAction = [&](const HandlerRegistry::Match &m) {
    if (m.id == QLatin1String("synchro.action.terminal") && virt)
      return;
    if (m.id == QLatin1String("synchro.action.agent") && virt)
      return;
    DoVerb v;
    v.id = m.id;
    v.name = m.manifest.name;
    v.description = m.manifest.description;
    v.runtime = m.manifest.runtime(QStringLiteral("action"));
    v.group = m.id == QLatin1String("synchro.action.open-with")
                  ? QStringLiteral("open")
                  : QStringLiteral("action");
    v.hasParams = HandlerActions::classify(m.manifest, QStringLiteral("action")) ==
                  HandlerActions::Kind::Qml;
    m_doVerbs.append(v);
  };
  for (const auto &m : actions) {
    if (m.id == QLatin1String("synchro.action.open-with"))
      appendAction(m);
  }
  for (const auto &m : actions) {
    if (m.id == QLatin1String("synchro.action.open-with"))
      continue;
    appendAction(m);
  }
  emit doVerbsChanged();
}

void HostApi::mountDoParams() {
  destroyActionItem();
  if (m_doIndex < 0 || m_doIndex >= m_doVerbs.size() || !m_registry ||
      !m_loader)
    return;
  const DoVerb &v = m_doVerbs.at(m_doIndex);
  if (!v.hasParams)
    return;
  const HandlerRegistry::Record rec = m_registry->handler(v.id);
  if (rec.manifest.id.isEmpty() || !rec.enabled)
    return;
  refreshOpenCandidates();
  QQuickItem *item = m_loader->create(m_engine, rec, QStringLiteral("action"),
                                      this, m_file, m_doSelection);
  if (!item) {
    m_error = m_loader->lastError();
    std::fprintf(stderr, "synchro: do params %s: %s\n", qPrintable(v.id),
                 qPrintable(m_error));
    return;
  }
  m_actionItem = item;
  attachDoParams();
  emit actionItemChanged();
}

void HostApi::registerDoSurface(QObject *surface) {
  m_doSurface = qobject_cast<QQuickItem *>(surface);
  attachDoParams();
}

void HostApi::registerDoContentSurface(QObject *surface) {
  m_doContentSurface = qobject_cast<QQuickItem *>(surface);
  attachDoPreview();
}

void HostApi::destroyDoContent() {
  if (m_doPreviewItem) {
    m_doPreviewItem->deleteLater();
    m_doPreviewItem = nullptr;
  }
  const bool had = m_doTargetIsDir || !m_doTargetName.isEmpty();
  m_doTargetName.clear();
  m_doTargetIsDir = false;
  if (had)
    emit doPreviewChanged();
}

QObject *HostApi::doFolderModel() const {
  return m_doTargetIsDir ? m_doFolderModel : nullptr;
}

QObject *HostApi::doFolderProxy() const {
  return m_doTargetIsDir ? m_doFolderProxy : nullptr;
}

void HostApi::ensureDoFolderListing() {
  if (m_doFolderModel)
    return;
  m_doFolderModel = new DirectoryModel(this);
  m_doFolderProxy = new FilterProxy(this);
  m_doFolderProxy->setDirectoryModel(m_doFolderModel);
}

Manifest::Item HostApi::doContentItem() const {
  if (!m_doItems.isEmpty())
    return m_doItems.constFirst();
  Manifest::Item cwd;
  if (!m_model)
    return cwd;
  const QString path = m_model->path();
  if (path.isEmpty() || DirectoryModel::isVirtualPath(path) ||
      !QFileInfo(path).isDir())
    return cwd;
  cwd.path = path;
  cwd.uri = QUrl::fromLocalFile(path);
  cwd.mime = QStringLiteral("inode/directory");
  cwd.isDir = true;
  return cwd;
}

void HostApi::attachDoPreview() {
  if (!m_doPreviewItem || !m_doContentSurface)
    return;
  m_doPreviewItem->setParentItem(m_doContentSurface);
  auto sync = [this] {
    if (!m_doPreviewItem || !m_doContentSurface)
      return;
    m_doPreviewItem->setX(0);
    m_doPreviewItem->setY(0);
    m_doPreviewItem->setWidth(m_doContentSurface->width());
    m_doPreviewItem->setHeight(m_doContentSurface->height());
  };
  sync();
  QObject::connect(m_doContentSurface, &QQuickItem::widthChanged, m_doPreviewItem,
                   sync);
  QObject::connect(m_doContentSurface, &QQuickItem::heightChanged,
                   m_doPreviewItem, sync);
}

void HostApi::loadDoContent() {
  destroyDoContent();
  const Manifest::Item item = doContentItem();
  if (item.path.isEmpty()) {
    emit doPreviewChanged();
    return;
  }
  m_doTargetIsDir = item.isDir;
  m_doTargetName = QFileInfo(item.path).fileName();
  if (m_doTargetName.isEmpty())
    m_doTargetName = item.path;

  if (item.isDir) {
    ensureDoFolderListing();
    if (m_model)
      m_doFolderModel->setShowHidden(m_model->showHidden());
    if (m_proxy) {
      m_doFolderProxy->setSortRoleName(m_proxy->sortRoleName());
      m_doFolderProxy->setSortOrder(m_proxy->sortOrder());
    }
    m_doFolderModel->setPath(item.path, QString(), true);
    emit doPreviewChanged();
    return;
  }

  QObject *slot = nullptr;
  if (loadPreviewFor(item, &slot))
    m_doPreviewItem = qobject_cast<QQuickItem *>(slot);
  if (m_doPreviewItem) {
    m_doPreviewItem->setFocus(false);
    attachDoPreview();
  }
  emit doPreviewChanged();
}

void HostApi::attachDoParams() {
  if (!m_actionItem || !m_doSurface)
    return;
  m_actionItem->setParentItem(m_doSurface);
  auto sync = [this] {
    if (!m_actionItem || !m_doSurface)
      return;
    m_actionItem->setX(0);
    m_actionItem->setY(0);
    m_actionItem->setWidth(m_doSurface->width());
    m_actionItem->setHeight(m_doSurface->height());
  };
  sync();
  QObject::connect(m_doSurface, &QQuickItem::widthChanged, m_actionItem, sync);
  QObject::connect(m_doSurface, &QQuickItem::heightChanged, m_actionItem, sync);
}

void HostApi::setDoIndex(int index) {
  if (m_doVerbs.isEmpty()) {
    if (m_doIndex != -1) {
      m_doIndex = -1;
      destroyActionItem();
      emit doIndexChanged();
      emit doHintChanged();
    }
    return;
  }
  index = qBound(0, index, m_doVerbs.size() - 1);
  const bool changed = index != m_doIndex;
  m_doIndex = index;
  if (m_doParamsFocused && !doHasParams())
    setDoParamsFocused(false);
  if (changed)
    emit doIndexChanged();
  mountDoParams();
  if (changed)
    emit doHintChanged();
}

void HostApi::doMove(int delta) {
  if (m_doVerbs.isEmpty() || delta == 0)
    return;
  setDoIndex(m_doIndex + delta);
}

bool HostApi::commitDoItems() {
  if (m_doItems.isEmpty()) {
    m_error = QStringLiteral("nothing selected");
    return false;
  }
  const QVector<Manifest::Item> items = m_doItems;
  closeAction();
  if (m_chooserMode) {
    emit peekCommitRequested();
    return true;
  }
  bool any = false;
  for (const Manifest::Item &item : items) {
    if (item.isDir) {
      if (m_model)
        m_model->setPath(item.path);
      return true;
    }
    if (openFile(item.path, item.mime)) {
      emit fileCommitted(item.path, item.mime);
      any = true;
    } else {
      std::fprintf(stderr, "synchro: do open %s: %s\n", qPrintable(item.path),
                   qPrintable(m_error));
    }
  }
  return any;
}

bool HostApi::runDoVerb() {
  m_error.clear();
  if (m_doIndex < 0 || m_doIndex >= m_doVerbs.size()) {
    m_error = QStringLiteral("no action");
    return false;
  }
  const DoVerb v = m_doVerbs.at(m_doIndex);
  if (v.id == QLatin1String("synchro.do.open"))
    return commitDoItems();
  if (v.hasParams) {
    if (m_actionItem) {
      QVariant ok;
      if (QMetaObject::invokeMethod(m_actionItem, "commit",
                                    Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, ok))) {
        if (ok.toBool()) {
          if (m_actionOpen)
            closeAction();
          return true;
        }
        return false;
      }
    }
    m_error = QStringLiteral("action needs parameters");
    return false;
  }
  const QString cwd = m_model ? m_model->path() : QString();
  if (!m_actions.runAction(v.id, m_doItems, cwd)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: do %s: %s\n", qPrintable(v.id),
                 qPrintable(m_error));
    return false;
  }
  closeAction();
  return true;
}

bool HostApi::deliverDoKey(int key, int modifiers) {
  QVariant consumed;
  QObject *target = nullptr;
  if (doHasParams() && m_actionItem)
    target = m_actionItem;
  else if (m_doPreviewItem)
    target = m_doPreviewItem;
  if (!target)
    return false;
  if (QMetaObject::invokeMethod(target, "actionKey", Qt::DirectConnection,
                                Q_RETURN_ARG(QVariant, consumed),
                                Q_ARG(QVariant, key),
                                Q_ARG(QVariant, modifiers)))
    return consumed.toBool();
  if (QMetaObject::invokeMethod(target, "peekKey", Qt::DirectConnection,
                                Q_RETURN_ARG(QVariant, consumed),
                                Q_ARG(QVariant, key),
                                Q_ARG(QVariant, modifiers)))
    return consumed.toBool();
  return false;
}

bool HostApi::copyText(const QString &text) {
  m_error.clear();
  QClipboard *clip = QGuiApplication::clipboard();
  if (!clip) {
    m_error = QStringLiteral("no clipboard");
    return false;
  }
  clip->setText(text);
  return true;
}

bool HostApi::openDoLayer(const QString &focusId) {
  m_error.clear();
  m_doItems = snapshotDoItems();
  m_doSelection.clear();
  m_doSelection.reserve(m_doItems.size());
  for (const Manifest::Item &item : m_doItems)
    m_doSelection.append(itemToMap(item));
  if (!m_doItems.isEmpty()) {
    const Manifest::Item &first = m_doItems.constFirst();
    const QUrl file = first.uri.isEmpty() ? QUrl::fromLocalFile(first.path)
                                          : first.uri;
    if (m_file != file) {
      m_file = file;
      emit fileChanged();
    }
    if (m_selection != m_doSelection) {
      m_selection = m_doSelection;
      emit selectionChanged();
    }
  }
  close();
  rebuildDoVerbs();
  int idx = 0;
  if (!focusId.isEmpty()) {
    for (int i = 0; i < m_doVerbs.size(); ++i) {
      if (m_doVerbs.at(i).id == focusId) {
        idx = i;
        break;
      }
    }
  }
  const bool wasOpen = m_actionOpen;
  m_actionOpen = true;
  m_doIndex = -1;
  m_doParamsFocused = false;
  if (!wasOpen)
    emit actionOpenChanged();
  // After actionOpen: QML binds doFolder* on doPreviewChanged.
  loadDoContent();
  setDoIndex(idx);
  emit doHintChanged();
  return true;
}

void HostApi::refreshListingChrome() {
  QUrl row;
  QUrl thumb;
  if (m_model && m_model->isVolumes() && m_registry) {
    const HandlerRegistry::Record rec =
        m_registry->handler(QStringLiteral("synchro.location.volumes"));
    if (rec.enabled && !rec.manifest.id.isEmpty()) {
      row = HandlerLoader::entryPointUrl(rec, QStringLiteral("row"));
      thumb = HandlerLoader::entryPointUrl(rec, QStringLiteral("thumb"));
    }
  }
  if (row == m_listingRowUrl && thumb == m_listingThumbUrl)
    return;
  m_listingRowUrl = row;
  m_listingThumbUrl = thumb;
  emit listingChromeChanged();
}
