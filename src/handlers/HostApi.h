#pragma once

#include "HandlerActions.h"
#include "HandlerExec.h"
#include "Manifest.h"
#include "PeekHost.h"

#include <QColor>
#include <QQuickItem>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DirectoryModel;
class FileOpEngine;
class FilterProxy;
class FilesOnlyProxy;
class HandlerLoader;
class HandlerRegistry;
class MimeMap;
class NavStack;
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
  Q_PROPERTY(bool doParamsFocused READ doParamsFocused WRITE
                 setDoParamsFocused NOTIFY doFocusChanged)
  Q_PROPERTY(QString doHint READ doHint NOTIFY doHintChanged)
  Q_PROPERTY(QString listHint READ listHint CONSTANT)
  Q_PROPERTY(QQuickItem *doPreviewItem READ doPreviewItem NOTIFY doPreviewChanged)
  Q_PROPERTY(QObject *doFolderModel READ doFolderModel NOTIFY doPreviewChanged)
  Q_PROPERTY(QObject *doFolderProxy READ doFolderProxy NOTIFY doPreviewChanged)
  Q_PROPERTY(bool doTargetIsDir READ doTargetIsDir NOTIFY doPreviewChanged)
  Q_PROPERTY(QString doTargetName READ doTargetName NOTIFY doPreviewChanged)
  Q_PROPERTY(QVariantList openCandidates READ openCandidates NOTIFY
                 openCandidatesChanged)
  Q_PROPERTY(bool folderPeek READ folderPeek NOTIFY folderPeekChanged)
  Q_PROPERTY(bool folderListing READ folderListing NOTIFY folderPeekChanged)
  Q_PROPERTY(QString folderPath READ folderPath NOTIFY folderPeekChanged)
  Q_PROPERTY(QString peekFileName READ peekFileName NOTIFY fileChanged)
  Q_PROPERTY(QObject *peekModel READ peekModel NOTIFY folderPeekChanged)
  Q_PROPERTY(QObject *peekProxy READ peekProxy NOTIFY folderPeekChanged)
  Q_PROPERTY(QObject *peekFileProxy READ peekFileProxy NOTIFY folderPeekChanged)
  Q_PROPERTY(bool gridMode READ gridMode WRITE setGridMode NOTIFY gridModeChanged)
  Q_PROPERTY(bool peekPreviewFocused READ peekPreviewFocused WRITE
                 setPeekPreviewFocused NOTIFY peekFocusChanged)
  Q_PROPERTY(QUrl listingRowUrl READ listingRowUrl NOTIFY listingChromeChanged)
  Q_PROPERTY(QUrl listingThumbUrl READ listingThumbUrl NOTIFY
                 listingChromeChanged)
  Q_PROPERTY(QString peekFindQuery READ peekFindQuery NOTIFY
                 peekFindQueryChanged)

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
  QString listHint() const;
  QQuickItem *doPreviewItem() const { return m_doPreviewItem; }
  QObject *doFolderModel() const;
  QObject *doFolderProxy() const;
  bool doTargetIsDir() const { return m_doTargetIsDir; }
  QString doTargetName() const { return m_doTargetName; }
  QUrl listingRowUrl() const { return m_listingRowUrl; }
  QUrl listingThumbUrl() const { return m_listingThumbUrl; }
  QVariantList openCandidates() const { return m_openCandidates; }
  QString lastError() const { return m_error; }
  HandlerExec &exec() { return m_exec; }
  void setSelection(SelectionModel *sel) { m_sel = sel; }
  void setFileOps(FileOpEngine *ops) { m_ops = ops; }

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
  Q_INVOKABLE QString processCwd(int pid) const;
  Q_INVOKABLE QString defaultShell() const;
  Q_INVOKABLE void registerSurface(QObject *surface);
  Q_INVOKABLE bool openFile(const QString &path, const QString &mime);
  Q_INVOKABLE bool runOpen(const QString &handlerId);
  Q_INVOKABLE bool runTerminal();
  Q_INVOKABLE bool runTrash();
  Q_INVOKABLE bool restoreTrash();
  Q_INVOKABLE bool emptyTrash();
  Q_INVOKABLE bool runAction(const QString &handlerId);
  Q_INVOKABLE bool openWithPalette();
  Q_INVOKABLE bool openDoLayer(const QString &focusId = QString());
  Q_INVOKABLE void closeAction() override;
  Q_INVOKABLE void setDoIndex(int index);
  Q_INVOKABLE void doMove(int delta) override;
  Q_INVOKABLE bool runDoVerb() override;
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
  Q_INVOKABLE QVariantMap readDatabase(const QUrl &url,
                                       const QString &engine,
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
  void doFocusChanged();
  void doHintChanged();
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

private:
  Manifest::Item currentItem() const;
  Manifest::Item itemAt(int proxyRow) const;
  QVector<Manifest::Item> currentItems() const;
  void setCurrentFromModel();
  void reloadPreview();
  void destroyPreview();
  void destroyAction();
  void refreshOpenCandidates();
  void onStatsApplied(const QStringList &paths);
  void destroyActionItem();
  void rebuildDoVerbs();
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
  QQuickItem *m_actionItem = nullptr;
  QQuickItem *m_doSurface = nullptr;
  QQuickItem *m_doContentSurface = nullptr;
  QQuickItem *m_doPreviewItem = nullptr;
  DirectoryModel *m_doFolderModel = nullptr;
  FilterProxy *m_doFolderProxy = nullptr;
  QString m_doTargetName;
  bool m_doTargetIsDir = false;
  struct DoVerb {
    QString id;
    QString name;
    QString description;
    QString runtime;
    QString group;
    bool hasParams = false;
  };
  QVector<DoVerb> m_doVerbs;
  QVector<Manifest::Item> m_doItems;
  QVariantList m_doSelection;
  int m_doIndex = -1;
  bool m_doParamsFocused = false;
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
