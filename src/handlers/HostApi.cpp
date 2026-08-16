#include "HostApi.h"

#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "XdgOpen.h"

#include <QAbstractItemModel>
#include <QFileInfo>
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
    });
    connect(m_model, &DirectoryModel::entryStatReady, this,
            &HostApi::onEntryStat);
  }
}

void HostApi::onEntryStat(const QString &path, const QVariantMap &st) {
  emit statReady(QUrl::fromLocalFile(path), st);
  if (m_open && currentItem().path == path && !m_previewItem)
    reloadPreview();
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

void HostApi::destroyPreview() {
  if (!m_previewItem)
    return;
  m_previewItem->deleteLater();
  m_previewItem = nullptr;
  m_loadedId.clear();
  emit previewItemChanged();
}

void HostApi::reloadPreview() {
  setCurrentFromModel();
  if (!m_registry || m_selection.isEmpty()) {
    destroyPreview();
    return;
  }
  const Manifest::Item item = currentItem();
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
  if (!m_loader)
    return;
  HandlerRegistry::Record rec = m_registry->handler(best.id);
  QQuickItem *itemQml =
      m_loader->create(m_engine, rec, QStringLiteral("preview"), this, m_file,
                       m_selection);
  if (!itemQml) {
    std::fprintf(stderr, "synchro: peek %s: %s\n", qPrintable(best.id),
                 qPrintable(m_loader->lastError()));
    return;
  }
  m_previewItem = itemQml;
  m_loadedId = best.id;
  emit previewItemChanged();
}

void HostApi::close() {
  if (!m_open && !m_previewItem)
    return;
  destroyPreview();
  if (!m_open)
    return;
  m_open = false;
  emit openChanged();
}

bool HostApi::openCurrent() {
  const Manifest::Item item = currentItem();
  if (item.path.isEmpty())
    return false;
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
  const QVariantList next = m_actions.openCandidates(currentItems());
  if (m_openCandidates == next)
    return;
  m_openCandidates = next;
  emit openCandidatesChanged();
}

void HostApi::destroyAction() {
  if (m_actionItem) {
    m_actionItem->deleteLater();
    m_actionItem = nullptr;
    emit actionItemChanged();
  }
  if (!m_actionOpen)
    return;
  m_actionOpen = false;
  emit actionOpenChanged();
}

void HostApi::closeAction() { destroyAction(); }

bool HostApi::openFile(const QString &path, const QString &mime) {
  m_error.clear();
  Manifest::Item item;
  item.path = path;
  item.uri = QUrl::fromLocalFile(path);
  item.mime = mime;
  if (item.mime.isEmpty() && m_mime)
    item.mime = m_mime->mimeForFile(path);
  item.isDir = false;
  const QString cwd =
      m_model ? m_model->path() : QFileInfo(path).absolutePath();
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
  const QVector<Manifest::Item> items = currentItems();
  if (items.isEmpty()) {
    m_error = QStringLiteral("nothing selected");
    return false;
  }
  const QString cwd = m_model ? m_model->path() : QString();
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
  setCurrentFromModel();
  const QVector<Manifest::Item> items = currentItems();
  const QString cwd = m_model ? m_model->path() : QString();
  const HandlerActions::Kind kind =
      HandlerActions::classify(rec.manifest, QStringLiteral("action"));
  if (kind == HandlerActions::Kind::Qml)
    return loadActionSurface(rec);
  if (!m_actions.runAction(handlerId, items, cwd)) {
    m_error = m_actions.lastError();
    std::fprintf(stderr, "synchro: :%s: %s\n", qPrintable(handlerId),
                 qPrintable(m_error));
    return false;
  }
  return true;
}

bool HostApi::loadActionSurface(const HandlerRegistry::Record &rec) {
  refreshOpenCandidates();
  if (!m_loader) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  destroyAction();
  QQuickItem *item = m_loader->create(m_engine, rec, QStringLiteral("action"),
                                      this, m_file, m_selection);
  if (!item) {
    m_error = m_loader->lastError();
    std::fprintf(stderr, "synchro: action %s: %s\n", qPrintable(rec.manifest.id),
                 qPrintable(m_error));
    return false;
  }
  m_actionItem = item;
  m_actionOpen = true;
  emit actionItemChanged();
  emit actionOpenChanged();
  return true;
}

bool HostApi::openWithPalette() {
  m_error.clear();
  close();
  setCurrentFromModel();
  if (m_selection.isEmpty()) {
    m_error = QStringLiteral("nothing selected");
    return false;
  }
  if (!m_registry) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  HandlerRegistry::Record rec =
      m_registry->handler(QStringLiteral("synchro.action.open-with"));
  if (rec.manifest.id.isEmpty() || !rec.enabled) {
    m_error = QStringLiteral("synchro.action.open-with is not available");
    return false;
  }
  return loadActionSurface(rec);
}
