#include "KeyMachine.h"

#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "NavStack.h"
#include "PeekHost.h"

#include <QDir>
#include <QFileInfo>

namespace {

constexpr int kSeekTimeoutMs = 800;

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
  case Mode::PeekOpen:
    return QStringLiteral("peek-open");
  }
  return QStringLiteral("list-focused");
}

bool KeyMachine::actionOpen() const {
  return m_host && m_host->actionOpen();
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
}

void KeyMachine::applyFieldText() {
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
  m_fieldText.clear();
  if (m_proxy)
    m_proxy->setFilter(QString());
  if (m_nav)
    m_nav->setLiveFilter(QString());
  emit fieldTextChanged();
}

void KeyMachine::onPathChanged() {
  m_seek.clear();
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
  setMode(Mode::FieldFilter);
  applyFieldText();
}

void KeyMachine::focusJump() {
  closeOverlays();
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

void KeyMachine::focusList() {
  closeOverlays();
  setMode(Mode::ListFocused);
}

void KeyMachine::escape() {
  if (m_host && m_host->actionOpen()) {
    closeAction();
    return;
  }
  if (m_mode == Mode::PeekOpen || (m_host && m_host->isOpen())) {
    closePeek();
    return;
  }
  if (m_mode != Mode::ListFocused) {
    if (!m_fieldText.isEmpty()) {
      clearFieldAndFilter();
      return;
    }
    setMode(Mode::ListFocused);
    return;
  }
  if (!m_fieldText.isEmpty() || (m_proxy && !m_proxy->filter().isEmpty()))
    clearFieldAndFilter();
}

void KeyMachine::acceptField() {
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
  case Qt::Key_D:
  case Qt::Key_P:
  case Qt::Key_U:
  case Qt::Key_T:
  case Qt::Key_G:
    return !shift;
  case Qt::Key_V:
  case Qt::Key_Period:
  case Qt::Key_Slash:
  case Qt::Key_Colon:
  case Qt::Key_Question:
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

  if (key == Qt::Key_J || key == Qt::Key_Down) {
    if (alt || chord)
      return false;
    if (m_proxy)
      m_proxy->moveCursor(1);
    else if (m_model)
      m_model->moveCursor(1);
    return true;
  }
  if (key == Qt::Key_K || key == Qt::Key_Up) {
    if (alt || chord)
      return false;
    if (m_proxy)
      m_proxy->moveCursor(-1);
    else if (m_model)
      m_model->moveCursor(-1);
    return true;
  }
  if (key == Qt::Key_Return || key == Qt::Key_Enter) {
    if (alt || chord)
      return false;
    if (m_proxy)
      m_proxy->activateCurrent();
    else if (m_model)
      m_model->activateCurrent();
    return true;
  }
  if (key == Qt::Key_L && !alt && !chord && !shift) {
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
  if ((key == Qt::Key_H || key == Qt::Key_Backspace) && !alt && !chord &&
      !shift) {
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
  if (key == Qt::Key_Period && !alt && !chord && !shift) {
    if (m_model)
      m_model->setShowHidden(!m_model->showHidden());
    return true;
  }
  if (key == Qt::Key_Slash && !alt && !chord) {
    focusFilter();
    return true;
  }
  if (key == Qt::Key_T && !alt && !chord && !shift) {
    closeOverlays();
    emit terminalRequested();
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

bool KeyMachine::handlePeekKey(int key, int modifiers) {
  if ((key == Qt::Key_Return || key == Qt::Key_Enter) && hasCtrl(modifiers) &&
      !hasAlt(modifiers) && !hasMeta(modifiers)) {
    closeOverlays();
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
  if (key == Qt::Key_Space || key == Qt::Key_Escape) {
    if (m_host)
      m_host->close();
    else
      setMode(Mode::ListFocused);
    return true;
  }
  if (key == Qt::Key_J || key == Qt::Key_Down) {
    if (m_host)
      m_host->step(1);
    return true;
  }
  if (key == Qt::Key_K || key == Qt::Key_Up) {
    if (m_host)
      m_host->step(-1);
    return true;
  }
  return true;
}

bool KeyMachine::handleListKey(int key, int modifiers, const QString &text) {
  if (m_mode == Mode::PeekOpen)
    return handlePeekKey(key, modifiers);
  if (m_mode != Mode::ListFocused)
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
    closeOverlays();
    emit openWithRequested();
    return true;
  }
  if (key == Qt::Key_Escape) {
    escape();
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
  if (m_mode == Mode::ListFocused)
    return false;
  if (key == Qt::Key_Escape) {
    escape();
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
