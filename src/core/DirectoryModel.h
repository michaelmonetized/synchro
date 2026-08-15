#pragma once

#include "DirectoryLister.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QThread>
#include <QVector>

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

  explicit DirectoryModel(QObject *parent = nullptr);
  ~DirectoryModel() override;

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QString path() const { return m_path; }
  bool showHidden() const { return m_showHidden; }
  int currentIndex() const { return m_currentIndex; }
  int count() const { return m_visible.size(); }
  bool listing() const { return m_listing; }
  QString errorString() const { return m_error; }

  qint64 lastFirstRowsMs() const { return m_lastFirstRowsMs; }

  Q_INVOKABLE void setPath(const QString &path);
  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setCurrentIndex(int index);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();

signals:
  void pathChanged();
  void showHiddenChanged();
  void currentIndexChanged();
  void countChanged();
  void listingChanged();
  void errorStringChanged();
  void listRequested(quint64 generation, const QString &path);
  void firstRowsInserted(qint64 elapsedMs, int rows);

private slots:
  void onBatchReady(quint64 generation, const QVector<DirectoryEntry> &batch);
  void onStatsReady(quint64 generation, const QVector<DirectoryEntry> &batch,
                    bool priority);
  void onFinished(quint64 generation, bool ok, const QString &error);

private:
  static QString normalizePath(const QString &path);
  void resetListing();
  void rebuildVisible();
  void applyEntry(const DirectoryEntry &entry);
  void maybeActivatePending();
  const DirectoryEntry *entryAt(int visibleRow) const;

  QThread m_thread;
  DirectoryLister *m_lister = nullptr;
  quint64 m_gen = 0;

  QString m_path;
  QString m_error;
  QString m_pendingActivate;
  QVector<DirectoryEntry> m_all;
  QVector<int> m_visible;
  QHash<QString, int> m_indexByName;

  int m_currentIndex = -1;
  bool m_showHidden = false;
  bool m_listing = false;
  bool m_loggedFirst = false;
  qint64 m_lastFirstRowsMs = -1;
  QElapsedTimer m_listTimer;
};
