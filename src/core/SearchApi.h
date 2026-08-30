#pragma once

#include <QDBusConnection>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

class SearchApiAdaptor;

// Versioned, desktop-neutral search surface owned by synchro-indexd. The API
// deliberately speaks ordinary maps and request IDs: QML, shell launchers,
// agents, and a future LocalSearch adapter can share it without inheriting the
// browser UI or Tracker's ontology.
class SearchApi : public QObject {
  Q_OBJECT

public:
  static QString defaultServiceName();
  static QString objectPath();
  static QString interfaceName();

  explicit SearchApi(QObject *parent = nullptr);
  ~SearchApi() override;

  bool start(const QDBusConnection &connection = QDBusConnection::sessionBus(),
             const QString &serviceName = defaultServiceName());
  QString lastError() const { return m_error; }
  QString serviceName() const { return m_serviceName; }

  QVariantMap describe() const;
  QVariantMap status(const QVariantMap &options = {}) const;
  qulonglong startSearch(const QString &query, const QVariantMap &options = {});
  qulonglong startQuery(const QString &sql, const QVariantMap &options = {});
  QVariantMap result(qulonglong requestId) const;
  bool cancel(qulonglong requestId);
  QVariantMap open(const QString &path, const QVariantMap &options = {});
  QVariantMap reveal(const QString &path, const QVariantMap &options = {});
  QVariantMap show(const QString &sql, const QVariantMap &options = {});

  // Direct seams keep source-tree clients and tests useful when the packaged
  // user service is unavailable. Public clients should prefer D-Bus.
  static QVariantMap executeSearch(const QString &query,
                                   const QVariantMap &options = {});
  static QVariantMap executeQuery(const QString &sql,
                                  const QVariantMap &options = {});

signals:
  void resultsReady(qulonglong requestId, const QVariantMap &result);

private:
  struct Request;
  qulonglong startTask(const QString &kind,
                       const std::function<QVariantMap()> &work);
  QVariantMap launchSynchro(const QStringList &arguments,
                            const QString &operation,
                            const QVariantMap &details = {}) const;
  void finishRequest(qulonglong requestId, const QVariantMap &payload);
  void expireRequest(qulonglong requestId);

  QDBusConnection m_connection = QDBusConnection::sessionBus();
  QString m_serviceName;
  QString m_error;
  SearchApiAdaptor *m_adaptor = nullptr;
  QThreadPool m_pool;
  QHash<qulonglong, Request *> m_requests;
  qulonglong m_nextRequestId = 1;
};

// Small client used by Synchro's own launcher and available as the reference
// behavior for future integrations.
class SearchApiClient {
public:
  static QVariantMap describe(int timeoutMs = 2000);
  static QVariantMap status(const QVariantMap &options = {},
                            int timeoutMs = 5000);
  static QVariantMap search(const QString &query,
                            const QVariantMap &options = {},
                            int timeoutMs = 7000);
  static QVariantMap query(const QString &sql,
                           const QVariantMap &options = {},
                           int timeoutMs = 12000);
  static QVariantMap call(const QString &method, const QVariantList &arguments,
                          int timeoutMs = 5000);
  static bool cancel(qulonglong requestId, int timeoutMs = 1000);

private:
  static QVariantMap waitFor(qulonglong requestId, int timeoutMs);
};
