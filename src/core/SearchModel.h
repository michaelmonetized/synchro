#pragma once

#include "DirectoryLister.h"
#include "SearchService.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class SearchServiceTest;

// Incremental folder sections for the search grid. A QVariantList rebuild
// would recreate every QML cell on each hit (wrong row + freeze).
class SearchFolderModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Role {
    PathRole = Qt::UserRole + 1,
    LabelRole,
    FirstRole,
    CountRole,
  };

  explicit SearchFolderModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void clear();
  void noteInsert(int entryRow, const QString &parentPath,
                  const QString &label);
  QVariantList toVariantList() const;
  // Grid down/up follows each folder's own rows, not a flat stride
  // across the whole result list.
  int stepVisual(int index, int dx, int dy, int columns) const;

private:
  struct Group {
    QString path;
    QString label;
    int first = 0;
    int count = 0;
  };

  QVector<Group> m_groups;
};

// Core adapter for search://. Same roles as DirectoryModel so the list / peek
// / Enter path can bind it (or DirectoryModel can forward to it).
class SearchModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString path READ path NOTIFY pathChanged)
  Q_PROPERTY(QString query READ query NOTIFY queryChanged)
  Q_PROPERTY(QString root READ root NOTIFY rootChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(bool listing READ listing NOTIFY listingChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY
                 showHiddenChanged)
  Q_PROPERTY(bool contentSearch READ contentSearch NOTIFY kindChanged)
  Q_PROPERTY(QVariantList folderGroups READ folderGroups NOTIFY folderGroupsChanged)

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
  };
  Q_ENUM(Role)

  explicit SearchModel(QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString path() const { return QStringLiteral("search://"); }
  QString query() const { return m_query; }
  QString root() const { return m_root; }
  int currentIndex() const { return m_currentIndex; }
  int count() const { return m_entries.size(); }
  bool listing() const { return m_listing; }
  QString errorString() const { return m_error; }
  bool showHidden() const { return m_hidden; }
  bool contentSearch() const { return m_content; }
  QVariantList folderGroups() const;
  SearchFolderModel *folderGroupModel() { return m_folderModel; }
  const SearchFolderModel *folderGroupModel() const { return m_folderModel; }
  bool running() const { return m_service.running(); }
  qint64 lastFirstRowsMs() const { return m_lastFirstRowsMs; }

  const DirectoryEntry *entryAt(int row) const;
  QVariantMap cachedStat(const QString &path) const;

  Q_INVOKABLE void start(const QString &query, const QString &root, bool hidden,
                         bool content = false);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void clear();
  Q_INVOKABLE void setShowHidden(bool hidden);
  Q_INVOKABLE void setCurrentIndex(int index);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE bool currentIsDir() const;
  void setThumbnail(const QString &path, const QString &url);
  Q_INVOKABLE QVariantMap rowMap(int row) const;
  static QString folderLabel(const QString &parentPath, const QString &root);

  SearchService &service() { return m_service; }
  const SearchService &service() const { return m_service; }

signals:
  void pathChanged();
  void queryChanged();
  void rootChanged();
  void currentIndexChanged();
  void countChanged();
  void listingChanged();
  void errorStringChanged();
  void showHiddenChanged();
  void kindChanged();
  void folderGroupsChanged();
  void fileActivated(const QString &path, const QString &mime);
  void navigateRequested(const QString &path);
  void firstRowsInserted(qint64 elapsedMs, int rows);

private:
  void onHit(const QString &path);
  void onFinished(bool ok, const QString &error);
  void resetEntries();
  void setListing(bool on);
  void setError(const QString &error);
  DirectoryEntry makeEntry(const QString &path) const;

  friend class SearchServiceTest;

  SearchService m_service;
  SearchFolderModel *m_folderModel = nullptr;
  QVector<DirectoryEntry> m_entries;
  QHash<QString, int> m_indexByPath;
  QString m_query;
  QString m_root;
  QString m_error;
  int m_currentIndex = -1;
  qint64 m_lastFirstRowsMs = -1;
  bool m_hidden = false;
  bool m_content = false;
  bool m_listing = false;
  bool m_loggedFirst = false;
};
