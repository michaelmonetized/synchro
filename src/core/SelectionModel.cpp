#include "SelectionModel.h"

#include "DirectoryModel.h"
#include "FilterProxy.h"

#include <algorithm>

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

QString SelectionModel::statusText() const {
  const int n = m_proxy ? m_proxy->count() : 0;
  const QString files = QStringLiteral("%1 files").arg(n);
  if (m_selected.size() > 1)
    return QStringLiteral("%1 selected   %2").arg(m_selected.size()).arg(files);
  return files;
}

void SelectionModel::setCursor(int proxyRow) {
  if (m_proxy)
    m_proxy->setCurrentIndex(proxyRow);
}

void SelectionModel::moveCursor(int delta) {
  if (m_proxy)
    m_proxy->moveCursor(delta);
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
  const int size = m_selected.size();
  setAnchor(proxyRow);
  m_suppressFollow = true;
  setCursor(proxyRow);
  m_suppressFollow = false;
  if (size <= 1)
    replaceSelected({proxyRow});
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
  const int c = cursor();
  if (c >= 0)
    replaceSelected({c});
  else
    replaceSelected({});
}

void SelectionModel::activate() {
  if (!m_proxy || !m_model)
    return;
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
  const QList<int> rows = selectedInOrder();
  out.reserve(rows.size());
  for (int r : rows) {
    const QString p = pathAt(r);
    if (!p.isEmpty())
      out.append(p);
  }
  return out;
}

QString SelectionModel::cursorPath() const { return pathAt(cursor()); }

QString SelectionModel::cursorName() const { return nameAt(cursor()); }

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
  m_selectedNames.clear();
  m_cursorName.clear();
  emit selectionChanged();
  emit statusTextChanged();
}

void SelectionModel::rematch() {
  if (m_leaveDir)
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
  if (m_selected == rows) {
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
