#pragma once

#include "CommandPalette.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

class DirectoryModel;
class FilterProxy;
class NavStack;
class PeekHost;
class RecentStore;
class SearchModel;

// Ranger-with-visible-field keyboard states (K7). The list owns keys on
// launch; the field is chrome, not an always-focused omnibar.
class KeyMachine : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
  Q_PROPERTY(bool listFocused READ listFocused NOTIFY modeChanged)
  Q_PROPERTY(bool fieldFocused READ fieldFocused NOTIFY modeChanged)
  Q_PROPERTY(bool peekOpen READ peekOpen NOTIFY modeChanged)
  Q_PROPERTY(bool actionOpen READ actionOpen NOTIFY actionOpenChanged)
  Q_PROPERTY(QString fieldText READ fieldText WRITE setFieldText NOTIFY
                 fieldTextChanged)
  Q_PROPERTY(int jumpEpoch READ jumpEpoch NOTIFY jumpEpochChanged)
  Q_PROPERTY(bool gridMode READ gridMode WRITE setGridMode NOTIFY gridModeChanged)
  Q_PROPERTY(bool helpOpen READ helpOpen NOTIFY helpOpenChanged)
  Q_PROPERTY(QString helpText READ helpText CONSTANT)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
  enum class Mode {
    ListFocused,
    FieldFilter,
    FieldJump,
    FieldCommand,
    FieldSearch,
    PeekOpen
  };
  Q_ENUM(Mode)

  using ActionRunner =
      std::function<bool(const QString &id, QString *error)>;

  explicit KeyMachine(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
                      QObject *parent = nullptr);

  QString mode() const;
  bool listFocused() const {
    return m_mode == Mode::ListFocused || m_mode == Mode::PeekOpen;
  }
  bool fieldFocused() const {
    return m_mode == Mode::FieldFilter || m_mode == Mode::FieldJump ||
           m_mode == Mode::FieldCommand || m_mode == Mode::FieldSearch;
  }
  bool peekOpen() const { return m_mode == Mode::PeekOpen; }
  bool actionOpen() const;
  void setPeekHost(PeekHost *host);
  void setRecentStore(RecentStore *store) { m_recents = store; }
  void setSearchModel(SearchModel *search);
  void setTrashAvailable(bool on) { m_trashAvailable = on; }
  void setActionRunner(ActionRunner runner) { m_actionRunner = std::move(runner); }
  void registerAction(const QString &id, const QString &title);
  QString fieldText() const { return m_fieldText; }
  int jumpEpoch() const { return m_jumpEpoch; }
  Mode modeEnum() const { return m_mode; }
  bool gridMode() const { return m_gridMode; }
  bool helpOpen() const { return m_helpOpen; }
  QString helpText() const { return CommandPalette::helpText(); }
  QString statusMessage() const { return m_status; }

  Q_INVOKABLE void setFieldText(const QString &text);
  Q_INVOKABLE void setGridMode(bool on);
  Q_INVOKABLE void setStatusMessage(const QString &text);
  Q_INVOKABLE void focusFilter();
  Q_INVOKABLE void focusJump();
  Q_INVOKABLE void focusCommand();
  Q_INVOKABLE void focusList();
  Q_INVOKABLE void escape();
  Q_INVOKABLE void acceptField();
  void restoreField(const QString &text);
  Q_INVOKABLE bool handleListKey(int key, int modifiers, const QString &text);
  Q_INVOKABLE bool handleFieldKey(int key, int modifiers);
  Q_INVOKABLE void requestEmptyTrash();
  bool confirmOpen() const { return !m_promptKind.isEmpty(); }

  // Path-sigil jump only. Bare names (even if they exist as dirs) never jump.
  static bool isJumpText(const QString &text, const QString &cwd);
  static QString resolveJump(const QString &text, const QString &cwd);
  static bool isCommandText(const QString &text);
  // Leading `?` but not `??` (content search is a later PR).
  static bool isSearchText(const QString &text);
  static QString searchQuery(const QString &text);

signals:
  void modeChanged();
  void fieldTextChanged();
  void jumpEpochChanged();
  void terminalRequested();
  void openWithRequested();
  void actionOpenChanged();
  void gridModeChanged();
  void helpOpenChanged();
  void statusMessageChanged();

private:
  void setMode(Mode mode);
  void setHelpOpen(bool on);
  void closePeek();
  void closeAction();
  void closeOverlays();
  void applyFieldText();
  void clearFieldAndFilter();
  void onPathChanged();
  bool handleListVerbs(int key, int modifiers);
  bool handlePeekKey(int key, int modifiers);
  void seek(const QString &chunk);
  void runCommand(const QString &text);
  bool runBuiltin(const QString &id, QString *info);
  bool runRecent(QString *info);
  void finishCommand();
  bool fieldQueryEmpty() const;
  void scheduleSearch();
  void runSearch();
  void cancelSearch();
  void revealCurrent();
  QString searchRoot() const;
  bool handleConfirmKey(int key, int modifiers, const QString &text);
  void clearConfirm();
  void acceptEmptyTrash();

  static bool isReservedVerb(int key, int modifiers);

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  NavStack *m_nav = nullptr;
  PeekHost *m_host = nullptr;
  RecentStore *m_recents = nullptr;
  SearchModel *m_search = nullptr;
  CommandPalette m_palette;
  QTimer m_searchDebounce;
  ActionRunner m_actionRunner;
  Mode m_mode = Mode::ListFocused;
  QString m_fieldText;
  QString m_seek;
  QString m_status;
  QElapsedTimer m_seekClock;
  int m_jumpEpoch = 0;
  bool m_gridMode = false;
  bool m_helpOpen = false;
  bool m_trashAvailable = true;
  QString m_promptKind;
};
