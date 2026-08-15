#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class DirectoryModel;
class FilterProxy;
class NavStack;
class PeekHost;

// Ranger-with-visible-field keyboard states (K7). The list owns keys on
// launch; the field is chrome, not an always-focused omnibar.
class KeyMachine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
  Q_PROPERTY(bool listFocused READ listFocused NOTIFY modeChanged)
  Q_PROPERTY(bool fieldFocused READ fieldFocused NOTIFY modeChanged)
  Q_PROPERTY(bool peekOpen READ peekOpen NOTIFY modeChanged)
  Q_PROPERTY(QString fieldText READ fieldText WRITE setFieldText NOTIFY
                 fieldTextChanged)
  Q_PROPERTY(int jumpEpoch READ jumpEpoch NOTIFY jumpEpochChanged)

public:
  enum class Mode { ListFocused, FieldFilter, FieldJump, PeekOpen };
  Q_ENUM(Mode)

  explicit KeyMachine(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
                      QObject *parent = nullptr);

  QString mode() const;
  bool listFocused() const {
    return m_mode == Mode::ListFocused || m_mode == Mode::PeekOpen;
  }
  bool fieldFocused() const {
    return m_mode == Mode::FieldFilter || m_mode == Mode::FieldJump;
  }
  bool peekOpen() const { return m_mode == Mode::PeekOpen; }
  void setPeekHost(PeekHost *host);
  QString fieldText() const { return m_fieldText; }
  int jumpEpoch() const { return m_jumpEpoch; }
  Mode modeEnum() const { return m_mode; }

  Q_INVOKABLE void setFieldText(const QString &text);
  Q_INVOKABLE void focusFilter();
  Q_INVOKABLE void focusJump();
  Q_INVOKABLE void focusList();
  Q_INVOKABLE void escape();
  Q_INVOKABLE void acceptField();
  void restoreField(const QString &text);
  Q_INVOKABLE bool handleListKey(int key, int modifiers, const QString &text);
  Q_INVOKABLE bool handleFieldKey(int key, int modifiers);

  // Path-sigil jump only. Bare names (even if they exist as dirs) never jump.
  static bool isJumpText(const QString &text, const QString &cwd);
  static QString resolveJump(const QString &text, const QString &cwd);

signals:
  void modeChanged();
  void fieldTextChanged();
  void jumpEpochChanged();
  void terminalRequested();
  void openWithRequested();

private:
  void setMode(Mode mode);
  void closePeek();
  void applyFieldText();
  void clearFieldAndFilter();
  void onPathChanged();
  bool handleListVerbs(int key, int modifiers);
  bool handlePeekKey(int key, int modifiers);
  void seek(const QString &chunk);

  static bool isReservedVerb(int key, int modifiers);

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  NavStack *m_nav = nullptr;
  PeekHost *m_host = nullptr;
  Mode m_mode = Mode::ListFocused;
  QString m_fieldText;
  QString m_seek;
  QElapsedTimer m_seekClock;
  int m_jumpEpoch = 0;
};
