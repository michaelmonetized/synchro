#pragma once

#include <QSortFilterProxyModel>
#include <QString>

class DirectoryModel;

// Name substring filter in C++ so the command field never walks rows in QML.
class FilterProxy : public QSortFilterProxyModel {
  Q_OBJECT
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
  Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY
                 currentIndexChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
  explicit FilterProxy(QObject *parent = nullptr);

  void setDirectoryModel(DirectoryModel *model);
  DirectoryModel *directoryModel() const;

  QString filter() const { return m_filter; }
  Q_INVOKABLE void setFilter(const QString &filter);

  int currentIndex() const;
  Q_INVOKABLE void setCurrentIndex(int proxyRow);
  int count() const { return rowCount(); }

  Q_INVOKABLE void selectRow(int proxyRow);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void activateCurrent();
  Q_INVOKABLE QString currentName() const;
  Q_INVOKABLE int seekPrefix(const QString &prefix);

signals:
  void filterChanged();
  void currentIndexChanged();
  void countChanged();

protected:
  bool filterAcceptsRow(int sourceRow,
                        const QModelIndex &sourceParent) const override;

private:
  void bindSource(DirectoryModel *model);
  void snapCursorIfHidden();

  QString m_filter;
  bool m_syncing = false;
};
