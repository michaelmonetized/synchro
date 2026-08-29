#include "SelectionModel.h"

#include "DirectoryModel.h"
#include "FilterProxy.h"

#include <algorithm>
#include <QFileInfo>
#include <QUrl>

SelectionModel::SelectionModel(FilterProxy *proxy, DirectoryModel *model,
                               QObject *parent)
    : QObject(parent), m_proxy(proxy), m_model(model) {
  if (m_proxy) {
    connect(m_proxy, &FilterProxy::currentIndexChanged, this,
            &SelectionModel::onCursorChanged);
    connect(m_proxy, &FilterProxy::countChanged, this,
            &SelectionModel::rematch);
    connect(m_proxy, &FilterProxy::filterChanged, this,
            &SelectionModel::rematch);
    connect(m_proxy, &FilterProxy::countChanged, this,
            &SelectionModel::statusTextChanged);
  }
  if (m_model) {
    connect(m_model, &DirectoryModel::pathChanged, this,
            &SelectionModel::onPathChanged);
  }
  const int c = cursor();
  if (c >= 0)
    replaceSelected({c});
}

int SelectionModel::cursor() const {
  return m_proxy ? m_proxy->currentIndex() : -1;
}

int SelectionModel::selectedCount() const {
  return m_externalItem.value(QStringLiteral("path")).toString().isEmpty()
             ? m_selected.size()
             : 1;
}

QString SelectionModel::statusText() const {
  const int n = m_proxy ? m_proxy->count() : 0;
  const QString files = QStringLiteral("%1 files").arg(n);
  if (selectedCount() > 1)
    return QStringLiteral("%1 selected   %2").arg(selectedCount()).arg(files);
  return files;
}

void SelectionModel::setCursor(int proxyRow) {
  if (!m_proxy)
    return;
  const bool hadExternal = clearExternalItem();
  m_proxy->setCurrentIndex(proxyRow);
  if (hadExternal) {
    const int now = cursor();
    replaceSelected(now >= 0 ? QSet<int>{now} : QSet<int>{});
  }
}

void SelectionModel::moveCursor(int delta) {
  if (!m_proxy)
    return;
  const bool hadExternal = clearExternalItem();
  m_proxy->moveCursor(delta);
  if (hadExternal) {
    const int now = cursor();
    replaceSelected(now >= 0 ? QSet<int>{now} : QSet<int>{});
  }
}

void SelectionModel::toggleCursor() {
  const int c = cursor();
  if (c < 0)
    return;
  toggleRow(c);
}

void SelectionModel::toggleRow(int proxyRow) {
  if (!m_proxy || proxyRow < 0 || proxyRow >= m_proxy->count())
    return;
  QSet<int> next = m_selected;
  if (next.contains(proxyRow))
    next.remove(proxyRow);
  else
    next.insert(proxyRow);
  replaceSelected(next);
}

void SelectionModel::selectAll() {
  if (!m_proxy)
    return;
  QSet<int> next;
  const int n = m_proxy->count();
  next.reserve(n);
  for (int i = 0; i < n; ++i)
    next.insert(i);
  replaceSelected(next);
}

void SelectionModel::setAnchor(int proxyRow) {
  m_anchor = proxyRow;
  m_anchorName = nameAt(proxyRow);
}

void SelectionModel::click(int proxyRow) {
  setAnchor(proxyRow);
  m_suppressFollow = true;
  setCursor(proxyRow);
  m_suppressFollow = false;
  // An ordinary click starts a new selection. Keeping a prior range alive
  // here made list rows feel impossible to deselect; additive selection is
  // already expressed explicitly by Ctrl-click and Shift-click.
  replaceSelected({proxyRow});
}

void SelectionModel::leftClick(int proxyRow) {
  // Pointer semantics are intentionally a little more direct than the
  // programmatic click(): clicking an already selected object removes it.
  // Right-click callers continue to use click() only when they first need to
  // target an unselected object, so opening a context menu never deselects.
  if (isSelected(proxyRow))
    ctrlClick(proxyRow);
  else
    click(proxyRow);
}

void SelectionModel::selectPath(const QString &path, const QString &name,
                                bool isDir, qint64 size) {
  if (path.isEmpty())
    return;

  // Prefer ordinary row selection whenever the target belongs to the current
  // listing. This keeps all established range and cursor semantics intact.
  if (m_proxy) {
    for (int row = 0; row < m_proxy->count(); ++row) {
      if (pathAt(row) == path) {
        click(row);
        return;
      }
    }
  }

  QVariantMap item;
  item.insert(QStringLiteral("path"), path);
  item.insert(QStringLiteral("uri"), QUrl::fromLocalFile(path));
  item.insert(QStringLiteral("name"),
              name.isEmpty() ? QFileInfo(path).fileName() : name);
  item.insert(QStringLiteral("isDir"), isDir);
  item.insert(QStringLiteral("size"), size);
  item.insert(QStringLiteral("mime"), QString());
  if (m_externalItem == item && m_selected.isEmpty())
    return;

  if (m_visual) {
    m_visual = false;
    emit visualChanged();
  }
  m_selected.clear();
  m_selectedNames.clear();
  m_externalItem = item;
  ++m_epoch;
  emit selectionChanged();
  emit epochChanged();
  emit statusTextChanged();
}

void SelectionModel::shiftClick(int proxyRow) {
  m_suppressFollow = true;
  setCursor(proxyRow);
  m_suppressFollow = false;
  applyRange(m_anchor, proxyRow);
}

void SelectionModel::ctrlClick(int proxyRow) {
  setAnchor(proxyRow);
  m_suppressFollow = true;
  setCursor(proxyRow);
  m_suppressFollow = false;
  toggleRow(proxyRow);
}

void SelectionModel::enterVisual() {
  if (m_visual)
    return;
  m_visual = true;
  setAnchor(cursor());
  applyRange(m_anchor, m_anchor);
  emit visualChanged();
}

void SelectionModel::exitVisual() {
  if (!m_visual)
    return;
  m_visual = false;
  emit visualChanged();
}

void SelectionModel::collapseToCursor() {
  exitVisual();
  if (!m_externalItem.isEmpty())
    return;
  const int c = cursor();
  if (c >= 0)
    replaceSelected({c});
  else
    replaceSelected({});
}

void SelectionModel::activate() {
  if (!m_proxy || !m_model)
    return;
  if (!m_externalItem.isEmpty()) {
    const QString path =
        m_externalItem.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
      return;
    if (m_externalItem.value(QStringLiteral("isDir")).toBool())
      m_model->setPath(path);
    else
      emit m_model->fileActivated(
          path, m_externalItem.value(QStringLiteral("mime")).toString());
    return;
  }
  if (m_model->isTrash()) {
    QStringList paths = selectedPaths();
    if (paths.isEmpty()) {
      const QString p = cursorPath();
      if (!p.isEmpty())
        paths.append(p);
    }
    if (!paths.isEmpty())
      m_model->requestRestore(paths);
    return;
  }
  QList<int> rows = selectedInOrder();
  if (rows.isEmpty()) {
    const int c = cursor();
    if (c >= 0)
      rows.append(c);
  }
  if (rows.isEmpty())
    return;
  if (rows.size() == 1) {
    m_proxy->setCurrentIndex(rows.first());
    m_proxy->activateCurrent();
    return;
  }
  QList<int> fileSrc;
  QList<int> dirSrc;
  for (int pr : rows) {
    const QModelIndex pidx = m_proxy->index(pr, 0);
    const QModelIndex src = m_proxy->mapToSource(pidx);
    if (!src.isValid())
      continue;
    if (isDirAt(pr))
      dirSrc.append(src.row());
    else
      fileSrc.append(src.row());
  }
  if (!fileSrc.isEmpty()) {
    for (int sr : fileSrc)
      m_model->activateIndex(sr);
    return;
  }
  if (dirSrc.isEmpty())
    return;
  const int csrc = m_model->currentIndex();
  if (dirSrc.contains(csrc))
    m_model->activateIndex(csrc);
  else
    m_model->activateIndex(dirSrc.first());
}

bool SelectionModel::isSelected(int proxyRow) const {
  return m_selected.contains(proxyRow);
}

QVariantList SelectionModel::selectedIndices() const {
  QVariantList out;
  const QList<int> rows = selectedInOrder();
  out.reserve(rows.size());
  for (int r : rows)
    out.append(r);
  return out;
}

QStringList SelectionModel::selectedPaths() const {
  QStringList out;
  const QString external =
      m_externalItem.value(QStringLiteral("path")).toString();
  if (!external.isEmpty()) {
    out.append(external);
    return out;
  }
  const QList<int> rows = selectedInOrder();
  out.reserve(rows.size());
  for (int r : rows) {
    const QString p = pathAt(r);
    if (!p.isEmpty())
      out.append(p);
  }
  return out;
}

QVariantMap SelectionModel::previewSummary(int itemLimit,
                                           int aggregateLimit) const {
  QVariantMap out;
  const int count = selectedCount();
  itemLimit = qBound(1, itemLimit, 24);
  aggregateLimit = qBound(itemLimit, aggregateLimit, 50000);
  out.insert(QStringLiteral("count"), count);
  if (!m_externalItem.isEmpty()) {
    out.insert(QStringLiteral("items"), QVariantList{m_externalItem});
    const bool isDir =
        m_externalItem.value(QStringLiteral("isDir")).toBool();
    const qint64 size =
        m_externalItem.value(QStringLiteral("size")).toLongLong();
    out.insert(QStringLiteral("files"), isDir ? 0 : 1);
    out.insert(QStringLiteral("folders"), isDir ? 1 : 0);
    out.insert(QStringLiteral("bytes"), !isDir && size >= 0 ? size : 0);
    out.insert(QStringLiteral("knownSizes"), !isDir && size >= 0 ? 1 : 0);
    out.insert(QStringLiteral("aggregateCount"), 1);
    out.insert(QStringLiteral("aggregateComplete"), true);
    out.insert(QStringLiteral("sizeComplete"), isDir || size >= 0);
    out.insert(QStringLiteral("kindSummary"),
               QStringList{isDir ? QStringLiteral("1 folders")
                                 : QStringLiteral("1 files")});
    return out;
  }
  if (!m_proxy || count == 0) {
    out.insert(QStringLiteral("items"), QVariantList());
    out.insert(QStringLiteral("aggregateComplete"), true);
    return out;
  }

  // Keep a stable, listing-order sample without sorting a potentially huge
  // selection. Aggregate common selections exactly; very large selections
  // are explicitly labelled as sampled so Look never stalls the browser.
  QList<int> sampleRows;
  sampleRows.reserve(qMin(itemLimit, count));
  qint64 bytes = 0;
  int files = 0;
  int folders = 0;
  int knownSizes = 0;
  int aggregateCount = 0;
  QHash<QString, int> kinds;

  const auto categoryFor = [](const QString &name, const QString &mime) {
    if (mime.startsWith(QLatin1String("image/")))
      return QStringLiteral("images");
    if (mime.startsWith(QLatin1String("video/")))
      return QStringLiteral("videos");
    if (mime.startsWith(QLatin1String("audio/")))
      return QStringLiteral("audio");
    const QString suffix = name.contains(QLatin1Char('.'))
                               ? name.section(QLatin1Char('.'), -1).toLower()
                               : QString();
    if (QStringList{QStringLiteral("zip"), QStringLiteral("tar"),
                    QStringLiteral("gz"), QStringLiteral("bz2"),
                    QStringLiteral("xz"), QStringLiteral("zst"),
                    QStringLiteral("7z"), QStringLiteral("rar")}
            .contains(suffix))
      return QStringLiteral("archives");
    if (QStringList{QStringLiteral("csv"), QStringLiteral("tsv"),
                    QStringLiteral("parquet"), QStringLiteral("duckdb"),
                    QStringLiteral("sqlite"), QStringLiteral("db")}
            .contains(suffix))
      return QStringLiteral("data");
    if (QStringList{QStringLiteral("md"), QStringLiteral("txt"),
                    QStringLiteral("pdf"), QStringLiteral("doc"),
                    QStringLiteral("docx"), QStringLiteral("odt")}
            .contains(suffix))
      return QStringLiteral("documents");
    if (QStringList{QStringLiteral("cpp"), QStringLiteral("h"),
                    QStringLiteral("qml"), QStringLiteral("js"),
                    QStringLiteral("ts"), QStringLiteral("py"),
                    QStringLiteral("rs"), QStringLiteral("go"),
                    QStringLiteral("java"), QStringLiteral("sh")}
            .contains(suffix))
      return QStringLiteral("code");
    return QStringLiteral("files");
  };

  for (int row : m_selected) {
    if (sampleRows.size() < itemLimit) {
      sampleRows.append(row);
      std::sort(sampleRows.begin(), sampleRows.end());
    } else if (row < sampleRows.constLast()) {
      sampleRows.last() = row;
      std::sort(sampleRows.begin(), sampleRows.end());
    }

    if (aggregateCount >= aggregateLimit)
      continue;
    const QModelIndex index = m_proxy->index(row, 0);
    if (!index.isValid())
      continue;
    ++aggregateCount;
    const bool isDir =
        m_proxy->data(index, DirectoryModel::IsDirRole).toBool();
    if (isDir) {
      ++folders;
      ++kinds[QStringLiteral("folders")];
      continue;
    }
    ++files;
    const qint64 size =
        m_proxy->data(index, DirectoryModel::SizeRole).toLongLong();
    if (size >= 0) {
      bytes += size;
      ++knownSizes;
    }
    const QString name =
        m_proxy->data(index, DirectoryModel::NameRole).toString();
    const QString mime =
        m_proxy->data(index, DirectoryModel::MimeRole).toString();
    ++kinds[categoryFor(name, mime)];
  }

  QVariantList items;
  items.reserve(sampleRows.size());
  for (int row : sampleRows)
    items.append(m_proxy->rowMap(row));

  QList<QPair<QString, int>> orderedKinds;
  orderedKinds.reserve(kinds.size());
  for (auto it = kinds.cbegin(); it != kinds.cend(); ++it)
    orderedKinds.append({it.key(), it.value()});
  std::sort(orderedKinds.begin(), orderedKinds.end(),
            [](const auto &a, const auto &b) {
              return a.second == b.second ? a.first < b.first
                                          : a.second > b.second;
            });
  QStringList kindSummary;
  for (int i = 0; i < qMin(3, orderedKinds.size()); ++i)
    kindSummary.append(QStringLiteral("%1 %2")
                           .arg(orderedKinds.at(i).second)
                           .arg(orderedKinds.at(i).first));

  const bool complete = aggregateCount == count;
  out.insert(QStringLiteral("items"), items);
  out.insert(QStringLiteral("files"), files);
  out.insert(QStringLiteral("folders"), folders);
  out.insert(QStringLiteral("bytes"), bytes);
  out.insert(QStringLiteral("knownSizes"), knownSizes);
  out.insert(QStringLiteral("aggregateCount"), aggregateCount);
  out.insert(QStringLiteral("aggregateComplete"), complete);
  out.insert(QStringLiteral("sizeComplete"), complete && knownSizes == files);
  out.insert(QStringLiteral("kindSummary"), kindSummary);
  return out;
}

QString SelectionModel::cursorPath() const {
  const QString external =
      m_externalItem.value(QStringLiteral("path")).toString();
  return external.isEmpty() ? pathAt(cursor()) : external;
}

QString SelectionModel::cursorName() const {
  const QString external =
      m_externalItem.value(QStringLiteral("name")).toString();
  return external.isEmpty() ? nameAt(cursor()) : external;
}

QVariantMap SelectionModel::primaryItem() const {
  if (!m_externalItem.isEmpty())
    return m_externalItem;
  const QList<int> rows = selectedInOrder();
  const int row = rows.isEmpty() ? cursor() : rows.constFirst();
  return m_proxy && row >= 0 ? m_proxy->rowMap(row) : QVariantMap();
}

void SelectionModel::onCursorChanged() {
  const int now = cursor();
  const QString oldName = m_cursorName;
  m_cursorName = nameAt(now);

  if (m_leaveDir) {
    if (now >= 0) {
      replaceSelected({now});
      m_leaveDir = false;
      setAnchor(now);
    }
    emit cursorChanged();
    emit statusTextChanged();
    return;
  }
  if (m_suppressFollow) {
    emit cursorChanged();
    return;
  }
  if (m_visual) {
    applyRange(m_anchor, now);
    emit cursorChanged();
    return;
  }
  if (m_selectedNames.size() == 1 && m_selectedNames.contains(oldName))
    replaceSelected(now >= 0 ? QSet<int>{now} : QSet<int>{});
  emit cursorChanged();
}

void SelectionModel::onPathChanged() {
  m_leaveDir = true;
  if (m_visual) {
    m_visual = false;
    emit visualChanged();
  }
  m_selected.clear();
  m_externalItem.clear();
  m_selectedNames.clear();
  m_cursorName.clear();
  emit selectionChanged();
  emit statusTextChanged();
}

void SelectionModel::rematch() {
  if (m_leaveDir)
    return;
  // Recursive 3D selections intentionally live outside the current proxy.
  // Listing batches and background sort/filter rematches must not silently
  // replace them with whichever direct child currently owns the cursor.
  if (!m_externalItem.isEmpty())
    return;
  const QStringList names = m_selectedNames;
  QSet<int> next;
  for (const QString &n : names) {
    const int r = findName(n);
    if (r >= 0)
      next.insert(r);
  }
  if (next.isEmpty() && cursor() >= 0)
    next.insert(cursor());
  replaceSelected(next);
  if (m_visual) {
    const int a = findName(m_anchorName);
    m_anchor = a >= 0 ? a : cursor();
  }
}

void SelectionModel::applyRange(int a, int b) {
  if (!m_proxy)
    return;
  const int n = m_proxy->count();
  if (n <= 0) {
    replaceSelected({});
    return;
  }
  a = qBound(0, a, n - 1);
  b = qBound(0, b, n - 1);
  if (a > b)
    std::swap(a, b);
  QSet<int> next;
  for (int i = a; i <= b; ++i)
    next.insert(i);
  replaceSelected(next);
}

void SelectionModel::replaceSelected(const QSet<int> &rows) {
  const bool hadExternal = clearExternalItem();
  if (!hadExternal && m_selected == rows) {
    syncNames();
    return;
  }
  m_selected = rows;
  syncNames();
  ++m_epoch;
  emit selectionChanged();
  emit epochChanged();
  emit statusTextChanged();
}

bool SelectionModel::clearExternalItem() {
  if (m_externalItem.isEmpty())
    return false;
  m_externalItem.clear();
  return true;
}

void SelectionModel::syncNames() {
  m_selectedNames.clear();
  const QList<int> rows = selectedInOrder();
  m_selectedNames.reserve(rows.size());
  for (int r : rows) {
    const QString n = nameAt(r);
    if (!n.isEmpty())
      m_selectedNames.append(n);
  }
}

int SelectionModel::findName(const QString &name) const {
  if (!m_proxy || name.isEmpty())
    return -1;
  for (int i = 0; i < m_proxy->count(); ++i) {
    if (nameAt(i) == name)
      return i;
  }
  return -1;
}

QString SelectionModel::nameAt(int proxyRow) const {
  if (!m_proxy || proxyRow < 0)
    return {};
  return m_proxy->data(m_proxy->index(proxyRow, 0), DirectoryModel::NameRole)
      .toString();
}

QString SelectionModel::pathAt(int proxyRow) const {
  if (!m_proxy || proxyRow < 0)
    return {};
  return m_proxy->data(m_proxy->index(proxyRow, 0), DirectoryModel::PathRole)
      .toString();
}

bool SelectionModel::isDirAt(int proxyRow) const {
  if (!m_proxy || proxyRow < 0)
    return false;
  return m_proxy->data(m_proxy->index(proxyRow, 0), DirectoryModel::IsDirRole)
      .toBool();
}

QList<int> SelectionModel::selectedInOrder() const {
  QList<int> rows = QList<int>(m_selected.begin(), m_selected.end());
  std::sort(rows.begin(), rows.end());
  return rows;
}
