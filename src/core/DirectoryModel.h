#pragma once

#include "DirectoryLister.h"
#include "DirectoryWatcher.h"
#include "ThumbnailService.h"

#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QCollator>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class DirectoryModelTest;
class RecentStore;
class SearchModel;
class ThumbnailService;

class DirectoryModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY
                 showHiddenChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(bool listing READ listing NOTIFY listingChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(bool isTrash READ isTrash NOTIFY pathChanged)
  Q_PROPERTY(bool isRecent READ isRecent NOTIFY pathChanged)
  Q_PROPERTY(bool isSearch READ isSearch NOTIFY pathChanged)
  Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchQueryChanged)
  Q_PROPERTY(bool isContentSearch READ isContentSearch NOTIFY searchQueryChanged)
  Q_PROPERTY(QString searchRoot READ searchRoot NOTIFY searchQueryChanged)
  Q_PROPERTY(QVariantList folderGroups READ folderGroups NOTIFY folderGroupsChanged)
  Q_PROPERTY(QAbstractItemModel *folderGroupModel READ folderGroupModel NOTIFY
                 pathChanged)
  Q_PROPERTY(QVariantMap currentStat READ currentStat NOTIFY currentStatChanged)
  Q_PROPERTY(QString volumeHint READ volumeHint NOTIFY volumeHintChanged)
  Q_PROPERTY(bool isVolumes READ isVolumes NOTIFY pathChanged)
  Q_PROPERTY(QVariantList fsnBoxes READ fsnBoxes NOTIFY fsnBoxesChanged)
  Q_PROPERTY(bool fsnListing READ fsnListing NOTIFY fsnListingChanged)

public:
  enum Role {
    NameRole = Qt::UserRole + 1,
    PathRole,
    UriRole,
    IsDirRole,
    SizeRole,
    MtimeRole,
    MimeRole,
    IconNameRole,
    ThumbnailRole,
    IsHiddenRole,
    IsSymlinkRole,
    DirKindRole,
    OrigPathRole,
    PermRole,
    DetailRole,
    UsedRole,
    TotalRole,
    PercentRole,
    ParentPathRole,
    ParentLabelRole,
    TypeLabelRole,
  };
  Q_ENUM(Role)

  explicit DirectoryModel(QObject *parent = nullptr);
  ~DirectoryModel() override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString path() const { return m_path; }
  bool showHidden() const { return m_showHidden; }
  int currentIndex() const;
  int count() const;
  bool listing() const { return m_listing; }
  QString errorString() const { return m_error; }
  QString returnPath() const { return m_returnPath; }
  QString volumeRoot() const { return m_volumeRoot; }
  QString volumeHint() const;
  SearchModel *searchModel() const { return m_search; }
  ThumbnailService *thumbnailService() const { return m_thumbs; }
  bool isTrash() const;
  bool isRecent() const;
  bool isSearch() const;
  bool isVolumes() const;
  QString searchQuery() const;
  bool isContentSearch() const;
  QString searchRoot() const;
  QVariantList folderGroups() const;
  QAbstractItemModel *folderGroupModel() const;
  Q_INVOKABLE QVariantMap rowMap(int row) const;
  QVariantMap currentStat() const;

  static bool isVirtualPath(const QString &path);
  static bool isSearchPath(const QString &path);
  static bool isTrashPath(const QString &path);
  static bool isRecentPath(const QString &path);
  static bool isVolumesPath(const QString &path);

  qint64 lastFirstRowsMs() const { return m_lastFirstRowsMs; }

  Q_INVOKABLE void setPath(const QString &path,
                           const QString &selectName = QString(),
                           bool force = false);
  void setSearchModel(SearchModel *model);
  void setRecentStore(RecentStore *store);
  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setCurrentIndex(int index);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE int stepSearchGrid(int index, int dx, int dy, int columns) const;
  Q_INVOKABLE void activateCurrent();
  void activateIndex(int sourceRow);
  void requestRestore(const QStringList &trashFiles);
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE bool currentIsDir() const;
  Q_INVOKABLE QString currentOrigPath() const;
  Q_INVOKABLE bool restoreCurrent();
  Q_INVOKABLE bool emptyTrash();
  Q_INVOKABLE void refreshFsn(const QString &view = QString());
  QVariantList fsnBoxes() const { return m_fsnBoxes; }
  bool fsnListing() const { return m_fsnListing; }
  Q_INVOKABLE void requestVisibleThumbs(int first, int last, int sizePx);
  Q_INVOKABLE void refreshThumbs(const QStringList &paths);
  void requestSourceThumbs(const QVector<int> &sourceRows, int sizePx);
  QVariantMap cachedStat(const QString &path) const;
  // Direct entry access for FilterProxy's sort/filter hot path: no QVariant
  // boxing, and name ordering via precomputed collation keys (locale-aware,
  // numeric, case-insensitive).
  const DirectoryEntry *entryAt(int visibleRow) const;
  int compareNamesForRows(int leftVisibleRow, int rightVisibleRow) const;
  void requestStatPath(const QString &path);

signals:
  void fileActivated(const QString &path, const QString &mime);
  void restoreRequested(const QStringList &files);
  void pathChanged();
  void showHiddenChanged();
  void currentIndexChanged();
  void countChanged();
  void listingChanged();
  void errorStringChanged();
  void aboutToNavigate();
  void listRequested(quint64 generation, const QString &path);
  void statRequested(quint64 generation, const QString &path,
                     const QStringList &names);
  void firstRowsInserted(qint64 elapsedMs, int rows);
  void currentStatChanged();
  void searchQueryChanged();
  void folderGroupsChanged();
  void volumeHintChanged();
  // Once per stat batch: the paths whose size/mtime/mime just landed.
  void statsApplied(const QStringList &paths);
  void fsnBoxesChanged();
  void fsnListingChanged();

private slots:
  void onBatchReady(quint64 generation, const QVector<DirectoryEntry> &batch);
  void onStatsReady(quint64 generation, const QVector<DirectoryEntry> &batch,
                    bool priority);
  void onFinished(quint64 generation, bool ok, const QString &error);
  void onWatchEvents(const QVector<DirectoryWatchEvent> &events);
  void onThumbnailReady(const QString &path, const QString &url);
  void applyFsnBoxes(quint64 gen, const QVariantList &boxes);

private:
  static QString normalizePath(const QString &path);
  void bindSearch();
  void unbindSearch();
  void adoptSearchRows();
  bool searching() const { return m_searching && m_search; }
  void resetListing();
  void rebuildVisible();
  int applyEntry(const DirectoryEntry &entry); // returns touched visible row
  void maybeActivatePending();
  void maybeSelectPending();
  void insertPlaceholder(const QString &name, bool isDir);
  void removeByName(const QString &name);
  void renameEntry(const QString &from, const QString &to);
  void insertVisible(int allIndex);
  void removeVisible(int allIndex);
  DirectoryEntry makePlaceholder(const QString &name, bool isDir) const;
  DirectoryEntry makeTrashEntry(const QString &name) const;
  DirectoryEntry makeRecentEntry(const QString &path, const QString &mime,
                                 const QString &ts) const;
  void loadTrashListing();
  void loadRecentListing();
  void loadVolumesListing();
  void updateVolumeRoot(const QString &previous, const QString &next);
  void navigateToExistingParent();
  void reload();
  QVariantMap entryToMap(const DirectoryEntry &e) const;
  void emitCurrentStat();

  friend class DirectoryModelTest;

  QThread m_thread;
  DirectoryLister *m_lister = nullptr;
  DirectoryWatcher m_watcher;
  ThumbnailService *m_thumbs = nullptr;
  SearchModel *m_search = nullptr;
  RecentStore *m_recents = nullptr;
  quint64 m_gen = 0;
  quint64 m_watchSerial = 0;

  QString m_path;
  QString m_returnPath;
  QString m_volumeRoot;
  QString m_error;
  QString m_pendingActivate;
  QString m_pendingSelect;
  QVector<DirectoryEntry> m_all;
  QVector<int> m_visible;
  QHash<QString, int> m_indexByName;
  QHash<QString, int> m_indexByPath;
  QHash<int, int> m_visibleRowByAll;
  QSet<QString> m_suppressedNames;
  QSet<QString> m_pendingThumbs;
  int m_thumbFirst = -1;
  int m_thumbLast = -1;
  int m_thumbSizePx = 128;
  QVector<int> m_thumbRows;

  int m_currentIndex = -1;
  bool m_showHidden = false;
  bool m_listing = false;
  bool m_searching = false;
  bool m_loggedFirst = false;
  qint64 m_lastFirstRowsMs = -1;
  QElapsedTimer m_listTimer;
  QCollator m_collator;
  QVector<QCollatorSortKey> m_sortKeys; // parallel to m_all
  QVariantList m_fsnBoxes;
  quint64 m_fsnGen = 0;
  bool m_fsnListing = false;
  QString m_fsnView = QStringLiteral("tree");
};
