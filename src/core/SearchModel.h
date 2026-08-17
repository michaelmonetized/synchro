#pragma once

#include "DirectoryLister.h"
#include "SearchService.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>

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
  bool running() const { return m_service.running(); }
  qint64 lastFirstRowsMs() const { return m_lastFirstRowsMs; }

  const DirectoryEntry *entryAt(int row) const;
  QVariantMap cachedStat(const QString &path) const;

  Q_INVOKABLE void start(const QString &query, const QString &root,
                         bool hidden);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void clear();
  Q_INVOKABLE void setShowHidden(bool hidden);
  Q_INVOKABLE void setCurrentIndex(int index);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE bool currentIsDir() const;
  void setThumbnail(const QString &path, const QString &url);

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

  SearchService m_service;
  QVector<DirectoryEntry> m_entries;
  QHash<QString, int> m_indexByPath;
  QString m_query;
  QString m_root;
  QString m_error;
  int m_currentIndex = -1;
  qint64 m_lastFirstRowsMs = -1;
  bool m_hidden = false;
  bool m_listing = false;
  bool m_loggedFirst = false;
  bool m_replaceOnNextHit = false;
};
