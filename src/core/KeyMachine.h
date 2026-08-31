#pragma once

#include "CommandPalette.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>

class DirectoryModel;
class FileOpEngine;
class FilterProxy;
class NavStack;
class LocationChips;
class PeekHost;
class RecentStore;
class SearchModel;
class SelectionModel;

// Conventional browser keyboard states. The listing owns focus on launch;
// printable input promotes the visible field into a local folder filter.
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
  Q_PROPERTY(bool fsnMode READ fsnMode WRITE setFsnMode NOTIFY fsnModeChanged)
  Q_PROPERTY(bool fsnTreeView READ fsnTreeView WRITE setFsnTreeView NOTIFY
                 fsnTreeViewChanged)
  Q_PROPERTY(QString panelId READ panelId WRITE setPanelId NOTIFY panelChanged)
  Q_PROPERTY(QString panelSide READ panelSide WRITE setPanelSide NOTIFY
                 panelChanged)
  Q_PROPERTY(bool panelFocused READ panelFocused WRITE setPanelFocused NOTIFY
                 panelFocusedChanged)
  Q_PROPERTY(bool lookKeyMode READ lookKeyMode WRITE setLookKeyMode NOTIFY
                 lookKeyModeChanged)
  Q_PROPERTY(int gridStride READ gridStride WRITE setGridStride NOTIFY
                 gridStrideChanged)
  Q_PROPERTY(bool helpOpen READ helpOpen NOTIFY helpOpenChanged)
  Q_PROPERTY(QString helpText READ helpText CONSTANT)
  Q_PROPERTY(QVariantList helpModel READ helpModel CONSTANT)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
  Q_PROPERTY(QString promptText READ promptText WRITE setPromptText NOTIFY
                 promptChanged)
  Q_PROPERTY(QString promptKind READ promptKind NOTIFY promptChanged)
  Q_PROPERTY(bool ynPrompt READ ynPrompt NOTIFY promptChanged)
  Q_PROPERTY(bool chooserMode READ chooserMode NOTIFY chooserModeChanged)

public:
  enum class Mode {
    ListFocused,
    FieldFilter,
    FieldJump,
    FieldCommand,
    FieldSearch,
    PeekOpen,
    VisualSelect,
    RenameInline,
    ConfirmDialog
  };
  Q_ENUM(Mode)

  using ActionRunner =
      std::function<bool(const QString &id, QString *error)>;

  explicit KeyMachine(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
                      QObject *parent = nullptr);

  QString mode() const;
  bool listFocused() const {
    return m_mode == Mode::ListFocused || m_mode == Mode::PeekOpen ||
           m_mode == Mode::VisualSelect;
  }
  bool fieldFocused() const {
    return m_mode == Mode::FieldFilter || m_mode == Mode::FieldJump ||
           m_mode == Mode::FieldCommand || m_mode == Mode::FieldSearch;
  }
  bool peekOpen() const { return m_mode == Mode::PeekOpen; }
  bool actionOpen() const;
  void setPeekHost(PeekHost *host);
  void setSelection(SelectionModel *sel) { m_selection = sel; }
  void setFileOps(FileOpEngine *ops) { m_fileOps = ops; }
  void setChooserMode(bool on, bool multiple = false, bool save = false);
  void setChooserPromptOpen(bool on);
  bool chooserMode() const { return m_chooserMode; }
  bool chooserSave() const { return m_chooserSave; }
  void setRecentStore(RecentStore *store) { m_recents = store; }
  void setLocationChips(LocationChips *chips) { m_chips = chips; }
  void setSearchModel(SearchModel *search);
  void setTrashAvailable(bool on) { m_trashAvailable = on; }
  void setActionRunner(ActionRunner runner) { m_actionRunner = std::move(runner); }
  void registerAction(const QString &id, const QString &title);
  QString fieldText() const { return m_fieldText; }
  int jumpEpoch() const { return m_jumpEpoch; }
  Mode modeEnum() const { return m_mode; }
  bool gridMode() const { return m_gridMode; }
  bool fsnMode() const { return m_fsnMode; }
  bool fsnTreeView() const { return m_fsnTreeView; }
  QString panelId() const { return m_panelId; }
  QString panelSide() const { return m_panelSide; }
  bool panelFocused() const { return m_panelFocused; }
  bool lookKeyMode() const { return m_lookKeyMode; }
  int gridStride() const { return m_gridStride; }
  bool helpOpen() const { return m_helpOpen; }
  QString helpText() const { return CommandPalette::helpText(); }
  QVariantList helpModel() const { return CommandPalette::helpModel(); }
  QString statusMessage() const { return m_status; }
  QString promptText() const { return m_promptText; }
  QString promptKind() const { return m_promptKind; }
  bool ynPrompt() const;

  Q_INVOKABLE void setFieldText(const QString &text);
  Q_INVOKABLE void setGridMode(bool on);
  Q_INVOKABLE void setFsnMode(bool on);
  Q_INVOKABLE void toggleFsnMode();
  Q_INVOKABLE void setFsnTreeView(bool tree);
  Q_INVOKABLE void setPanelId(const QString &id);
  Q_INVOKABLE void setPanelSide(const QString &side);
  Q_INVOKABLE void togglePanel(const QString &id);
  Q_INVOKABLE void setPanelFocused(bool on);
  Q_INVOKABLE void setLookKeyMode(bool on);
  Q_INVOKABLE void setGridStride(int columns);
  // Browser grids resolve adjacency from their rendered delegates, then hand
  // the chosen model row back here so visual selection semantics stay central.
  Q_INVOKABLE void moveGridCursorTo(int index);
  Q_INVOKABLE void setStatusMessage(const QString &text);
  Q_INVOKABLE void focusFilter();
  Q_INVOKABLE void focusJump();
  Q_INVOKABLE void focusCommand();
  Q_INVOKABLE void focusSearch();
  Q_INVOKABLE void toggleSearchField();
  Q_INVOKABLE void focusList();
  Q_INVOKABLE void escape();
  Q_INVOKABLE void acceptField();
  void restoreField(const QString &text);
  Q_INVOKABLE bool handleListKey(int key, int modifiers, const QString &text);
  Q_INVOKABLE bool handleFieldKey(int key, int modifiers);
  Q_INVOKABLE void requestEmptyTrash();
  Q_INVOKABLE void setPromptText(const QString &text);
  Q_INVOKABLE void acceptPrompt();
  bool confirmOpen() const {
    return m_mode == Mode::RenameInline || m_mode == Mode::ConfirmDialog ||
           !m_promptKind.isEmpty();
  }

  // Path-sigil jump only. Bare names (even if they exist as dirs) never jump.
  static bool isJumpText(const QString &text, const QString &cwd);
  static QString resolveJump(const QString &text, const QString &cwd);
  static bool isCommandText(const QString &text);
  // Leading `?` (name) or `??` (content).
  static bool isSearchText(const QString &text);
  static bool isContentSearchText(const QString &text);
  static QString searchQuery(const QString &text);
  static constexpr int kMinContentQueryChars = 3;
  static constexpr int kMinNameQueryChars = 3;

signals:
  void modeChanged();
  void fieldTextChanged();
  void jumpEpochChanged();
  void terminalRequested();
  void openWithRequested();
  void actionOpenChanged();
  void gridModeChanged();
  void fsnModeChanged();
  void fsnTreeViewChanged();
  void panelChanged();
  void panelFocusRequested();
  void sqlScanRequested();
  void agentSearchRequested();
  void semanticSearchRequested(const QString &query);
  void settingsRequested();
  void panelFocusedChanged();
  void lookKeyModeChanged();
  void lookToggleRequested();
  void gridStrideChanged();
  void helpOpenChanged();
  void statusMessageChanged();
  void promptChanged();
  void chooserModeChanged();
  void chooserAcceptRequested();
  void dismissRequested();
  void chooserPromptDismissRequested();
  void filterCycleRequested(int delta);
  void saveNameFocusRequested();
  void localFilterStarted();
  void localFilterCanceled();
  void gridMoveRequested(int dx, int dy);

private:
  void setMode(Mode mode);
  void setSearchStatusMessage(const QString &text);
  void setHelpOpen(bool on);
  void closePeek();
  void closeAction();
  void closeOverlays();
  void applyFieldText();
  void clearFieldAndFilter();
  void beginLocalFilter();
  void cancelLocalFilter();
  void activateCurrent();
  void onPathChanged();
  bool handleListVerbs(int key, int modifiers);
  bool handlePeekKey(int key, int modifiers);
  bool handleDoKey(int key, int modifiers);
  void runCommand(const QString &text);
  bool runBuiltin(const QString &id, QString *info);
  bool runRecent(QString *info);
  bool applySortCommand(const QString &text, QString *info);
  bool runPinCommand(const QString &id, QString *info);
  QString pinTarget() const;
  void finishCommand();
  bool fieldQueryEmpty() const;
  void scheduleSearch();
  void runSearch();
  void cancelSearch();
  void leaveSearchListing();
  void revealCurrent();
  QString searchRoot() const;
  bool handleConfirmKey(int key, int modifiers, const QString &text);
  bool handleChooserPromptKey(int key, int modifiers);
  void clearConfirm();
  void acceptEmptyTrash();
  void startRename();
  void startMkdir();
  void startUnlinkConfirm();
  void startEmptyConfirm();
  void nudgeCursor(int dx, int dy, bool leap);
  void setCursorIndex(int index);
  int cursorIndex() const;

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  NavStack *m_nav = nullptr;
  PeekHost *m_host = nullptr;
  SelectionModel *m_selection = nullptr;
  FileOpEngine *m_fileOps = nullptr;
  RecentStore *m_recents = nullptr;
  LocationChips *m_chips = nullptr;
  SearchModel *m_search = nullptr;
  CommandPalette m_palette;
  QTimer m_searchDebounce;
  QString m_searchAnchor;
  bool m_holdSearchField = false;
  ActionRunner m_actionRunner;
  Mode m_mode = Mode::ListFocused;
  QString m_fieldText;
  QString m_status;
  bool m_searchStatus = false;
  bool m_localFilterSession = false;
  int m_filterRestoreSourceRow = -1;
  QString m_filterRestorePath;
  QString m_filterRestoreItemPath;
  int m_jumpEpoch = 0;
  bool m_gridMode = false;
  bool m_fsnMode = false;
  bool m_fsnTreeView = true;
  QString m_panelId;
  QString m_panelSide = QStringLiteral("bottom");
  bool m_panelFocused = false;
  bool m_lookKeyMode = false;
  int m_gridStride = 1;
  bool m_helpOpen = false;
  bool m_trashAvailable = true;
  QString m_promptKind;
  QString m_promptText;
  bool m_chooserMode = false;
  bool m_chooserMultiple = false;
  bool m_chooserSave = false;
  bool m_chooserPromptOpen = false;
};
