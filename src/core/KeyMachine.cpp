#include "KeyMachine.h"

#include "DirectoryModel.h"
#include "FileOpEngine.h"
#include "FilterProxy.h"
#include "LocationChips.h"
#include "NavStack.h"
#include "PeekHost.h"
#include "RecentStore.h"
#include "SearchModel.h"
#include "SelectionModel.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFileInfo>

namespace {

constexpr int kSearchDebounceMs = 350;

bool hasCtrl(int modifiers) { return modifiers & Qt::ControlModifier; }

bool hasAlt(int modifiers) { return modifiers & Qt::AltModifier; }

bool hasMeta(int modifiers) { return modifiers & Qt::MetaModifier; }

bool hasShift(int modifiers) { return modifiers & Qt::ShiftModifier; }

bool hasChord(int modifiers) {
  return hasCtrl(modifiers) || hasMeta(modifiers);
}

QString expandTilde(const QString &text) {
  if (text == QLatin1Char('~'))
    return QDir::homePath();
  if (text.startsWith(QLatin1String("~/")))
    return QDir::homePath() + text.mid(1);
  return text;
}

} // namespace

KeyMachine::KeyMachine(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
                       QObject *parent)
    : QObject(parent), m_model(model), m_proxy(proxy), m_nav(nav) {
  m_searchDebounce.setSingleShot(true);
  m_searchDebounce.setInterval(kSearchDebounceMs);
  connect(&m_searchDebounce, &QTimer::timeout, this, &KeyMachine::runSearch);
  if (m_model)
    connect(m_model, &DirectoryModel::pathChanged, this,
            &KeyMachine::onPathChanged);
  if (m_nav)
    connect(m_nav, &NavStack::filterRestored, this, &KeyMachine::restoreField);
}

QString KeyMachine::mode() const {
  switch (m_mode) {
  case Mode::ListFocused:
    return QStringLiteral("list-focused");
  case Mode::FieldFilter:
    return QStringLiteral("field-filter");
  case Mode::FieldJump:
    return QStringLiteral("field-jump");
  case Mode::FieldCommand:
    return QStringLiteral("field-command");
  case Mode::FieldSearch:
    return QStringLiteral("field-search");
  case Mode::PeekOpen:
    return QStringLiteral("peek-open");
  case Mode::VisualSelect:
    return QStringLiteral("visual-select");
  case Mode::RenameInline:
    return QStringLiteral("rename-inline");
  case Mode::ConfirmDialog:
    return QStringLiteral("confirm-dialog");
  }
  return QStringLiteral("list-focused");
}

bool KeyMachine::ynPrompt() const {
  return m_promptKind == QLatin1String("unlink") ||
         m_promptKind == QLatin1String("empty-trash");
}

void KeyMachine::setChooserMode(bool on, bool multiple, bool save) {
  if (m_chooserMode == on && m_chooserMultiple == multiple &&
      m_chooserSave == save)
    return;
  m_chooserMode = on;
  m_chooserMultiple = multiple;
  m_chooserSave = on && save;
  if (!on)
    m_chooserPromptOpen = false;
  emit chooserModeChanged();
}

void KeyMachine::setChooserPromptOpen(bool on) {
  m_chooserPromptOpen = on && m_chooserMode;
}

void KeyMachine::setPromptText(const QString &text) {
  if (m_promptText == text)
    return;
  m_promptText = text;
  emit promptChanged();
}

bool KeyMachine::actionOpen() const {
  return m_host && m_host->actionOpen();
}

void KeyMachine::setSearchModel(SearchModel *search) {
  if (m_search == search)
    return;
  if (m_search)
    disconnect(m_search, nullptr, this, nullptr);
  m_search = search;
  if (!m_search)
    return;
  connect(m_search, &SearchModel::errorStringChanged, this, [this] {
    if (!m_search)
      return;
    if (!m_model || !DirectoryModel::isSearchPath(m_model->path()))
      return;
    const QString err = m_search->errorString();
    if (!err.isEmpty())
      setSearchStatusMessage(err);
  });
  connect(m_search, &SearchModel::listingChanged, this, [this] {
    if (!m_search || m_search->listing())
      return;
    // A search worker may finish after the user has already returned to a
    // real folder. Its summary only belongs to the search:// listing.
    if (!m_model || !DirectoryModel::isSearchPath(m_model->path()))
      return;
    if (!m_search->errorString().isEmpty())
      return;
    const int n = m_search->count();
    if (n <= 0 && !m_search->query().isEmpty())
      setSearchStatusMessage(QStringLiteral("no matches"));
    else if (n > 0) {
      const QString root = m_search->root();
      const QString home = QDir::homePath();
      QString where = root;
      if (root == home)
        where = QStringLiteral("~");
      else if (root.startsWith(home + QLatin1Char('/')))
        where = QLatin1Char('~') + root.mid(home.size());
      setSearchStatusMessage(
          (m_search->contentSearch()
               ? QStringLiteral("%1 content matches in %2")
               : QStringLiteral("%1 matches in %2"))
              .arg(n)
              .arg(where));
    }
  });
}

void KeyMachine::setPeekHost(PeekHost *host) {
  if (m_host == host)
    return;
  if (m_host)
    disconnect(m_host, nullptr, this, nullptr);
  m_host = host;
  if (!m_host)
    return;
  connect(m_host, &PeekHost::openChanged, this, [this] {
    if (m_host->isOpen())
      setMode(Mode::PeekOpen);
    else if (m_mode == Mode::PeekOpen)
      setMode(Mode::ListFocused);
  });
  connect(m_host, &PeekHost::actionOpenChanged, this,
          &KeyMachine::actionOpenChanged);
}

void KeyMachine::setMode(Mode mode) {
  if (m_mode == mode)
    return;
  m_mode = mode;
  emit modeChanged();
}

void KeyMachine::setFieldText(const QString &text) {
  if (m_fieldText == text)
    return;
  m_fieldText = text;
  applyFieldText();
  emit fieldTextChanged();
  // Stale hint only belongs to the failed query still in the field.
  if (!m_status.isEmpty())
    setStatusMessage(QString());
}

void KeyMachine::setGridMode(bool on) {
  if (m_fsnMode)
    setFsnMode(false);
  if (m_gridMode == on)
    return;
  m_gridMode = on;
  if (!on)
    setGridStride(1);
  emit gridModeChanged();
}

void KeyMachine::setFsnMode(bool on) {
  if (m_chooserMode)
    on = false;
  if (m_fsnMode == on)
    return;
  m_fsnMode = on;
  emit fsnModeChanged();
}

void KeyMachine::toggleFsnMode() {
  if (m_chooserMode) {
    setStatusMessage(QStringLiteral("fsn is a main-window toy"));
    return;
  }
  setFsnMode(!m_fsnMode);
  setStatusMessage(m_fsnMode ? QStringLiteral("it's a unix system")
                             : QString());
}

void KeyMachine::setFsnTreeView(bool tree) {
  if (m_fsnTreeView == tree)
    return;
  m_fsnTreeView = tree;
  emit fsnTreeViewChanged();
}

void KeyMachine::setPanelId(const QString &id) {
  const QString next = m_chooserMode ? QString() : id;
  if (m_panelId == next)
    return;
  m_panelId = next;
  emit panelChanged();
}

void KeyMachine::setPanelSide(const QString &side) {
  QString next = QStringLiteral("bottom");
  if (side == QLatin1String("left") || side == QLatin1String("right") ||
      side == QLatin1String("top"))
    next = side;
  if (m_panelSide == next)
    return;
  m_panelSide = next;
  emit panelChanged();
}

void KeyMachine::setPanelFocused(bool on) {
  if (m_panelFocused == on)
    return;
  m_panelFocused = on;
  emit panelFocusedChanged();
}

void KeyMachine::setLookKeyMode(bool on) {
  if (m_lookKeyMode == on)
    return;
  m_lookKeyMode = on;
  emit lookKeyModeChanged();
}

void KeyMachine::togglePanel(const QString &id) {
  setPanelId(m_panelId == id ? QString() : id);
}

void KeyMachine::setGridStride(int columns) {
  const int next = qMax(1, columns);
  if (m_gridStride == next)
    return;
  m_gridStride = next;
  emit gridStrideChanged();
}

void KeyMachine::moveGridCursorTo(int index) { setCursorIndex(index); }

void KeyMachine::setCursorIndex(int index) {
  if (m_selection)
    m_selection->setCursor(index);
  else if (m_proxy)
    m_proxy->setCurrentIndex(index);
  else if (m_model)
    m_model->setCurrentIndex(index);
}

int KeyMachine::cursorIndex() const {
  if (m_proxy)
    return m_proxy->currentIndex();
  if (m_model)
    return m_model->currentIndex();
  return -1;
}

void KeyMachine::nudgeCursor(int dx, int dy, bool leap) {
  const int step = leap ? 5 : 1;
  if (m_gridMode && m_model && m_model->isSearch()) {
    const int next = m_model->stepSearchGrid(
        cursorIndex(), dx * step, dy * step, m_gridStride);
    setCursorIndex(next);
    return;
  }
  if (m_gridMode) {
    const int count = m_proxy ? m_proxy->count()
                              : (m_model ? m_model->rowCount() : 0);
    if (count <= 0)
      return;
    const int stride = qMax(1, m_gridStride);
    const int current = qBound(0, cursorIndex(), count - 1);
    const int row = current / stride;
    const int column = current % stride;
    int next = current;
    if (dx != 0) {
      const int rowFirst = row * stride;
      const int rowLast = qMin(count - 1, rowFirst + stride - 1);
      next = qBound(rowFirst, current + dx * step, rowLast);
    } else if (dy != 0) {
      const int lastRow = (count - 1) / stride;
      const int targetRow = qBound(0, row + dy * step, lastRow);
      const int targetFirst = targetRow * stride;
      const int targetLast = qMin(count - 1, targetFirst + stride - 1);
      next = targetFirst + qMin(column, targetLast - targetFirst);
    }
    setCursorIndex(next);
    return;
  }
  const int delta = dy * step + dx * step;
  if (delta == 0)
    return;
  if (m_selection)
    m_selection->moveCursor(delta);
  else if (m_proxy)
    m_proxy->moveCursor(delta);
  else if (m_model)
    m_model->moveCursor(delta);
}

void KeyMachine::setStatusMessage(const QString &text) {
  m_searchStatus = false;
  if (m_status == text)
    return;
  m_status = text;
  emit statusMessageChanged();
}

void KeyMachine::setSearchStatusMessage(const QString &text) {
  m_searchStatus = !text.isEmpty();
  if (m_status == text)
    return;
  m_status = text;
  emit statusMessageChanged();
}

void KeyMachine::setHelpOpen(bool on) {
  if (m_helpOpen == on)
    return;
  m_helpOpen = on;
  emit helpOpenChanged();
}

void KeyMachine::registerAction(const QString &id, const QString &title) {
  m_palette.registerAction(id, title);
}

void KeyMachine::applyFieldText() {
  // Leading ':' is the palette, never a filter. Builtins win over action :id.
  if (isCommandText(m_fieldText)) {
    cancelSearch();
    if (m_proxy)
      m_proxy->setFilter(QString());
    if (m_nav)
      m_nav->setLiveFilter(QString());
    if (m_mode == Mode::FieldFilter || m_mode == Mode::FieldJump ||
        m_mode == Mode::FieldSearch)
      setMode(Mode::FieldCommand);
    return;
  }
  if (isSearchText(m_fieldText)) {
    if (m_proxy)
      m_proxy->setFilter(QString());
    if (m_nav)
      m_nav->setLiveFilter(QString());
    if (m_mode == Mode::FieldFilter || m_mode == Mode::FieldJump ||
        m_mode == Mode::FieldCommand)
      setMode(Mode::FieldSearch);
    if (m_mode == Mode::FieldSearch)
      scheduleSearch();
    return;
  }
  cancelSearch();
  if (m_mode == Mode::FieldCommand || m_mode == Mode::FieldSearch)
    setMode(Mode::FieldFilter);
  if (!m_proxy)
    return;
  const QString cwd = m_model ? m_model->path() : QString();
  // Unsigiled text always filters — even when a same-named directory exists.
  if (isJumpText(m_fieldText, cwd))
    m_proxy->setFilter(QString());
  else
    m_proxy->setFilter(m_fieldText);
  if (m_nav)
    m_nav->setLiveFilter(m_proxy->filter());
}

void KeyMachine::clearFieldAndFilter() {
  m_searchDebounce.stop();
  m_fieldText.clear();
  if (m_proxy)
    m_proxy->setFilter(QString());
  if (m_nav)
    m_nav->setLiveFilter(QString());
  emit fieldTextChanged();
  if (m_promptKind.isEmpty() && !m_status.isEmpty())
    setStatusMessage(QString());
}

void KeyMachine::beginLocalFilter() {
  if (m_localFilterSession)
    return;
  m_localFilterSession = true;
  m_filterRestoreSourceRow = m_model ? m_model->currentIndex() : -1;
  m_filterRestorePath = m_model ? m_model->path() : QString();
  m_filterRestoreItemPath.clear();
  if (m_model && m_filterRestoreSourceRow >= 0) {
    m_filterRestoreItemPath =
        m_model->data(m_model->index(m_filterRestoreSourceRow, 0),
                      DirectoryModel::PathRole)
            .toString();
  }
  emit localFilterStarted();
}

void KeyMachine::cancelLocalFilter() {
  const bool restore = m_localFilterSession && m_model && m_proxy &&
                       m_model->path() == m_filterRestorePath;
  const int sourceRow = m_filterRestoreSourceRow;
  const QString itemPath = m_filterRestoreItemPath;
  clearFieldAndFilter();

  if (restore) {
    int row = -1;
    if (sourceRow >= 0 && sourceRow < m_model->rowCount()) {
      const QModelIndex source = m_model->index(sourceRow, 0);
      if (itemPath.isEmpty() ||
          m_model->data(source, DirectoryModel::PathRole).toString() ==
              itemPath)
        row = m_proxy->mapFromSource(source).row();
    }
    if (row < 0 && !itemPath.isEmpty()) {
      for (int i = 0; i < m_proxy->rowCount(); ++i) {
        if (m_proxy->data(m_proxy->index(i, 0), DirectoryModel::PathRole)
                .toString() == itemPath) {
          row = i;
          break;
        }
      }
    }
    if (row >= 0)
      setCursorIndex(row);
  }

  m_localFilterSession = false;
  m_filterRestoreSourceRow = -1;
  m_filterRestorePath.clear();
  m_filterRestoreItemPath.clear();
  setMode(Mode::ListFocused);
  emit localFilterCanceled();
}

void KeyMachine::activateCurrent() {
  if (m_chooserMode) {
    emit chooserAcceptRequested();
    return;
  }
  if (m_selection)
    m_selection->activate();
  else if (m_proxy)
    m_proxy->activateCurrent();
  else if (m_model)
    m_model->activateCurrent();
}

void KeyMachine::onPathChanged() {
  m_localFilterSession = false;
  m_filterRestoreSourceRow = -1;
  m_filterRestorePath.clear();
  m_filterRestoreItemPath.clear();
  if (m_selection)
    m_selection->exitVisual();
  if (!m_promptKind.isEmpty() || m_mode == Mode::RenameInline ||
      m_mode == Mode::ConfirmDialog) {
    clearConfirm();
    setStatusMessage(QString());
  }
  // Entering search:// is the field-search destination; keep `?query`.
  if (m_model && DirectoryModel::isSearchPath(m_model->path()))
    return;
  if (m_searchStatus)
    setSearchStatusMessage(QString());
  if (m_holdSearchField)
    return;
  m_searchAnchor.clear();
  if (m_nav && m_nav->restoring()) {
    setMode(Mode::ListFocused);
    return;
  }
  if (!m_fieldText.isEmpty() || (m_proxy && !m_proxy->filter().isEmpty())) {
    m_fieldText.clear();
    if (m_proxy)
      m_proxy->setFilter(QString());
    emit fieldTextChanged();
  }
  if (m_nav)
    m_nav->setLiveFilter(QString());
  setMode(Mode::ListFocused);
}

void KeyMachine::restoreField(const QString &text) {
  if (m_holdSearchField)
    return;
  if (m_fieldText != text) {
    m_fieldText = text;
    emit fieldTextChanged();
  }
  applyFieldText();
  setMode(Mode::ListFocused);
}

void KeyMachine::closePeek() {
  if (m_host && m_host->isOpen())
    m_host->close();
}

void KeyMachine::closeAction() {
  if (m_host && m_host->actionOpen())
    m_host->closeAction();
}

void KeyMachine::closeOverlays() {
  closePeek();
  closeAction();
}

void KeyMachine::focusFilter() {
  closeOverlays();
  setHelpOpen(false);
  if (m_selection)
    m_selection->exitVisual();
  beginLocalFilter();
  setMode(Mode::FieldFilter);
  applyFieldText();
}

void KeyMachine::focusJump() {
  closeOverlays();
  setHelpOpen(false);
  const QString path = m_model ? m_model->path() : QString();
  setMode(Mode::FieldJump);
  if (m_fieldText != path) {
    m_fieldText = path;
    applyFieldText();
    emit fieldTextChanged();
  } else {
    applyFieldText();
  }
  ++m_jumpEpoch;
  emit jumpEpochChanged();
}

void KeyMachine::focusCommand() {
  closeOverlays();
  setHelpOpen(false);
  setStatusMessage(QString());
  if (m_fieldText != QLatin1String(":")) {
    m_fieldText = QStringLiteral(":");
    emit fieldTextChanged();
  }
  applyFieldText();
  setMode(Mode::FieldCommand);
}

void KeyMachine::focusSearch() {
  closeOverlays();
  setHelpOpen(false);
  if (m_selection)
    m_selection->exitVisual();
  if (!isSearchText(m_fieldText)) {
    m_fieldText = QStringLiteral("?");
    emit fieldTextChanged();
  }
  applyFieldText();
  setMode(Mode::FieldSearch);
}

void KeyMachine::toggleSearchField() {
  if (m_mode == Mode::FieldSearch) {
    const QString q = searchQuery(m_fieldText);
    if (q.isEmpty()) {
      clearFieldAndFilter();
      setMode(Mode::ListFocused);
      return;
    }
    m_searchDebounce.stop();
    // Same live query: just hop focus. runSearch() would no-op on
    // query match, but skip the call so Tab cannot retrigger start().
    if (!m_search || m_search->query() != q ||
        m_search->root() != searchRoot() ||
        m_search->contentSearch() != isContentSearchText(m_fieldText) ||
        (m_model && !DirectoryModel::isSearchPath(m_model->path())))
      runSearch();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_chooserMode && m_chooserSave && !isSearchText(m_fieldText)) {
    emit saveNameFocusRequested();
    return;
  }
  focusSearch();
}

void KeyMachine::focusList() {
  closeOverlays();
  setHelpOpen(false);
  setMode(Mode::ListFocused);
}

void KeyMachine::clearConfirm() {
  if (m_promptKind.isEmpty() && m_promptText.isEmpty())
    return;
  m_promptKind.clear();
  m_promptText.clear();
  emit promptChanged();
}

void KeyMachine::startRename() {
  if (m_chooserMode || (m_model && m_model->isTrash()))
    return;
  QString name;
  if (m_selection)
    name = m_selection->cursorName();
  if (name.isEmpty() && m_proxy)
    name = m_proxy->currentName();
  if (name.isEmpty() && m_model)
    name = m_model->currentName();
  if (name.isEmpty())
    return;
  if (m_selection)
    m_selection->exitVisual();
  m_promptKind = QStringLiteral("rename");
  m_promptText = name;
  setMode(Mode::RenameInline);
  emit promptChanged();
}

void KeyMachine::startMkdir() {
  if (m_chooserMode || (m_model && DirectoryModel::isVirtualPath(
                                       m_model ? m_model->path() : QString())))
    return;
  if (m_selection)
    m_selection->exitVisual();
  m_promptKind = QStringLiteral("mkdir");
  m_promptText = QStringLiteral("New folder");
  emit promptChanged();
  setMode(Mode::ConfirmDialog);
}

void KeyMachine::startUnlinkConfirm() {
  if (m_chooserMode)
    return;
  const int n = m_selection ? m_selection->selectedCount() : 0;
  const QString cursor = m_selection ? m_selection->cursorPath() : QString();
  if (n <= 0 && cursor.isEmpty())
    return;
  if (m_selection)
    m_selection->exitVisual();
  m_promptKind = QStringLiteral("unlink");
  m_promptText = n > 1 ? QStringLiteral("Permanently delete %1 items?").arg(n)
                       : QStringLiteral("Permanently delete?");
  emit promptChanged();
  setMode(Mode::ConfirmDialog);
}

void KeyMachine::startEmptyConfirm() {
  if (!m_model || !m_model->isTrash())
    return;
  if (m_selection)
    m_selection->exitVisual();
  m_promptKind = QStringLiteral("empty-trash");
  m_promptText = QStringLiteral("Empty trash?");
  emit promptChanged();
  setMode(Mode::ConfirmDialog);
}

void KeyMachine::requestEmptyTrash() {
  if (!m_model || !m_model->isTrash()) {
    setStatusMessage(QStringLiteral("empty is only available in trash"));
    return;
  }
  startEmptyConfirm();
}

void KeyMachine::acceptEmptyTrash() {
  if (m_fileOps) {
    m_fileOps->emptyTrash();
    return;
  }
  if (!m_model || !m_model->isTrash()) {
    setStatusMessage(QStringLiteral("empty is only available in trash"));
    return;
  }
  if (!m_model->emptyTrash()) {
    setStatusMessage(m_model->errorString().isEmpty()
                         ? QStringLiteral("empty is not available")
                         : m_model->errorString());
    return;
  }
  setStatusMessage(QStringLiteral("emptied trash"));
}

void KeyMachine::acceptPrompt() {
  const QString text = m_promptText.trimmed();
  if (m_mode == Mode::RenameInline) {
    if (m_fileOps)
      m_fileOps->renameCursor(text);
    clearConfirm();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_mode != Mode::ConfirmDialog)
    return;
  if (m_promptKind == QLatin1String("mkdir")) {
    if (m_fileOps)
      m_fileOps->mkdirHere(text);
    clearConfirm();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_promptKind == QLatin1String("unlink")) {
    if (m_fileOps)
      m_fileOps->unlinkSelection();
    clearConfirm();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_promptKind == QLatin1String("empty-trash")) {
    acceptEmptyTrash();
    clearConfirm();
    setMode(Mode::ListFocused);
  }
}

bool KeyMachine::handleChooserPromptKey(int key, int modifiers) {
  if (!m_chooserMode || !m_chooserPromptOpen)
    return false;
  if (hasChord(modifiers) || hasAlt(modifiers))
    return true;
  if (key == Qt::Key_Escape) {
    emit chooserPromptDismissRequested();
    return true;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Y) {
    emit chooserAcceptRequested();
    return true;
  }
  if (key == Qt::Key_N)
    emit chooserPromptDismissRequested();
  return true;
}

bool KeyMachine::handleConfirmKey(int key, int modifiers, const QString &text) {
  if (m_mode != Mode::ConfirmDialog && m_mode != Mode::RenameInline)
    return false;
  if (hasChord(modifiers) || hasAlt(modifiers))
    return true;
  if (key == Qt::Key_Escape) {
    escape();
    return true;
  }
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !ynPrompt()) {
    acceptPrompt();
    return true;
  }
  if (ynPrompt()) {
    const QString t = text.toLower();
    if (key == Qt::Key_Y || t == QLatin1String("y")) {
      acceptPrompt();
      return true;
    }
    if (key == Qt::Key_N || t == QLatin1String("n")) {
      escape();
      return true;
    }
    return true;
  }
  return false;
}

void KeyMachine::escape() {
  if (m_mode == Mode::RenameInline || m_mode == Mode::ConfirmDialog ||
      !m_promptKind.isEmpty()) {
    clearConfirm();
    setStatusMessage(QString());
    setMode(Mode::ListFocused);
    return;
  }
  if (m_helpOpen) {
    setHelpOpen(false);
    return;
  }
  if (m_host && m_host->actionOpen()) {
    closeAction();
    return;
  }
  if (m_mode == Mode::PeekOpen || (m_host && m_host->isOpen())) {
    closePeek();
    return;
  }
  if (m_mode == Mode::VisualSelect) {
    if (m_selection)
      m_selection->exitVisual();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_mode != Mode::ListFocused) {
    if (m_mode == Mode::FieldFilter) {
      cancelLocalFilter();
      return;
    }
    if (m_mode == Mode::FieldSearch || isSearchText(m_fieldText))
      cancelSearch();
    // ':' / '?' are chrome (same as an empty filter). Only a real query is step 4.
    if (!fieldQueryEmpty()) {
      clearFieldAndFilter();
      return;
    }
    clearFieldAndFilter();
    setMode(Mode::ListFocused);
    return;
  }
  if (m_selection && m_selection->selectedCount() > 1) {
    m_selection->collapseToCursor();
    return;
  }
  if (!m_fieldText.isEmpty() || (m_proxy && !m_proxy->filter().isEmpty())) {
    if (isSearchText(m_fieldText))
      cancelSearch();
    if (m_localFilterSession)
      cancelLocalFilter();
    else
      clearFieldAndFilter();
    return;
  }
  if (m_fsnMode) {
    setFsnMode(false);
    return;
  }
  if (m_chooserMode)
    emit dismissRequested();
}

void KeyMachine::acceptField() {
  if (m_mode == Mode::FieldCommand || isCommandText(m_fieldText)) {
    runCommand(m_fieldText);
    return;
  }
  if (m_mode == Mode::FieldSearch || isSearchText(m_fieldText)) {
    m_searchDebounce.stop();
    runSearch();
    if (!searchQuery(m_fieldText).isEmpty())
      setMode(Mode::ListFocused);
    return;
  }
  const QString cwd = m_model ? m_model->path() : QString();
  if (isJumpText(m_fieldText, cwd)) {
    const QString dest = resolveJump(m_fieldText, cwd);
    if (dest.isEmpty())
      return;
    if (m_nav)
      m_nav->navigate(dest);
    else if (m_model)
      m_model->setPath(dest);
    setMode(Mode::ListFocused);
    return;
  }
  applyFieldText();
  setMode(Mode::ListFocused);
  if (!m_fieldText.isEmpty() && (!m_proxy || m_proxy->rowCount() > 0))
    activateCurrent();
}

bool KeyMachine::isCommandText(const QString &text) {
  return text.trimmed().startsWith(QLatin1Char(':'));
}

bool KeyMachine::isSearchText(const QString &text) {
  return text.startsWith(QLatin1Char('?'));
}

bool KeyMachine::isContentSearchText(const QString &text) {
  return text.startsWith(QLatin1String("??"));
}

QString KeyMachine::searchQuery(const QString &text) {
  if (isContentSearchText(text))
    return text.mid(2);
  if (isSearchText(text))
    return text.mid(1);
  return {};
}

bool KeyMachine::fieldQueryEmpty() const {
  if (m_mode == Mode::FieldCommand || isCommandText(m_fieldText))
    return CommandPalette::stripSigil(m_fieldText).isEmpty();
  if (m_mode == Mode::FieldSearch || isSearchText(m_fieldText))
    return searchQuery(m_fieldText).isEmpty();
  return m_fieldText.isEmpty();
}

void KeyMachine::scheduleSearch() {
  // Kill the previous walk immediately; debounce only starts the next one.
  if (m_search)
    m_search->cancel();
  m_searchDebounce.start();
}

QString KeyMachine::searchRoot() const {
  if (!m_searchAnchor.isEmpty())
    return m_searchAnchor;
  if (!m_model)
    return QDir::homePath();
  const QString loc = m_model->path();
  if (DirectoryModel::isSearchPath(loc)) {
    if (m_search && !m_search->root().isEmpty())
      return m_search->root();
    if (!m_model->returnPath().isEmpty())
      return m_model->returnPath();
  } else if (!DirectoryModel::isVirtualPath(loc) && !loc.isEmpty()) {
    return loc;
  }
  if (!m_model->returnPath().isEmpty())
    return m_model->returnPath();
  return QDir::homePath();
}

void KeyMachine::cancelSearch() {
  m_searchDebounce.stop();
  if (m_search)
    m_search->cancel();
}

void KeyMachine::leaveSearchListing() {
  if (!m_model || !DirectoryModel::isSearchPath(m_model->path()))
    return;
  m_holdSearchField = true;
  if (m_nav && m_nav->canGoBack())
    m_nav->goBack();
  else {
    const QString dest = !m_searchAnchor.isEmpty()
                             ? m_searchAnchor
                             : (!m_model->returnPath().isEmpty()
                                    ? m_model->returnPath()
                                    : QDir::homePath());
    m_model->setPath(dest);
  }
  m_holdSearchField = false;
  if (isSearchText(m_fieldText))
    setMode(Mode::FieldSearch);
}

void KeyMachine::runSearch() {
  const QString query = searchQuery(m_fieldText);
  if (query.isEmpty()) {
    cancelSearch();
    if (m_search)
      m_search->clear();
    leaveSearchListing();
    return;
  }
  if (isContentSearchText(m_fieldText) &&
      query.size() < kMinContentQueryChars) {
    cancelSearch();
    if (m_search)
      m_search->clear();
    setSearchStatusMessage(
        QStringLiteral("type %1+ characters for content search")
            .arg(kMinContentQueryChars));
    return;
  }
  if (!isContentSearchText(m_fieldText) &&
      query.size() < kMinNameQueryChars) {
    cancelSearch();
    if (m_search)
      m_search->clear();
    setSearchStatusMessage(
        QStringLiteral("type %1+ characters to search")
            .arg(kMinNameQueryChars));
    return;
  }
  const QString loc = m_model ? m_model->path() : QString();
  if (DirectoryModel::isVirtualPath(loc) && !DirectoryModel::isSearchPath(loc)) {
    setSearchStatusMessage(QStringLiteral("search is not available"));
    return;
  }
  if (!m_search) {
    setSearchStatusMessage(QStringLiteral("search is not available"));
    return;
  }
  if (!DirectoryModel::isSearchPath(loc) &&
      !DirectoryModel::isVirtualPath(loc) && !loc.isEmpty())
    m_searchAnchor = loc;
  const QString root = searchRoot();
  if (m_model && !DirectoryModel::isSearchPath(m_model->path())) {
    if (m_nav)
      m_nav->navigate(QStringLiteral("search://"));
    else
      m_model->setPath(QStringLiteral("search://"));
  } else if (m_search->query() == query && m_search->root() == root &&
             m_search->contentSearch() == isContentSearchText(m_fieldText))
    return;
  m_search->start(query, root, m_model && m_model->showHidden(),
                  isContentSearchText(m_fieldText));
}

void KeyMachine::revealCurrent() {
  if (!m_model)
    return;
  QString path = m_model->currentOrigPath();
  if (path.isEmpty()) {
    const QAbstractItemModel *src =
        m_proxy ? static_cast<const QAbstractItemModel *>(m_proxy)
                : static_cast<const QAbstractItemModel *>(m_model);
    const int row = m_proxy ? m_proxy->currentIndex() : m_model->currentIndex();
    if (src && row >= 0)
      path = src->data(src->index(row, 0), DirectoryModel::PathRole).toString();
  }
  if (path.isEmpty())
    return;
  const QFileInfo fi(path);
  QDir dir(fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
  if (fi.isDir() && !dir.cdUp())
    return;
  const QString dest = dir.absolutePath();
  if (dest.isEmpty() || dest == m_model->path())
    return;
  m_model->setPath(dest, fi.fileName());
}

void KeyMachine::finishCommand() {
  clearFieldAndFilter();
  if (m_mode == Mode::ConfirmDialog || m_mode == Mode::RenameInline)
    return;
  if (m_promptKind.isEmpty())
    setStatusMessage(QString());
  setMode(Mode::ListFocused);
}

void KeyMachine::runCommand(const QString &text) {
  const QString stripped = CommandPalette::stripSigil(text);
  if (stripped.compare(QLatin1String("sort"), Qt::CaseInsensitive) == 0 ||
      stripped.startsWith(QLatin1String("sort "), Qt::CaseInsensitive)) {
    QString info;
    if (!applySortCommand(stripped, &info))
      return;
    finishCommand();
    if (!info.isEmpty())
      setStatusMessage(info);
    return;
  }
  const QStringList fsnToks =
      stripped.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (!fsnToks.isEmpty()) {
    const QString head = fsnToks.first().toLower();
    if (head == QLatin1String("panel")) {
      if (m_chooserMode) {
        setStatusMessage(QStringLiteral("no panels in picker windows"));
        return;
      }
      if (fsnToks.size() < 2) {
        setStatusMessage(QStringLiteral(":panel <id> — e.g. :panel duckdb"));
        return;
      }
      QString id = fsnToks.at(1);
      if (id == QLatin1String("off")) {
        setPanelId(QString());
        finishCommand();
        return;
      }
      if (!id.contains(QLatin1Char('.')))
        id = QStringLiteral("synchro.panel.") + id;
      togglePanel(id);
      finishCommand();
      if (!m_panelId.isEmpty())
        emit panelFocusRequested();
      return;
    }
    // ":term" is the embedded panel; ":terminal" stays the external
    // terminal action handler.
    if (head == QLatin1String("term")) {
      if (m_chooserMode) {
        setStatusMessage(QStringLiteral("no terminal in picker windows"));
        return;
      }
      const QString id = QStringLiteral("synchro.panel.terminal");
      if (fsnToks.size() > 1) {
        const QString arg = fsnToks.at(1).toLower();
        if (arg == QLatin1String("bottom") || arg == QLatin1String("left") ||
            arg == QLatin1String("right") || arg == QLatin1String("top")) {
          setPanelSide(arg);
          setPanelId(id);
        } else if (arg == QLatin1String("off")) {
          setPanelId(QString());
        } else {
          setStatusMessage(QStringLiteral(":term [top|bottom|left|right|off]"));
          return;
        }
      } else {
        togglePanel(id);
      }
      finishCommand();
      if (!m_panelId.isEmpty())
        emit panelFocusRequested();
      return;
    }
    if (head == QLatin1String("sql")) {
      if (m_chooserMode) {
        setStatusMessage(QStringLiteral("no SQL workbench in picker windows"));
        return;
      }
      const bool forceScan = fsnToks.size() == 2 &&
                             fsnToks.at(1).compare(
                                 QLatin1String("scan"),
                                 Qt::CaseInsensitive) == 0;
      if (fsnToks.size() > 1 && !forceScan) {
        setStatusMessage(QStringLiteral(":sql [scan]"));
        return;
      }
      setPanelId(QStringLiteral("synchro.panel.sql"));
      finishCommand();
      if (forceScan)
        emit sqlScanRequested();
      emit panelFocusRequested();
      return;
    }
    if (head == QLatin1String("ask") || head == QLatin1String("find-agent") ||
        head == QLatin1String("agent-find")) {
      if (m_chooserMode) {
        setStatusMessage(QStringLiteral("no agent search in picker windows"));
        return;
      }
      if (fsnToks.size() > 1) {
        setStatusMessage(QStringLiteral(":ask"));
        return;
      }
      finishCommand();
      emit agentSearchRequested();
      return;
    }
    if (head == QLatin1String("flow") || head == QLatin1String("flows") ||
        head == QLatin1String("omaflow")) {
      if (m_chooserMode) {
        setStatusMessage(QStringLiteral("no automation panel in picker windows"));
        return;
      }
      if (fsnToks.size() > 1) {
        setStatusMessage(QStringLiteral(":flow"));
        return;
      }
      setPanelId(QStringLiteral("synchro.panel.omaflow"));
      finishCommand();
      emit panelFocusRequested();
      return;
    }
    if (head == QLatin1String("fsn") || head == QLatin1String("fsv") ||
        head == QLatin1String("park") || head == QLatin1String("nedry")) {
      if (fsnToks.size() > 1) {
        const QString arg = fsnToks.at(1).toLower();
        if (arg == QLatin1String("tree") || arg == QLatin1String("treev"))
          setFsnTreeView(true);
        else if (arg == QLatin1String("map") || arg == QLatin1String("mapv"))
          setFsnTreeView(false);
        else {
          setStatusMessage(QStringLiteral(":fsv tree|map"));
          return;
        }
      }
      QString info;
      if (!runBuiltin(QStringLiteral("fsn"), &info))
        return;
      finishCommand();
      if (!info.isEmpty())
        setStatusMessage(info);
      return;
    }
  }
  CommandSpec spec;
  QString err;
  if (!m_palette.resolve(text, &spec, &err)) {
    setStatusMessage(err);
    return;
  }
  if (spec.builtin) {
    QString info;
    if (!runBuiltin(spec.id, &info))
      return;
    finishCommand();
    if (!info.isEmpty())
      setStatusMessage(info);
    return;
  }
  if (!m_actionRunner) {
    setStatusMessage(QStringLiteral("unknown command"));
    return;
  }
  if (!m_actionRunner(spec.id, &err)) {
    setStatusMessage(err.isEmpty() ? QStringLiteral("command failed") : err);
    return;
  }
  finishCommand();
}

bool KeyMachine::runBuiltin(const QString &id, QString *info) {
  if (id == QLatin1String("home")) {
    if (m_nav)
      m_nav->goHome();
    else if (m_model)
      m_model->setPath(QDir::homePath());
    return true;
  }
  if (id == QLatin1String("hidden")) {
    if (m_model) {
      m_model->setShowHidden(!m_model->showHidden());
      if (info)
        *info = m_model->showHidden() ? QStringLiteral("hidden on")
                                      : QStringLiteral("hidden off");
    }
    return true;
  }
  if (id == QLatin1String("sort")) {
    return applySortCommand(QStringLiteral("sort"), info);
  }
  if (id == QLatin1String("all") || id == QLatin1String("files") ||
      id == QLatin1String("folders")) {
    if (m_proxy) {
      m_proxy->setKindFilter(id);
      if (info) {
        if (id == QLatin1String("files"))
          *info = QStringLiteral("files only");
        else if (id == QLatin1String("folders"))
          *info = QStringLiteral("folders only");
        else
          *info = QStringLiteral("showing all");
      }
    }
    return true;
  }
  if (id == QLatin1String("grid")) {
    setGridMode(true);
    return true;
  }
  if (id == QLatin1String("list")) {
    setGridMode(false);
    return true;
  }
  if (id == QLatin1String("fsn") || id == QLatin1String("fsv") ||
      id == QLatin1String("park") || id == QLatin1String("nedry")) {
    if (m_chooserMode) {
      if (info)
        *info = QStringLiteral("fsn is a main-window toy");
      return true;
    }
    setFsnMode(true);
    if (info)
      *info = QStringLiteral("it's a unix system");
    return true;
  }
  if (id == QLatin1String("help") || id == QLatin1String("?")) {
    setHelpOpen(true);
    return true;
  }
  if (id == QLatin1String("trash")) {
    if (!m_trashAvailable) {
      if (info)
        *info = QStringLiteral("trash is not available");
      return true;
    }
    if (m_nav)
      m_nav->navigate(QStringLiteral("trash://"));
    else if (m_model)
      m_model->setPath(QStringLiteral("trash://"));
    return true;
  }
  if (id == QLatin1String("recent"))
    return runRecent(info);
  if (id == QLatin1String("volumes")) {
    if (m_nav)
      m_nav->goVolumes();
    else if (m_model)
      m_model->setPath(QStringLiteral("volumes://"));
    return true;
  }
  if (id == QLatin1String("pin") || id == QLatin1String("unpin"))
    return runPinCommand(id, info);
  if (id == QLatin1String("empty")) {
    if (!m_model || !m_model->isTrash()) {
      if (info)
        *info = QStringLiteral("empty is only available in trash");
      return true;
    }
    requestEmptyTrash();
    return true;
  }
  setStatusMessage(QStringLiteral("unknown command"));
  return false;
}

bool KeyMachine::applySortCommand(const QString &text, QString *info) {
  if (!m_proxy) {
    setStatusMessage(QStringLiteral("sort is not available"));
    return false;
  }
  const QStringList parts =
      text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  QString role = m_proxy->sortRoleName();
  QString order = m_proxy->sortOrder();
  bool touched = false;
  for (int i = 1; i < parts.size(); ++i) {
    const QString p = parts.at(i).toLower();
    if (p == QLatin1String("name") || p == QLatin1String("size") ||
        p == QLatin1String("mtime") || p == QLatin1String("type")) {
      role = p;
      touched = true;
    } else if (p == QLatin1String("asc") || p == QLatin1String("desc")) {
      order = p;
      touched = true;
    } else {
      setStatusMessage(QStringLiteral("sort name|size|mtime|type [asc|desc]"));
      return false;
    }
  }
  if (!touched)
    order = order == QLatin1String("desc") ? QStringLiteral("asc")
                                           : QStringLiteral("desc");
  m_proxy->setSortRoleName(role);
  m_proxy->setSortOrder(order);
  if (info)
    *info = QStringLiteral("sort %1 %2")
                .arg(m_proxy->sortRoleName(), m_proxy->sortOrder());
  return true;
}

bool KeyMachine::runRecent(QString *info) {
  Q_UNUSED(info);
  if (m_nav)
    m_nav->navigate(QStringLiteral("recent://"));
  else if (m_model)
    m_model->setPath(QStringLiteral("recent://"));
  return true;
}

QString KeyMachine::pinTarget() const {
  if (m_selection) {
    const QString cursor = m_selection->cursorPath();
    if (!cursor.isEmpty() && !DirectoryModel::isVirtualPath(cursor)) {
      const QFileInfo fi(cursor);
      if (fi.isDir()) {
        const QString canon = fi.canonicalFilePath();
        return canon.isEmpty() ? fi.absoluteFilePath() : canon;
      }
    }
  }
  if (!m_model)
    return {};
  const QString cwd = m_model->path();
  if (cwd.isEmpty() || DirectoryModel::isVirtualPath(cwd))
    return {};
  const QFileInfo fi(cwd);
  if (!fi.isDir())
    return {};
  const QString canon = fi.canonicalFilePath();
  return canon.isEmpty() ? fi.absoluteFilePath() : canon;
}

bool KeyMachine::runPinCommand(const QString &id, QString *info) {
  if (!m_chips) {
    setStatusMessage(QStringLiteral("pins are not available"));
    return false;
  }
  const QString path = pinTarget();
  if (path.isEmpty()) {
    setStatusMessage(QStringLiteral("cannot pin this location"));
    return false;
  }
  const bool pinned = m_chips->isPinned(path);
  const bool wantPin = id != QLatin1String("unpin");
  if (wantPin && pinned) {
    if (!m_chips->unpin(path)) {
      setStatusMessage(QStringLiteral("cannot unpin"));
      return false;
    }
    if (info)
      *info = QStringLiteral("unpinned %1").arg(QFileInfo(path).fileName());
    return true;
  }
  if (!wantPin) {
    if (!pinned) {
      if (info)
        *info = QStringLiteral("not pinned");
      return true;
    }
    if (!m_chips->unpin(path)) {
      setStatusMessage(QStringLiteral("cannot unpin"));
      return false;
    }
    if (info)
      *info = QStringLiteral("unpinned %1").arg(QFileInfo(path).fileName());
    return true;
  }
  if (!m_chips->pin(path)) {
    setStatusMessage(QStringLiteral("cannot pin"));
    return false;
  }
  if (info)
    *info = QStringLiteral("pinned %1").arg(QFileInfo(path).fileName());
  return true;
}

bool KeyMachine::isJumpText(const QString &text, const QString &cwd) {
  const QString t = text.trimmed();
  if (t.isEmpty())
    return false;
  if (t.startsWith(QLatin1Char('/')))
    return true;
  if (t.startsWith(QLatin1String("~/")))
    return true;
  const int slash = t.indexOf(QLatin1Char('/'));
  if (slash < 0)
    return false;
  const QString prefix = t.left(slash);
  if (prefix.isEmpty())
    return false;
  QString head = expandTilde(prefix);
  if (!QFileInfo(head).isAbsolute()) {
    if (cwd.isEmpty())
      return false;
    head = QDir(cwd).filePath(prefix);
  }
  return QFileInfo(head).exists();
}

QString KeyMachine::resolveJump(const QString &text, const QString &cwd) {
  QString t = text.trimmed();
  if (t.isEmpty())
    return {};
  t = expandTilde(t);
  if (!QFileInfo(t).isAbsolute()) {
    if (cwd.isEmpty())
      return {};
    t = QDir(cwd).filePath(t);
  }
  t = QDir::cleanPath(t);
  const QFileInfo info(t);
  if (info.isDir()) {
    const QString canon = info.canonicalFilePath();
    return canon.isEmpty() ? info.absoluteFilePath() : canon;
  }
  if (info.exists()) {
    const QFileInfo parent(info.absolutePath());
    const QString canon = parent.canonicalFilePath();
    return canon.isEmpty() ? parent.absoluteFilePath() : canon;
  }
  return {};
}

bool KeyMachine::handleListVerbs(int key, int modifiers) {
  const bool alt = hasAlt(modifiers);
  const bool chord = hasChord(modifiers);
  const bool shift = hasShift(modifiers);
  const bool ctrl = hasCtrl(modifiers);
  const bool meta = hasMeta(modifiers);

  if (key == Qt::Key_Space && ctrl && !alt && !meta) {
    if (m_selection && (!m_chooserMode || m_chooserMultiple))
      m_selection->toggleCursor();
    return true;
  }
  if (key == Qt::Key_A && ctrl && !alt && !meta) {
    if (m_selection && (!m_chooserMode || m_chooserMultiple))
      m_selection->selectAll();
    return true;
  }
  if (key == Qt::Key_C && ctrl && !alt && !meta) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->copySelection();
    return true;
  }
  if (key == Qt::Key_X && ctrl && !alt && !meta) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->cutSelection();
    return true;
  }
  if (key == Qt::Key_V && ctrl && !alt && !meta) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->paste();
    return true;
  }
  if (key == Qt::Key_N && ctrl && shift && !alt && !meta) {
    if (!m_chooserMode)
      startMkdir();
    return true;
  }
  if (key == Qt::Key_F2 && !alt && !chord) {
    if (!m_chooserMode)
      startRename();
    return true;
  }
  if (key == Qt::Key_Z && ctrl && !alt && !meta) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->undo();
    return true;
  }
  if (key == Qt::Key_Delete && !alt && !chord) {
    if (m_chooserMode)
      return true;
    if (shift || (m_model && m_model->isTrash()))
      startUnlinkConfirm();
    else if (m_fileOps)
      m_fileOps->trashSelection();
    return true;
  }

  if (key == Qt::Key_Down) {
    if (alt || chord)
      return false;
    if (m_gridMode) {
      nudgeCursor(0, 1, false);
      return true;
    }
    if (m_selection)
      m_selection->moveCursor(1);
    else if (m_proxy)
      m_proxy->moveCursor(1);
    else if (m_model)
      m_model->moveCursor(1);
    return true;
  }
  if (key == Qt::Key_Up) {
    if (alt || chord)
      return false;
    if (m_gridMode) {
      nudgeCursor(0, -1, false);
      return true;
    }
    if (m_selection)
      m_selection->moveCursor(-1);
    else if (m_proxy)
      m_proxy->moveCursor(-1);
    else if (m_model)
      m_model->moveCursor(-1);
    return true;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (alt || chord)
      return false;
    activateCurrent();
    return true;
  }
  if (key == Qt::Key_Space && !alt && !chord) {
    if (shift || !m_lookKeyMode) {
      if (m_host)
        m_host->toggle();
    } else {
      emit lookToggleRequested();
    }
    return true;
  }
  if (key == Qt::Key_Backspace && !alt && !chord && !shift) {
    if (m_nav)
      m_nav->goUp();
    return true;
  }
  if (key == Qt::Key_Left && alt && !chord) {
    if (m_nav)
      m_nav->goBack();
    return true;
  }
  if (key == Qt::Key_Right && alt && !chord) {
    if (m_nav)
      m_nav->goForward();
    return true;
  }
  if (key == Qt::Key_G && ctrl && !alt && !meta) {
    revealCurrent();
    return true;
  }
  if (key == Qt::Key_D && ctrl && !alt && !meta) {
    QString info;
    runPinCommand(QStringLiteral("pin"), &info);
    if (!info.isEmpty())
      setStatusMessage(info);
    return true;
  }
  if (m_chooserMode &&
      (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) && ctrl &&
      !alt && !meta) {
    emit filterCycleRequested(key == Qt::Key_BracketRight ? 1 : -1);
    return true;
  }
  if (m_chooserMode && m_chooserSave &&
      (key == Qt::Key_Tab || key == Qt::Key_Backtab) && !alt && !chord) {
    emit saveNameFocusRequested();
    return true;
  }
  if (key == Qt::Key_H && ctrl && !alt && !meta) {
    if (m_selection)
      m_selection->exitVisual();
    if (m_mode == Mode::VisualSelect)
      setMode(Mode::ListFocused);
    if (m_model) {
      m_model->setShowHidden(!m_model->showHidden());
      setStatusMessage(m_model->showHidden() ? QStringLiteral("hidden on")
                                             : QStringLiteral("hidden off"));
    }
    return true;
  }
  if (key == Qt::Key_Slash && !alt && !chord) {
    focusFilter();
    return true;
  }
  if (key == Qt::Key_F1 && !alt && !chord) {
    setHelpOpen(!m_helpOpen);
    return true;
  }
  return false;
}

bool KeyMachine::handleDoKey(int key, int modifiers) {
  if (!m_host || !m_host->actionOpen())
    return false;
  const bool params = m_host->doParamsFocused();
  if (key == Qt::Key_Escape ||
      (key == Qt::Key_Q && !hasChord(modifiers) && !hasAlt(modifiers) &&
       !hasShift(modifiers))) {
    closeAction();
    return true;
  }
  if ((key == Qt::Key_Left || key == Qt::Key_Backtab) && params &&
      !hasChord(modifiers) && !hasAlt(modifiers)) {
    m_host->setDoParamsFocused(false);
    return true;
  }
  if ((key == Qt::Key_Right || key == Qt::Key_Tab) &&
      !hasChord(modifiers) && !hasAlt(modifiers)) {
    m_host->setDoParamsFocused(true);
    return true;
  }
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    if (hasCtrl(modifiers))
      return true;
    if (params && m_host->deliverDoKey(key, modifiers))
      return true;
    m_host->runDoVerb();
    return true;
  }
  if (params && m_host->deliverDoKey(key, modifiers))
    return true;
  if (key == Qt::Key_J || key == Qt::Key_Down || key == Qt::Key_S)
    m_host->doMove(1);
  else if (key == Qt::Key_K || key == Qt::Key_Up || key == Qt::Key_W)
    m_host->doMove(-1);
  return true;
}

bool KeyMachine::handlePeekKey(int key, int modifiers) {
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && hasCtrl(modifiers) &&
      !hasAlt(modifiers) && !hasMeta(modifiers)) {
    emit openWithRequested();
    return true;
  }
  if (hasChord(modifiers) || hasAlt(modifiers))
    return true;
  if (key == Qt::Key_T && !hasShift(modifiers)) {
    closeOverlays();
    emit terminalRequested();
    return true;
  }
  if (key == Qt::Key_Escape) {
    const bool previewOn =
        m_host && m_host->isOpen() && !m_host->folderListing() &&
        m_host->peekPreviewFocused();
    if (previewOn && m_host->deliverPeekKey(key, modifiers))
      return true;
    if (m_host)
      m_host->close();
    else
      setMode(Mode::ListFocused);
    return true;
  }
  if (m_chooserMode &&
      (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) &&
      !hasShift(modifiers)) {
    emit filterCycleRequested(key == Qt::Key_BracketRight ? 1 : -1);
    return true;
  }
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
      !hasShift(modifiers) && !hasChord(modifiers) && !hasAlt(modifiers)) {
    if (m_chooserMode) {
      emit chooserAcceptRequested();
      return true;
    }
    if (m_host)
      m_host->commitPeek();
    return true;
  }
  if (key == Qt::Key_Space && !hasShift(modifiers)) {
    if (m_host && m_host->folderListing()) {
      m_host->peekActivate();
      return true;
    }
    if (m_host && m_host->inFolderPeek()) {
      m_host->peekBack();
      return true;
    }
    if (m_host)
      m_host->close();
    else
      setMode(Mode::ListFocused);
    return true;
  }
  const bool listing = m_host && m_host->folderListing();
  const bool filePeek = m_host && m_host->isOpen() && !listing;
  const bool previewOn = filePeek && m_host->peekPreviewFocused();

  if (filePeek && !m_host->inFolderPeek() && !hasShift(modifiers) &&
      (key == Qt::Key_Q || key == Qt::Key_H || key == Qt::Key_Left ||
       key == Qt::Key_Backspace)) {
    m_host->close();
    return true;
  }

  if (m_host && m_host->inFolderPeek()) {
    if ((key == Qt::Key_H || key == Qt::Key_Left || key == Qt::Key_Backspace ||
         key == Qt::Key_Q) &&
        !hasShift(modifiers)) {
      m_host->peekBack();
      return true;
    }
    if ((key == Qt::Key_L || key == Qt::Key_Right || key == Qt::Key_E) &&
        !hasShift(modifiers) && !previewOn) {
      m_host->peekActivate();
      return true;
    }
    if (listing && (key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S ||
                    key == Qt::Key_D)) {
      int dx = 0;
      int dy = 0;
      if (key == Qt::Key_W)
        dy = -1;
      else if (key == Qt::Key_S)
        dy = 1;
      else if (key == Qt::Key_A)
        dx = -1;
      else
        dx = 1;
      const int step = hasShift(modifiers) ? 5 : 1;
      const int stride = m_host ? m_host->peekGridStride() : 1;
      const int delta = dy * step * stride + dx * step;
      if (delta != 0)
        m_host->peekMove(delta);
      return true;
    }
  }

  // A/D hop index ↔ file. Qt focus stays on the list. S is scroll, not hop.
  if (filePeek && !hasShift(modifiers)) {
    if (key == Qt::Key_A) {
      if (previewOn)
        m_host->setPeekPreviewFocused(false);
      return true;
    }
    if (key == Qt::Key_D && !previewOn) {
      m_host->setPeekPreviewFocused(true);
      return true;
    }
  }

  if (previewOn && !hasChord(modifiers) && !hasAlt(modifiers) &&
      key != Qt::Key_J && key != Qt::Key_K && key != Qt::Key_T &&
      key != Qt::Key_Escape && key != Qt::Key_Space && key != Qt::Key_Q) {
    if (m_host->deliverPeekKey(key, modifiers))
      return true;
    if (key == Qt::Key_W || key == Qt::Key_S || key == Qt::Key_Up ||
        key == Qt::Key_Down || key == Qt::Key_PageUp ||
        key == Qt::Key_PageDown || key == Qt::Key_Tab)
      return true;
  }

  if (key == Qt::Key_J || key == Qt::Key_Down || key == Qt::Key_S) {
    if (m_host)
      m_host->step(1);
    return true;
  }
  if (key == Qt::Key_K || key == Qt::Key_Up || key == Qt::Key_W) {
    if (m_host)
      m_host->step(-1);
    return true;
  }
  return true;
}

bool KeyMachine::handleListKey(int key, int modifiers, const QString &text) {
  if (handleChooserPromptKey(key, modifiers))
    return true;
  if (m_host && m_host->actionOpen())
    return handleDoKey(key, modifiers);
  if (m_mode == Mode::PeekOpen)
    return handlePeekKey(key, modifiers);
  if (m_mode == Mode::RenameInline || m_mode == Mode::ConfirmDialog ||
      !m_promptKind.isEmpty())
    return handleConfirmKey(key, modifiers, text);
  if (m_mode != Mode::ListFocused && m_mode != Mode::VisualSelect)
    return false;

  if (hasCtrl(modifiers) && !hasAlt(modifiers) && !hasMeta(modifiers) &&
      !hasShift(modifiers) &&
      (key == Qt::Key_1 || key == Qt::Key_2)) {
    setGridMode(key == Qt::Key_2);
    return true;
  }

  if (key == Qt::Key_K && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    focusFilter();
    return true;
  }
  if (key == Qt::Key_L && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    focusJump();
    return true;
  }
  if (key == Qt::Key_M && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    toggleFsnMode();
    return true;
  }
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) &&
      hasCtrl(modifiers) && !hasAlt(modifiers) && !hasMeta(modifiers)) {
    emit openWithRequested();
    return true;
  }
  if (key == Qt::Key_Escape) {
    escape();
    return true;
  }
  if (!hasChord(modifiers) && !hasAlt(modifiers) &&
      (key == Qt::Key_Colon || text == QLatin1String(":") ||
       (key == Qt::Key_Semicolon && hasShift(modifiers)))) {
    focusCommand();
    return true;
  }
  if (m_fsnMode && !hasChord(modifiers) && !hasAlt(modifiers) &&
      (key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S ||
       key == Qt::Key_D || key == Qt::Key_M))
    return false;
  if (!hasChord(modifiers) && !hasAlt(modifiers) &&
      (key == Qt::Key_Question || text == QLatin1String("?"))) {
    focusSearch();
    return true;
  }
  if (handleListVerbs(key, modifiers))
    return true;

  if (!hasChord(modifiers) && !hasAlt(modifiers) && !text.isEmpty()) {
    bool printable = true;
    for (const QChar c : text) {
      if (!c.isPrint()) {
        printable = false;
        break;
      }
    }
    if (printable) {
      const QString next = m_localFilterSession ? m_fieldText + text : text;
      focusFilter();
      setFieldText(next);
      return true;
    }
  }
  return false;
}

bool KeyMachine::handleFieldKey(int key, int modifiers) {
  if (handleChooserPromptKey(key, modifiers))
    return true;
  if (m_mode == Mode::PeekOpen || (m_host && m_host->isOpen()))
    return handlePeekKey(key, modifiers);
  if (m_mode == Mode::ListFocused)
    return false;
  if (hasCtrl(modifiers) && !hasAlt(modifiers) && !hasMeta(modifiers) &&
      !hasShift(modifiers) &&
      (key == Qt::Key_1 || key == Qt::Key_2)) {
    setGridMode(key == Qt::Key_2);
    return true;
  }
  if (key == Qt::Key_Escape) {
    escape();
    return true;
  }
  if (m_mode == Mode::FieldFilter && !hasChord(modifiers) &&
      !hasAlt(modifiers) &&
      (key == Qt::Key_Up || key == Qt::Key_Down)) {
    const int dy = key == Qt::Key_Up ? -1 : 1;
    if (m_gridMode)
      emit gridMoveRequested(0, dy);
    else
      nudgeCursor(0, dy, false);
    return true;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (hasChord(modifiers) || hasAlt(modifiers))
      return false;
    acceptField();
    return true;
  }
  if (key == Qt::Key_K && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    focusFilter();
    return true;
  }
  if (key == Qt::Key_L && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    focusJump();
    return true;
  }
  if (key == Qt::Key_M && hasCtrl(modifiers) && !hasAlt(modifiers) &&
      !hasMeta(modifiers)) {
    toggleFsnMode();
    return true;
  }
  return false;
}
