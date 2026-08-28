#pragma once

#include "HandlerActions.h"
#include "HandlerExec.h"
#include "Manifest.h"
#include "PeekHost.h"

#include <QColor>
#include <QQuickItem>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DirectoryModel;
class FileOpEngine;
class FileCatalog;
class FilterProxy;
class FilesOnlyProxy;
class HandlerLoader;
class HandlerRegistry;
class MimeMap;
class NavStack;
class OmaflowBridge;
class QQmlEngine;
class SelectionModel;
class XdgOpen;

// Peek + location chrome. No setListing — handlers cannot replace the model.
class HostApi : public PeekHost {
  Q_OBJECT
  Q_PROPERTY(QUrl file READ file NOTIFY fileChanged)
  Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
  Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
  Q_PROPERTY(QObject *previewItem READ previewItem NOTIFY previewItemChanged)
  Q_PROPERTY(QObject *folderListingItem READ folderListingItem NOTIFY
                 folderListingItemChanged)
  Q_PROPERTY(int peekGridStride READ peekGridStride WRITE setPeekGridStride
                 NOTIFY peekGridStrideChanged)
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)
  Q_PROPERTY(bool actionOpen READ actionOpen NOTIFY actionOpenChanged)
  Q_PROPERTY(QQuickItem *actionItem READ actionItem NOTIFY actionItemChanged)
  Q_PROPERTY(QVariantList doVerbs READ doVerbs NOTIFY doVerbsChanged)
  Q_PROPERTY(int doIndex READ doIndex WRITE setDoIndex NOTIFY doIndexChanged)
  Q_PROPERTY(QString doCaption READ doCaption NOTIFY doVerbsChanged)
  Q_PROPERTY(QString doBriefTitle READ doBriefTitle NOTIFY doIndexChanged)
  Q_PROPERTY(QString doBriefBody READ doBriefBody NOTIFY doIndexChanged)
  Q_PROPERTY(bool doHasParams READ doHasParams NOTIFY doIndexChanged)
  Q_PROPERTY(bool doParamsFocused READ doParamsFocused WRITE setDoParamsFocused
                 NOTIFY doFocusChanged)
  Q_PROPERTY(QString doHint READ doHint NOTIFY doHintChanged)
  Q_PROPERTY(bool doContextual READ doContextual NOTIFY doPresentationChanged)
  Q_PROPERTY(qreal doAnchorX READ doAnchorX NOTIFY doPresentationChanged)
  Q_PROPERTY(qreal doAnchorY READ doAnchorY NOTIFY doPresentationChanged)
  Q_PROPERTY(QString doProvider READ doProvider NOTIFY doIndexChanged)
  Q_PROPERTY(QString doEffect READ doEffect NOTIFY doIndexChanged)
  Q_PROPERTY(bool doArmed READ doArmed NOTIFY doOperationChanged)
  Q_PROPERTY(
      QString doOperationState READ doOperationState NOTIFY doOperationChanged)
  Q_PROPERTY(
      QString doOperationTitle READ doOperationTitle NOTIFY doOperationChanged)
  Q_PROPERTY(QString doOperationSummary READ doOperationSummary NOTIFY
                 doOperationChanged)
  Q_PROPERTY(QVariantList doOperationArtifacts READ doOperationArtifacts NOTIFY
                 doOperationChanged)
  Q_PROPERTY(QString listHint READ listHint CONSTANT)
  Q_PROPERTY(
      QQuickItem *doPreviewItem READ doPreviewItem NOTIFY doPreviewChanged)
  Q_PROPERTY(QObject *doFolderModel READ doFolderModel NOTIFY doPreviewChanged)
  Q_PROPERTY(QObject *doFolderProxy READ doFolderProxy NOTIFY doPreviewChanged)
  Q_PROPERTY(bool doTargetIsDir READ doTargetIsDir NOTIFY doPreviewChanged)
  Q_PROPERTY(QString doTargetName READ doTargetName NOTIFY doPreviewChanged)
  Q_PROPERTY(QVariantMap doMetadata READ doMetadata NOTIFY doMetadataChanged)
  Q_PROPERTY(QVariantList openCandidates READ openCandidates NOTIFY
                 openCandidatesChanged)
  Q_PROPERTY(bool folderPeek READ folderPeek NOTIFY folderPeekChanged)
  Q_PROPERTY(bool folderListing READ folderListing NOTIFY folderPeekChanged)
  Q_PROPERTY(QString folderPath READ folderPath NOTIFY folderPeekChanged)
  Q_PROPERTY(QString peekFileName READ peekFileName NOTIFY fileChanged)
  Q_PROPERTY(QObject *peekModel READ peekModel NOTIFY folderPeekChanged)
  Q_PROPERTY(QObject *peekProxy READ peekProxy NOTIFY folderPeekChanged)
  Q_PROPERTY(QObject *peekFileProxy READ peekFileProxy NOTIFY folderPeekChanged)
  Q_PROPERTY(
      bool gridMode READ gridMode WRITE setGridMode NOTIFY gridModeChanged)
  Q_PROPERTY(bool peekPreviewFocused READ peekPreviewFocused WRITE
                 setPeekPreviewFocused NOTIFY peekFocusChanged)
  Q_PROPERTY(QUrl listingRowUrl READ listingRowUrl NOTIFY listingChromeChanged)
  Q_PROPERTY(
      QUrl listingThumbUrl READ listingThumbUrl NOTIFY listingChromeChanged)
  Q_PROPERTY(
      QString peekFindQuery READ peekFindQuery NOTIFY peekFindQueryChanged)
  Q_PROPERTY(QQuickItem *inlinePreviewItem READ inlinePreviewItem NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(QString inlinePreviewMode READ inlinePreviewMode NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(QString inlinePreviewPath READ inlinePreviewPath NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(QString inlinePreviewHandler READ inlinePreviewHandler NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(int inlinePreviewCount READ inlinePreviewCount NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(QVariantMap inlinePreviewStat READ inlinePreviewStat NOTIFY
                 inlinePreviewChanged)
  Q_PROPERTY(QObject *inlineFolderModel READ inlineFolderModel NOTIFY
                 inlineFolderChanged)
  Q_PROPERTY(QObject *inlineFolderProxy READ inlineFolderProxy NOTIFY
                 inlineFolderChanged)
  Q_PROPERTY(bool inlineFolderLoading READ inlineFolderLoading NOTIFY
                 inlineFolderChanged)
  Q_PROPERTY(QString inlineFolderError READ inlineFolderError NOTIFY
                 inlineFolderChanged)
  Q_PROPERTY(bool inlineFolderTruncated READ inlineFolderTruncated NOTIFY
                 inlineFolderChanged)

public:
  HostApi(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
          HandlerRegistry *registry, HandlerLoader *loader, XdgOpen *xdg,
          MimeMap *mime, QQmlEngine *engine, QObject *parent = nullptr);

  QUrl file() const { return m_file; }
  QVariantList selection() const { return m_selection; }
  bool isOpen() const override { return m_open; }
  QObject *previewItem() const { return m_previewItem; }
  QObject *folderListingItem() const { return m_folderPreviewItem; }
  QString title() const { return m_title; }
  bool actionOpen() const override { return m_actionOpen; }
  QQuickItem *actionItem() const { return m_actionItem; }
  QVariantList doVerbs() const;
  int doIndex() const { return m_doIndex; }
  QString doCaption() const;
  QString doBriefTitle() const;
  QString doBriefBody() const;
  bool doHasParams() const;
  bool doParamsFocused() const override { return m_doParamsFocused; }
  QString doHint() const;
  bool doContextual() const { return m_doContextual; }
  qreal doAnchorX() const { return m_doAnchorX; }
  qreal doAnchorY() const { return m_doAnchorY; }
  QString doProvider() const;
  QString doEffect() const;
  bool doArmed() const { return m_doArmed; }
  QString doOperationState() const { return m_doOperationState; }
  QString doOperationTitle() const { return m_doOperationTitle; }
  QString doOperationSummary() const { return m_doOperationSummary; }
  QVariantList doOperationArtifacts() const { return m_doOperationArtifacts; }
  QString listHint() const;
  QQuickItem *doPreviewItem() const { return m_doPreviewItem; }
  QObject *doFolderModel() const;
  QObject *doFolderProxy() const;
  bool doTargetIsDir() const { return m_doTargetIsDir; }
  QString doTargetName() const { return m_doTargetName; }
  QVariantMap doMetadata() const { return m_doMetadata; }
  QUrl listingRowUrl() const { return m_listingRowUrl; }
  QUrl listingThumbUrl() const { return m_listingThumbUrl; }
  QQuickItem *inlinePreviewItem() const { return m_inlinePreviewItem; }
  QString inlinePreviewMode() const { return m_inlinePreviewMode; }
  QString inlinePreviewPath() const { return m_inlinePreviewPath; }
  QString inlinePreviewHandler() const { return m_inlinePreviewHandler; }
  int inlinePreviewCount() const { return m_inlinePreviewCount; }
  QVariantMap inlinePreviewStat() const { return m_inlinePreviewStat; }
  QObject *inlineFolderModel() const;
  QObject *inlineFolderProxy() const;
  bool inlineFolderLoading() const { return m_inlineFolderLoading; }
  QString inlineFolderError() const { return m_inlineFolderError; }
  bool inlineFolderTruncated() const { return m_inlineFolderTruncated; }
  QVariantList openCandidates() const { return m_openCandidates; }
  QString lastError() const { return m_error; }
  HandlerExec &exec() { return m_exec; }
  void setSelection(SelectionModel *sel) { m_sel = sel; }
  void setFileOps(FileOpEngine *ops) { m_ops = ops; }
  void setFileCatalog(FileCatalog *catalog);
  void setOmaflowBridge(OmaflowBridge *bridge);

public slots:
  void close() override;

public:
  Q_INVOKABLE bool toggle() override;
  Q_INVOKABLE bool openCurrent() override;
  Q_INVOKABLE void step(int delta) override;
  Q_INVOKABLE void openExternal(const QUrl &url);
  Q_INVOKABLE void reveal(const QUrl &url);
  Q_INVOKABLE void navigate(const QUrl &url);
  Q_INVOKABLE void setTitle(const QString &title);
  Q_INVOKABLE QVariantMap stat(const QUrl &url) const;
  // Panel handler support (synchro.panel.*)
  Q_INVOKABLE QUrl panelSource(const QString &id) const;
  Q_INVOKABLE QString panelRelevance(const QString &id) const;
  Q_INVOKABLE QVariantMap panelInfo(const QString &id) const;
  Q_INVOKABLE QVariantList panelPeers(const QString &id) const;
  Q_INVOKABLE bool panelSupportsCompanion(const QString &id,
                                          const QString &companion) const;
  Q_INVOKABLE QVariantList relevantPanels() const;
  Q_INVOKABLE bool
  attachSqlHighlighter(QObject *quickDocument, const QColor &keyword,
                       const QColor &stringColor, const QColor &number,
                       const QColor &comment, const QColor &normal) const;
  Q_INVOKABLE QString processCwd(int pid) const;
  Q_INVOKABLE QString defaultShell() const;
  Q_INVOKABLE QString terminalColorScheme() const;
  Q_INVOKABLE QString terminalBackground() const;
  Q_INVOKABLE bool setTerminalBackgroundOpacity(QObject *terminal,
                                                double opacity) const;
  Q_INVOKABLE void registerSurface(QObject *surface);
  Q_INVOKABLE void setInlinePreviewActive(bool active);
  Q_INVOKABLE void refreshInlinePreview();
  Q_INVOKABLE void refreshThemedPreviews();
  Q_INVOKABLE void clearInlinePreview();
  Q_INVOKABLE bool promoteInlinePreview();
  Q_INVOKABLE bool promoteInlineFolderRow(int row);
  Q_INVOKABLE bool commitInlineFolderRow(int row);
  // Non-blocking initial text/markdown reads for the ambient Look surface.
  Q_INVOKABLE quint64 requestPreview(const QUrl &url, int maxBytes = 65536,
                                     qint64 startByte = 0,
                                     const QVariantMap &presentation = {});
  // Background data-preview reads for inline quick apps. Results are tagged
  // so a handler can discard an obsolete request after selection changes.
  Q_INVOKABLE quint64 requestParquet(const QUrl &url, int maxRows = 12);
  Q_INVOKABLE quint64 requestDatabase(const QUrl &url, const QString &engine,
                                      const QString &table = QString(),
                                      int offset = 0, int limit = 40);
  Q_INVOKABLE bool openFile(const QString &path, const QString &mime);
  Q_INVOKABLE bool runOpen(const QString &handlerId);
  Q_INVOKABLE bool runTerminal();
  Q_INVOKABLE bool runTrash();
  Q_INVOKABLE bool restoreTrash();
  Q_INVOKABLE bool emptyTrash();
  Q_INVOKABLE bool runAction(const QString &handlerId);
  Q_INVOKABLE QVariantList omaflowMatches() const;
  Q_INVOKABLE bool runOmaflow(const QString &ruleId, bool dryRun = true);
  Q_INVOKABLE bool openWithPalette();
  Q_INVOKABLE bool openDoLayer(const QString &focusId = QString());
  Q_INVOKABLE bool openDoContext(qreal sceneX, qreal sceneY);
  Q_INVOKABLE void closeAction() override;
  Q_INVOKABLE void setDoIndex(int index);
  Q_INVOKABLE void doMove(int delta) override;
  Q_INVOKABLE bool runDoVerb() override;
  Q_INVOKABLE bool inspectDoVerb();
  Q_INVOKABLE void cancelDoOperation();
  Q_INVOKABLE void clearDoOperation();
  Q_INVOKABLE void revealDoArtifact(int index);
  Q_INVOKABLE void setDoParamsFocused(bool on) override;
  Q_INVOKABLE bool copyText(const QString &text);
  Q_INVOKABLE void registerDoSurface(QObject *surface);
  Q_INVOKABLE void registerDoContentSurface(QObject *surface);
  // First N bytes as text for peek handlers. Never executes the file.
  // startByte seeks into the file (aligned back to a newline) so find
  // can show a hit past the default 64 KiB window.
  Q_INVOKABLE QVariantMap readPreview(const QUrl &url, int maxBytes = 65536,
                                      qint64 startByte = 0) const;
  // Literal find (smart-case: sensitive only if needle has an uppercase).
  // Whole file, capped. offset/length are UTF-8 bytes.
  Q_INVOKABLE QVariantList findInFile(const QUrl &url, const QString &needle,
                                      int maxHits = 200) const;
  // Overlay find backgrounds on syntax HTML (or wrap plain text).
  Q_INVOKABLE QString markFindHits(const QString &html, const QString &plain,
                                   const QString &needle, int currentLocal,
                                   const QColor &matchFill,
                                   const QColor &currentFill) const;
  QString peekFindQuery() const;
  // Parquet footer schema; sample rows if duckdb is on PATH.
  Q_INVOKABLE QVariantMap readParquet(const QUrl &url, int maxRows = 12) const;
  // Read-only table browser for sqlite / duckdb peek handlers.
  // engine is "sqlite" or "duckdb". table empty → first table.
  Q_INVOKABLE QVariantMap readDatabase(const QUrl &url, const QString &engine,
                                       const QString &table = QString(),
                                       int offset = 0, int limit = 40) const;
  // Zip / tar / gzip member list. Does not extract.
  Q_INVOKABLE QVariantMap readArchive(const QUrl &url,
                                      int maxEntries = 200) const;
  // Local file URL Qt Image can decode; WebP is rasterized to a cached PNG.
  Q_INVOKABLE QUrl rasterUrl(const QUrl &url) const;

  bool folderPeek() const { return !m_folderStack.isEmpty(); }
  bool inFolderPeek() const override { return folderPeek(); }
  bool folderListing() const override {
    return folderPeek() && !m_filePeekFromFolder;
  }
  QString folderPath() const;
  QObject *peekModel() const;
  QObject *peekProxy() const;
  QObject *peekFileProxy();
  Q_INVOKABLE void peekActivate() override;
  Q_INVOKABLE void commitPeek() override;
  Q_INVOKABLE void peekBack() override;
  Q_INVOKABLE void setChooserMode(bool on);
  void peekMove(int delta) override;
  int peekGridStride() const override;
  Q_INVOKABLE void setPeekGridStride(int columns) override;
  Q_INVOKABLE QString peekCursorPath() const;
  Q_INVOKABLE QString peekFileName() const;
  Q_INVOKABLE bool peekCursorIsDir() const;
  bool gridMode() const { return m_gridMode; }
  Q_INVOKABLE void setGridMode(bool on);
  bool peekPreviewFocused() const override { return m_peekPreviewFocused; }
  Q_INVOKABLE void setPeekPreviewFocused(bool on) override;
  bool deliverPeekKey(int key, int modifiers) override;
  bool deliverDoKey(int key, int modifiers) override;

signals:
  void fileChanged();
  void selectionChanged();
  void previewItemChanged();
  void titleChanged();
  void statReady(const QUrl &url, const QVariantMap &st);
  void actionItemChanged();
  void doVerbsChanged();
  void doIndexChanged();
  void doMetadataChanged();
  void doFocusChanged();
  void doHintChanged();
  void doPresentationChanged();
  void doOperationChanged();
  void doPreviewChanged();
  void openCandidatesChanged();
  void folderPeekChanged();
  void folderListingItemChanged();
  void peekGridStrideChanged();
  void gridModeChanged();
  void peekFocusChanged();
  void fileCommitted(const QString &path, const QString &mime);
  void peekCommitRequested();
  void listingChromeChanged();
  void peekFindQueryChanged();
  void inlinePreviewChanged();
  void inlineFolderChanged();
  void previewReady(quint64 requestId, const QUrl &file,
                    const QVariantMap &preview);
  void parquetReady(quint64 requestId, const QUrl &file,
                    const QVariantMap &preview);
  void databaseReady(quint64 requestId, const QUrl &file,
                     const QVariantMap &preview);

private:
  Manifest::Item currentItem() const;
  Manifest::Item itemAt(int proxyRow) const;
  QVector<Manifest::Item> currentItems() const;
  void setCurrentFromModel();
  void reloadPreview();
  void destroyPreview();
  void destroyInlinePreview();
  Manifest::Item inlineCurrentItem() const;
  void ensureInlineFolderListing();
  void destroyAction();
  void refreshDoMetadata();
  void mergeDoImageFacts(const QVariantMap &facts);
  void applyDoMetadataResult(quint64 request, const QVariantMap &result);
  void refreshOpenCandidates();
  void onStatsApplied(const QStringList &paths);
  void destroyActionItem();
  void rebuildDoVerbs();
  bool openDoLayerAt(const QString &focusId, bool contextual, qreal sceneX,
                     qreal sceneY);
  bool startCurrentFlow(bool dryRun);
  void finishDoOperation(bool ok);
  void mountDoParams();
  void attachDoParams();
  void loadDoContent();
  void destroyDoContent();
  void attachDoPreview();
  Manifest::Item doContentItem() const;
  void ensureDoFolderListing();
  void refreshListingChrome();
  QVector<Manifest::Item> snapshotDoItems() const;
  QVector<Manifest::Item> activeItems() const;
  bool commitDoItems();
  static QVariantMap itemToMap(const Manifest::Item &item);
  void ensurePeekListing();
  void startFolderPeek(const QString &path);
  void showFolderListing();
  void showPeekFile(const Manifest::Item &item);
  void destroyFilePreview();
  bool loadPreviewFor(const Manifest::Item &item, QObject **slot);
  void applyPeekSelection(const Manifest::Item &item);
  Manifest::Item peekItemAt(int proxyRow) const;
  Manifest::Item peekCurrentItem() const;
  void stepPeekFiles(int delta);
  void endFolderPeek();

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  SelectionModel *m_sel = nullptr;
  FileOpEngine *m_ops = nullptr;
  NavStack *m_nav = nullptr;
  HandlerRegistry *m_registry = nullptr;
  HandlerLoader *m_loader = nullptr;
  XdgOpen *m_xdg = nullptr;
  MimeMap *m_mime = nullptr;
  QQmlEngine *m_engine = nullptr;

  HandlerExec m_exec;
  HandlerActions m_actions;

  QUrl m_file;
  QVariantList m_selection;
  QVariantList m_openCandidates;
  QString m_title;
  QString m_error;
  QObject *m_previewItem = nullptr;
  QObject *m_folderPreviewItem = nullptr;
  QObject *m_filePreviewItem = nullptr;
  QQuickItem *m_inlinePreviewItem = nullptr;
  QString m_inlinePreviewMode;
  QString m_inlinePreviewPath;
  QString m_inlinePreviewHandler;
  int m_inlinePreviewCount = 0;
  QVariantMap m_inlinePreviewStat;
  QSet<QString> m_inlineThumbPending;
  bool m_inlinePreviewActive = false;
  quint64 m_nextPreviewRequest = 0;
  QSet<QString> m_inlinePreparing;
  DirectoryModel *m_inlineFolderModel = nullptr;
  FilterProxy *m_inlineFolderProxy = nullptr;
  FileCatalog *m_fileCatalog = nullptr;
  OmaflowBridge *m_omaflow = nullptr;
  quint64 m_inlineFolderRequest = 0;
  bool m_inlineFolderLoading = false;
  bool m_inlineFolderTruncated = false;
  QString m_inlineFolderError;
  QQuickItem *m_actionItem = nullptr;
  QQuickItem *m_doSurface = nullptr;
  QQuickItem *m_doContentSurface = nullptr;
  QQuickItem *m_doPreviewItem = nullptr;
  DirectoryModel *m_doFolderModel = nullptr;
  FilterProxy *m_doFolderProxy = nullptr;
  QString m_doTargetName;
  bool m_doTargetIsDir = false;
  QVariantMap m_doMetadata;
  QString m_doMetadataPath;
  quint64 m_doMetadataRequest = 0;
  struct DoVerb {
    QString id;
    QString name;
    QString description;
    QString runtime;
    QString group;
    QString provider;
    QString providerId;
    QString effect;
    bool hasParams = false;
  };
  QVector<DoVerb> m_doVerbs;
  QVector<Manifest::Item> m_doItems;
  QVariantList m_doSelection;
  int m_doIndex = -1;
  bool m_doParamsFocused = false;
  bool m_doExplicitSelection = false;
  bool m_doContextual = false;
  qreal m_doAnchorX = -1;
  qreal m_doAnchorY = -1;
  bool m_doArmed = false;
  QString m_doOperationState;
  QString m_doOperationTitle;
  QString m_doOperationRule;
  QStringList m_doOperationPaths;
  QString m_doOperationSummary;
  QVariantList m_doOperationArtifacts;
  QString m_loadedId;
  bool m_open = false;
  bool m_actionOpen = false;

  DirectoryModel *m_peekModel = nullptr;
  FilterProxy *m_peekProxy = nullptr;
  QStringList m_folderStack;
  bool m_filePeekFromFolder = false;
  bool m_peekNavigating = false;
  bool m_gridMode = false;
  int m_peekGridStride = 1;
  bool m_peekPreviewFocused = false;
  bool m_chooserMode = false;
  FilesOnlyProxy *m_fileOnlyProxy = nullptr;
  QUrl m_listingRowUrl;
  QUrl m_listingThumbUrl;
};
