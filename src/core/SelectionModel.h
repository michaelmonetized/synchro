#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class DirectoryModel;
class FilterProxy;

// Cursor + selected set over the filtered listing (K17).
// Default selected = {cursor}. Space stays peek — not a member of this set.
class SelectionModel : public QObject {
  Q_OBJECT
  Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY cursorChanged)
  Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
  Q_PROPERTY(int epoch READ epoch NOTIFY epochChanged)
  Q_PROPERTY(bool visual READ visual NOTIFY visualChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
  explicit SelectionModel(FilterProxy *proxy, DirectoryModel *model,
                          QObject *parent = nullptr);

  int cursor() const;
  int selectedCount() const;
  int epoch() const { return m_epoch; }
  bool visual() const { return m_visual; }
  QString statusText() const;

  Q_INVOKABLE void setCursor(int proxyRow);
  Q_INVOKABLE void moveCursor(int delta);
  Q_INVOKABLE void toggleCursor();
  Q_INVOKABLE void toggleRow(int proxyRow);
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void click(int proxyRow);
  Q_INVOKABLE void leftClick(int proxyRow);
  // StrataV/MapV render recursive descendants which need not have a row in the
  // current directory proxy. Keep one such path as a proper browser selection
  // so previews, actions, and file operations all target what was clicked.
  Q_INVOKABLE void selectPath(const QString &path,
                              const QString &name = QString(),
                              bool isDir = false, qint64 size = -1);
  Q_INVOKABLE void shiftClick(int proxyRow);
  Q_INVOKABLE void ctrlClick(int proxyRow);
  Q_INVOKABLE void enterVisual();
  Q_INVOKABLE void exitVisual();
  Q_INVOKABLE void collapseToCursor();
  Q_INVOKABLE void activate();
  Q_INVOKABLE bool isSelected(int proxyRow) const;
  Q_INVOKABLE QVariantList selectedIndices() const;
  Q_INVOKABLE QStringList selectedPaths() const;
  Q_INVOKABLE QVariantMap previewSummary(int itemLimit = 10,
                                         int aggregateLimit = 10000) const;
  Q_INVOKABLE QString cursorPath() const;
  Q_INVOKABLE QString cursorName() const;
  Q_INVOKABLE QVariantMap primaryItem() const;

signals:
  void cursorChanged();
  void selectionChanged();
  void epochChanged();
  void visualChanged();
  void statusTextChanged();

private:
  void onCursorChanged();
  void onPathChanged();
  void rematch();
  void applyRange(int a, int b);
  void replaceSelected(const QSet<int> &rows);
  void setAnchor(int proxyRow);
  void syncNames();
  bool clearExternalItem();
  int findName(const QString &name) const;
  QString nameAt(int proxyRow) const;
  QString pathAt(int proxyRow) const;
  QList<int> selectedInOrder() const;
  bool isDirAt(int proxyRow) const;

  FilterProxy *m_proxy = nullptr;
  DirectoryModel *m_model = nullptr;
  QSet<int> m_selected;
  QVariantMap m_externalItem;
  QStringList m_selectedNames;
  QString m_cursorName;
  QString m_anchorName;
  int m_anchor = 0;
  int m_epoch = 0;
  bool m_visual = false;
  bool m_leaveDir = false;
  bool m_suppressFollow = false;
};
