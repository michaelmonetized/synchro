#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QProcess;

struct AgentSkillStatus {
  QString sourceDir;
  QStringList installed;
  QStringList present;
  QStringList conflicts;
  QStringList errors;

  bool ok() const;
  QJsonObject toJson() const;
};

// Zero-configuration bridge between Synchro and Omarchy's selected agent.
// The shared skill teaches independently launched agents about the stable CLI;
// action handoffs additionally receive a self-describing JSON context document.
class AgentIntegration {
public:
  static QString skillSourceDir();
  static AgentSkillStatus installSkill(const QString &home = {},
                                       const QString &sourceDir = {});
  static AgentSkillStatus inspectSkill(const QString &home = {},
                                       const QString &sourceDir = {});

  static QJsonObject context(const QString &manifestPath = {},
                             const QString &cwd = {});
  static QJsonObject doctor(const QString &home = {},
                            const QString &sourceDir = {});
};

// Asynchronous, non-interactive use of Omarchy's configured default agent for
// natural-language file discovery. The result is a read-only SQL location;
// the QML shell decides where to render it, so no second Synchro is launched.
class AgentSearchBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString agent READ agent NOTIFY changed)
  Q_PROPERTY(QString request READ request NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QStringList thumbnails READ thumbnails NOTIFY thumbnailsChanged)

public:
  explicit AgentSearchBridge(QObject *parent = nullptr);
  ~AgentSearchBridge() override;

  bool running() const { return m_process != nullptr || m_fakePending; }
  QString agent() const { return m_agent; }
  QString request() const { return m_request; }
  QString error() const { return m_error; }
  QStringList thumbnails() const { return m_thumbnails; }

  Q_INVOKABLE bool start(const QString &request, const QString &cwd);
  Q_INVOKABLE void cancel();

  static QString promptFor(const QString &request, const QString &cwd,
                           const QString &binary);
  static QVariantMap parseResponse(const QByteArray &response,
                                   QString *error = nullptr);

signals:
  void changed();
  void thumbnailsChanged();
  void resultReady(const QString &label, const QString &sql,
                   const QString &cwd);

private:
  void finish(bool processOk);
  void loadThumbnails();
  void clearResultFile();

  QProcess *m_process = nullptr;
  QByteArray m_output;
  QString m_agent;
  QString m_request;
  QString m_cwd;
  QString m_error;
  QString m_resultPath;
  QStringList m_thumbnails;
  bool m_fakePending = false;
};
