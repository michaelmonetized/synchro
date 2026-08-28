#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QFileSystemWatcher;
class QProcess;
class QTimer;

// Structured view of Omaflow state plus narrowly-scoped author, review, run,
// and dry-run entry points. The bridge never invokes a shell: the discovered
// Omaflow executable receives a fixed argv shape.
class OmaflowBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
  Q_PROPERTY(bool installed READ installed NOTIFY installedChanged)
  Q_PROPERTY(QString version READ version NOTIFY installedChanged)
  Q_PROPERTY(QString executable READ executable NOTIFY installedChanged)
  Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
  Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)
  Q_PROPERTY(QVariantMap staging READ staging NOTIFY stagingChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
  Q_PROPERTY(QString operationRule READ operationRule NOTIFY operationChanged)
  Q_PROPERTY(QString operationKind READ operationKind NOTIFY operationChanged)
  Q_PROPERTY(
      QString operationOutput READ operationOutput NOTIFY operationChanged)
  Q_PROPERTY(QVariantMap operationResult READ operationResult NOTIFY
                 operationChanged)

public:
  explicit OmaflowBridge(QObject *parent = nullptr);
  ~OmaflowBridge() override;

  bool active() const { return m_active; }
  bool installed() const { return !m_executable.isEmpty(); }
  QString version() const { return m_version; }
  QString executable() const { return m_executable; }
  QVariantList rules() const { return m_rules; }
  QVariantList activity() const { return m_activity; }
  QVariantMap staging() const { return m_staging; }
  bool busy() const { return m_process != nullptr; }
  QString operationRule() const { return m_operationRule; }
  QString operationKind() const { return m_operationKind; }
  QString operationOutput() const { return m_operationOutput; }
  QVariantMap operationResult() const { return m_operationResult; }

  Q_INVOKABLE void setActive(bool active);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool author(const QString &request);
  Q_INVOKABLE bool acceptStage();
  Q_INVOKABLE bool rejectStage();
  Q_INVOKABLE bool dryRun(const QString &ruleId);
  Q_INVOKABLE bool run(const QString &ruleId);
  Q_INVOKABLE QVariantList matchingRules(const QVariantList &selection) const;
  Q_INVOKABLE bool runSelection(const QString &ruleId,
                                const QVariantList &selection,
                                const QString &cwd, bool dryRun = false);
  Q_INVOKABLE void cancel();

signals:
  void activeChanged();
  void installedChanged();
  void rulesChanged();
  void activityChanged();
  void stagingChanged();
  void operationChanged();
  void operationFinished(bool ok);

private:
  bool startOperation(const QString &ruleId, bool dryRun,
                      const QVariantList &selection = {},
                      const QString &cwd = QString());
  bool startCliOperation(const QString &kind, const QString &ruleId,
                         const QStringList &args, int timeoutMs = 45000);
  bool knownRule(const QString &ruleId) const;
  QString writeContext(const QVariantList &selection, const QString &cwd);
  void discoverExecutable();
  void reloadState();
  void rearmWatcher();
  void scheduleRefresh();
  void finishOperation(bool ok, const QString &fallback = QString());
  QString discoverFromPluginRoot(const QString &root, QString *version) const;

  QString m_configDir;
  QString m_stateDir;
  QString m_executable;
  QString m_version;
  QVariantList m_rules;
  QVariantList m_activity;
  QVariantMap m_staging;
  bool m_active = false;
  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_refreshTimer = nullptr;
  QTimer *m_pollTimer = nullptr;
  QTimer *m_operationTimer = nullptr;
  QProcess *m_process = nullptr;
  QString m_operationRule;
  QString m_operationKind;
  QString m_operationOutput;
  QVariantMap m_operationResult;
  QByteArray m_processOutput;
  QString m_contextPath;
  QString m_resultPath;
};
