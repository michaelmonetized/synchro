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

constexpr int kSeekTimeoutMs = 800;
constexpr int kSearchDebounceMs = 200;

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
    const QString err = m_search->errorString();
    if (!err.isEmpty())
      setStatusMessage(err);
  });
  connect(m_search, &SearchModel::listingChanged, this, [this] {
    if (!m_search || m_search->listing())
      return;
    if (!m_search->errorString().isEmpty())
      return;
    const int n = m_search->count();
    if (n <= 0 && !m_search->query().isEmpty())
      setStatusMessage(QStringLiteral("no matches"));
    else if (n > 0)
      setStatusMessage(QStringLiteral("%1 matches").arg(n));
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
  if (m_gridMode == on)
    return;
  m_gridMode = on;
  if (!on)
    setGridStride(1);
  emit gridModeChanged();
}

void KeyMachine::setGridStride(int columns) {
  const int next = qMax(1, columns);
  if (m_gridStride == next)
    return;
  m_gridStride = next;
  emit gridStrideChanged();
}

void KeyMachine::nudgeCursor(int dx, int dy, bool leap) {
  const int step = leap ? 5 : 1;
  const int stride = m_gridMode ? m_gridStride : 1;
  const int delta = dy * step * stride + dx * step;
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
  // `??` is content search (later). Do not treat it as `?` name search.
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

void KeyMachine::onPathChanged() {
  m_seek.clear();
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
    clearFieldAndFilter();
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
}

bool KeyMachine::isCommandText(const QString &text) {
  return text.trimmed().startsWith(QLatin1Char(':'));
}

bool KeyMachine::isSearchText(const QString &text) {
  return text.startsWith(QLatin1Char('?')) &&
         !text.startsWith(QLatin1String("??"));
}

QString KeyMachine::searchQuery(const QString &text) {
  if (!isSearchText(text))
    return {};
  return text.mid(1);
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
  if (!m_model)
    return QDir::homePath();
  const QString loc = m_model->path();
  if (DirectoryModel::isSearchPath(loc)) {
    if (m_search && !m_search->root().isEmpty())
      return m_search->root();
    if (!m_model->returnPath().isEmpty())
      return m_model->returnPath();
  }
  if (!DirectoryModel::isVirtualPath(loc) && !loc.isEmpty())
    return loc;
  return QDir::homePath();
}

void KeyMachine::cancelSearch() {
  m_searchDebounce.stop();
  if (m_search)
    m_search->cancel();
}

void KeyMachine::runSearch() {
  const QString query = searchQuery(m_fieldText);
  if (query.isEmpty()) {
    cancelSearch();
    return;
  }
  const QString loc = m_model ? m_model->path() : QString();
  if (DirectoryModel::isVirtualPath(loc) && !DirectoryModel::isSearchPath(loc)) {
    setStatusMessage(QStringLiteral("search is not available"));
    return;
  }
  if (!m_search) {
    setStatusMessage(QStringLiteral("search is not available"));
    return;
  }
  const QString root = searchRoot();
  if (m_model && !DirectoryModel::isSearchPath(m_model->path())) {
    if (m_nav)
      m_nav->navigate(QStringLiteral("search://"));
    else
      m_model->setPath(QStringLiteral("search://"));
  } else if (m_search->query() == query && m_search->root() == root)
    return;
  m_search->start(query, root, m_model && m_model->showHidden());
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
  if (id == QLatin1String("grid")) {
    setGridMode(true);
    return true;
  }
  if (id == QLatin1String("list")) {
    setGridMode(false);
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

bool KeyMachine::isReservedVerb(int key, int modifiers) {
  if (hasChord(modifiers) || hasAlt(modifiers))
    return false;
  const bool shift = hasShift(modifiers);
  switch (key) {
  case Qt::Key_J:
  case Qt::Key_K:
  case Qt::Key_H:
  case Qt::Key_L:
  case Qt::Key_N:
  case Qt::Key_R:
  case Qt::Key_Y:
  case Qt::Key_P:
  case Qt::Key_U:
  case Qt::Key_T:
  case Qt::Key_G:
  case Qt::Key_Delete:
    return !shift;
  case Qt::Key_W:
  case Qt::Key_A:
  case Qt::Key_S:
  case Qt::Key_D:
  case Qt::Key_Q:
  case Qt::Key_E:
  case Qt::Key_V:
  case Qt::Key_Period:
  case Qt::Key_Slash:
  case Qt::Key_Colon:
  case Qt::Key_Question:
  case Qt::Key_F1:
  case Qt::Key_X:
  case Qt::Key_Space:
  case Qt::Key_Return:
  case Qt::Key_Enter:
  case Qt::Key_Backspace:
  case Qt::Key_Up:
  case Qt::Key_Down:
  case Qt::Key_Left:
  case Qt::Key_Right:
    return true;
  default:
    return false;
  }
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
  if ((key == Qt::Key_Y && !alt && !chord && !shift) ||
      (key == Qt::Key_C && ctrl && !alt && !meta)) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->copySelection();
    return true;
  }
  if ((key == Qt::Key_X && !alt && !chord && !shift) ||
      (key == Qt::Key_X && ctrl && !alt && !meta)) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->cutSelection();
    return true;
  }
  if (!alt && !chord &&
      (key == Qt::Key_W || key == Qt::Key_A || key == Qt::Key_S ||
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
    nudgeCursor(dx, dy, shift);
    return true;
  }
  if ((key == Qt::Key_P && !alt && !chord && !shift) ||
      (key == Qt::Key_V && ctrl && !alt && !meta)) {
    if (!m_chooserMode && m_fileOps)
      m_fileOps->paste();
    return true;
  }
  if (key == Qt::Key_V && shift && !alt && !chord) {
    if (m_selection && (!m_chooserMode || m_chooserMultiple)) {
      if (m_mode == Mode::VisualSelect) {
        m_selection->exitVisual();
        setMode(Mode::ListFocused);
      } else {
        m_selection->enterVisual();
        setMode(Mode::VisualSelect);
      }
    }
    return true;
  }
  if (key == Qt::Key_N && !alt && !chord && !shift) {
    if (!m_chooserMode)
      startMkdir();
    return true;
  }
  if ((key == Qt::Key_R && !alt && !chord && !shift) ||
      (key == Qt::Key_F2 && !alt && !chord)) {
    if (!m_chooserMode)
      startRename();
    return true;
  }
  if ((key == Qt::Key_U && !alt && !chord && !shift) ||
      (key == Qt::Key_Z && ctrl && !alt && !meta)) {
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

  if (key == Qt::Key_J || key == Qt::Key_Down) {
    if (alt || chord)
      return false;
    if (m_selection)
      m_selection->moveCursor(1);
    else if (m_proxy)
      m_proxy->moveCursor(1);
    else if (m_model)
      m_model->moveCursor(1);
    return true;
  }
  if (key == Qt::Key_K || key == Qt::Key_Up) {
    if (alt || chord)
      return false;
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
    if (m_chooserMode) {
      emit chooserAcceptRequested();
      return true;
    }
    if (m_selection)
      m_selection->activate();
    else if (m_proxy)
      m_proxy->activateCurrent();
    else if (m_model)
      m_model->activateCurrent();
    return true;
  }
  if ((key == Qt::Key_L || key == Qt::Key_Right || key == Qt::Key_E) &&
      !alt && !chord && !shift) {
    const bool isDir = m_model && m_model->currentIsDir();
    if (!isDir && m_host) {
      m_host->openCurrent();
      return true;
    }
    if (m_proxy)
      m_proxy->activateCurrent();
    else if (m_model)
      m_model->activateCurrent();
    return true;
  }
  if (key == Qt::Key_Space && !alt && !chord && !shift) {
    if (m_host)
      m_host->toggle();
    return true;
  }
  if ((key == Qt::Key_H || key == Qt::Key_Backspace || key == Qt::Key_Left ||
       key == Qt::Key_Q) &&
      !alt && !chord && !shift) {
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
  if (m_chooserMode &&
      (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) && !alt &&
      !chord && !shift) {
    emit filterCycleRequested(key == Qt::Key_BracketRight ? 1 : -1);
    return true;
  }
  if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) && !alt && !chord) {
    toggleSearchField();
    return true;
  }
  if ((key == Qt::Key_P && shift && !alt && !chord)) {
    QString info;
    runPinCommand(QStringLiteral("pin"), &info);
    if (!info.isEmpty())
      setStatusMessage(info);
    return true;
  }
  if (key == Qt::Key_Period && !alt && !chord && !shift) {
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
  if (key == Qt::Key_V && !alt && !chord && !shift) {
    setGridMode(!m_gridMode);
    return true;
  }
  if ((key == Qt::Key_Question || key == Qt::Key_F1) && !alt && !chord) {
    setHelpOpen(!m_helpOpen);
    return true;
  }
  if (key == Qt::Key_T && !alt && !chord && !shift) {
    closeOverlays();
    emit terminalRequested();
    return true;
  }
  if (key == Qt::Key_G && !alt && !chord && !shift) {
    revealCurrent();
    return true;
  }
  return false;
}

void KeyMachine::seek(const QString &chunk) {
  if (!m_seekClock.isValid() || m_seekClock.elapsed() > kSeekTimeoutMs)
    m_seek.clear();
  m_seek += chunk;
  m_seekClock.restart();
  if (m_proxy)
    m_proxy->seekPrefix(m_seek);
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
  if (key == Qt::Key_A && !hasChord(modifiers) && !hasAlt(modifiers) &&
      !hasShift(modifiers)) {
    m_host->setDoParamsFocused(false);
    return true;
  }
  if (key == Qt::Key_D && !hasChord(modifiers) && !hasAlt(modifiers) &&
      !hasShift(modifiers)) {
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
  if (handleListVerbs(key, modifiers))
    return true;
  if (isReservedVerb(key, modifiers))
    return true;

  if (!hasChord(modifiers) && !hasAlt(modifiers) && !text.isEmpty()) {
    bool printable = true;
    for (const QChar c : text) {
      if (!c.isPrint() || c.isSpace()) {
        printable = false;
        break;
      }
    }
    if (printable) {
      seek(text);
      return true;
    }
  }
  return false;
}

bool KeyMachine::handleFieldKey(int key, int modifiers) {
  if (handleChooserPromptKey(key, modifiers))
    return true;
  if (m_mode == Mode::ListFocused)
    return false;
  if (key == Qt::Key_Escape) {
    escape();
    return true;
  }
  if ((key == Qt::Key_Tab || key == Qt::Key_Backtab) &&
      !hasAlt(modifiers) && !hasChord(modifiers)) {
    toggleSearchField();
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
  return false;
}
