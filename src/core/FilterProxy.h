#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantMap>
#include <QVector>

class DirectoryModel;

struct PortalFilterRule {
  uint type = 0; // 0 = glob, 1 = mime
  QString pattern;
};

// Name substring filter in C++ so the command field never walks rows in QML.
class FilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString sortRoleName READ sortRoleName WRITE setSortRoleName
                 NOTIFY sortChanged)
  Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY
                 sortChanged)

public:
  explicit FilterProxy(QObject *parent = nullptr);

  void setDirectoryModel(DirectoryModel *model);
  DirectoryModel *directoryModel() const;

  QString filter() const { return m_filter; }
  Q_INVOKABLE void setFilter(const QString &filter);

  int currentIndex() const;
  Q_INVOKABLE void setCurrentIndex(int proxyRow);
  int count() const { return rowCount(); }

  QString sortRoleName() const { return m_sortRole; }
  QString sortOrder() const { return m_sortOrder; }
  Q_INVOKABLE void setSortRoleName(const QString &role);
  Q_INVOKABLE void setSortOrder(const QString &order);

  Q_INVOKABLE void selectRow(int proxyRow);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE int seekPrefix(const QString &prefix);
  Q_INVOKABLE void requestVisibleThumbs(int first, int last, int sizePx);
  Q_INVOKABLE QVariantMap rowMap(int proxyRow) const;
  void setPortalRules(const QVector<PortalFilterRule> &rules);

signals:
  void filterChanged();
  void currentIndexChanged();
  void countChanged();
  void sortChanged();

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;
  bool lessThan(const QModelIndex &left,
                const QModelIndex &right) const override;

private:
  void bindSource(DirectoryModel *model);
  void snapCursorIfHidden();

  void applySort();
  bool keepSourceOrder() const;
  static int roleFromName(const QString &name);

  bool matchesPortal(const QString &name, const QString &path, bool isDir,
                     const QString &mime) const;

  QString m_filter;
  QString m_sortRole = QStringLiteral("name");
  QString m_sortOrder = QStringLiteral("asc");
  bool m_syncing = false;
  QVector<PortalFilterRule> m_portalRules;
};

// Peek grid index: files only. Source is a FilterProxy.
class FilesOnlyProxy : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  explicit FilesOnlyProxy(QObject *parent = nullptr);

  void setListing(FilterProxy *src);
  int currentIndex() const;
  Q_INVOKABLE void setCurrentIndex(int proxyRow);
  int count() const { return rowCount(); }
  Q_INVOKABLE void requestVisibleThumbs(int first, int last, int sizePx);

signals:
  void currentIndexChanged();
  void countChanged();

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;

private:
  FilterProxy *m_listing = nullptr;
};
