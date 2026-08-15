#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

class DirectoryModel;

class NavStack : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
  Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
  Q_PROPERTY(QString homePath READ homePath CONSTANT)

public:
  explicit NavStack(DirectoryModel *model, QObject *parent = nullptr);

  bool canGoBack() const { return !m_back.isEmpty(); }
  bool canGoForward() const { return !m_forward.isEmpty(); }
  QString homePath() const;

  Q_INVOKABLE void navigate(const QString &path);
  Q_INVOKABLE void goBack();
  Q_INVOKABLE void goForward();
  Q_INVOKABLE void goUp();
  Q_INVOKABLE void goHome();
  Q_INVOKABLE QVariantList pathSegments() const;
  Q_INVOKABLE QVariantList segmentsFor(const QString &path) const;

signals:
  void historyChanged();

private:
  struct Frame {
    QString path;
    QString selectedName;
    QString viewMode;
    QString filter;
  };

  Frame snapshot() const;
  void restore(const Frame &frame);

  DirectoryModel *m_model = nullptr;
  Frame m_current;
  QVector<Frame> m_back;
  QVector<Frame> m_forward;
  bool m_restoring = false;

private slots:
  void onAboutToNavigate();
  void onPathChanged();
};
