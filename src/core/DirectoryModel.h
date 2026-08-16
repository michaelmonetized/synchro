#pragma once

#include "DirectoryLister.h"
#include "DirectoryWatcher.h"
#include "ThumbnailService.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantMap>
#include <QVector>

class DirectoryModelTest;
class RecentStore;
class SearchModel;

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
  Q_PROPERTY(QVariantMap currentStat READ currentStat NOTIFY currentStatChanged)

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
  SearchModel *searchModel() const { return m_search; }
  bool isTrash() const;
  bool isRecent() const;
  QVariantMap currentStat() const;

  static bool isVirtualPath(const QString &path);
  static bool isSearchPath(const QString &path);
  static bool isTrashPath(const QString &path);
  static bool isRecentPath(const QString &path);

  qint64 lastFirstRowsMs() const { return m_lastFirstRowsMs; }

  Q_INVOKABLE void setPath(const QString &path,
                           const QString &selectName = QString(),
                           bool force = false);
  void setSearchModel(SearchModel *model);
  void setRecentStore(RecentStore *store);
  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setCurrentIndex(int index);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE bool currentIsDir() const;
  Q_INVOKABLE QString currentOrigPath() const;
  Q_INVOKABLE bool restoreCurrent();
  Q_INVOKABLE bool emptyTrash();
  Q_INVOKABLE void requestVisibleThumbs(int first, int last, int sizePx);
  QVariantMap cachedStat(const QString &path) const;
  void requestStatPath(const QString &path);

signals:
  void fileActivated(const QString &path, const QString &mime);
  void entryStatReady(const QString &path, const QVariantMap &st);
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

private slots:
  void onBatchReady(quint64 generation, const QVector<DirectoryEntry> &batch);
  void onStatsReady(quint64 generation, const QVector<DirectoryEntry> &batch,
                    bool priority);
  void onFinished(quint64 generation, bool ok, const QString &error);
  void onWatchEvents(const QVector<DirectoryWatchEvent> &events);
  void onThumbnailReady(const QString &path, const QString &url);

private:
  static QString normalizePath(const QString &path);
  void bindSearch();
  void unbindSearch();
  void adoptSearchRows();
  bool searching() const { return m_searching && m_search; }
  void resetListing();
  void rebuildVisible();
  void applyEntry(const DirectoryEntry &entry);
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
  void navigateToExistingParent();
  void reload();
  const DirectoryEntry *entryAt(int visibleRow) const;
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
  QString m_error;
  QString m_pendingActivate;
  QString m_pendingSelect;
  QVector<DirectoryEntry> m_all;
  QVector<int> m_visible;
  QHash<QString, int> m_indexByName;
  QHash<QString, int> m_indexByPath;
  QHash<int, int> m_visibleRowByAll;
  QSet<QString> m_suppressedNames;
  int m_thumbFirst = -1;
  int m_thumbLast = -1;
  int m_thumbSizePx = 128;

  int m_currentIndex = -1;
  bool m_showHidden = false;
  bool m_listing = false;
  bool m_searching = false;
  bool m_loggedFirst = false;
  qint64 m_lastFirstRowsMs = -1;
  QElapsedTimer m_listTimer;
};
