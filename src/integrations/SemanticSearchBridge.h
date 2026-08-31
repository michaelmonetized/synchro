#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QProcess;
class QTimer;

// Async bridge to the optional local CLIP text encoder. Results are already in
// DirectoryModel's SQL-result shape, so the current window can render them as
// an ordinary navigable pseudo-folder without involving an agent or spawning a
// second Synchro instance.
class SemanticSearchBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString query READ query NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)

public:
  explicit SemanticSearchBridge(QObject *parent = nullptr);
  ~SemanticSearchBridge() override;

  bool running() const { return m_process != nullptr; }
  QString query() const { return m_query; }
  QString error() const { return m_error; }

  Q_INVOKABLE bool start(const QString &query, const QString &cwd,
                         int limit = 60);
  Q_INVOKABLE void cancel();

signals:
  void changed();
  void resultReady(const QString &label, const QVariantMap &result);

private:
  void finish(bool processOk);

  QProcess *m_process = nullptr;
  QTimer *m_timeout = nullptr;
  QByteArray m_output;
  QByteArray m_errorOutput;
  QString m_query;
  QString m_cwd;
  QString m_error;
};
