#include "SearchApi.h"

#include "Config.h"
#include "FileCatalog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace {

constexpr int kApiVersion = 1;

QString normalizedInputPath(const QString &input) {
  const QUrl url(input);
  const QString raw = url.isLocalFile() ? url.toLocalFile() : input;
  if (raw.trimmed().isEmpty())
    return {};
  return QFileInfo(raw).absoluteFilePath();
}

QStringList stringListOption(const QVariantMap &options, const QString &key) {
  const QVariant value = options.value(key);
  if (value.metaType().id() == QMetaType::QString)
    return {value.toString()};
  return value.toStringList();
}

qint64 integerOption(const QVariantMap &options, const QString &key,
                     qint64 fallback) {
  bool ok = false;
  const qint64 value = options.value(key).toLongLong(&ok);
  return ok ? value : fallback;
}

QVariantMap errorResult(const QString &error, bool transport = false) {
  QVariantMap out{{QStringLiteral("ok"), false},
                  {QStringLiteral("error"), error},
                  {QStringLiteral("apiVersion"), kApiVersion}};
  if (transport)
    out.insert(QStringLiteral("transportError"), true);
  return out;
}

QVariant normalizeDbusValue(const QVariant &value) {
  if (value.metaType().id() == qMetaTypeId<QDBusArgument>()) {
    const QDBusArgument argument = value.value<QDBusArgument>();
    if (argument.currentType() == QDBusArgument::MapType)
      return normalizeDbusValue(qdbus_cast<QVariantMap>(argument));
    if (argument.currentType() == QDBusArgument::ArrayType)
      return normalizeDbusValue(qdbus_cast<QVariantList>(argument));
  }
  if (value.metaType().id() == QMetaType::QVariantMap) {
    QVariantMap normalized;
    const QVariantMap map = value.toMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
      normalized.insert(it.key(), normalizeDbusValue(it.value()));
    return normalized;
  }
  if (value.metaType().id() == QMetaType::QVariantList) {
    QVariantList normalized;
    const QVariantList list = value.toList();
    normalized.reserve(list.size());
    for (const QVariant &item : list)
      normalized.append(normalizeDbusValue(item));
    return normalized;
  }
  return value;
}

bool hasHiddenPathComponent(const QString &path, const QString &root) {
  const QString relative =
      root.isEmpty() ? path : QDir(root).relativeFilePath(path);
  const QStringList components =
      QDir::cleanPath(relative).split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const QString &component : components) {
    if (component.size() > 1 && component.startsWith(QLatin1Char('.')) &&
        component != QLatin1String(".."))
      return true;
  }
  return false;
}

QVariantMap filterSearchResult(QVariantMap result,
                               const QVariantMap &options) {
  const int requestedLimit =
      qBound(1, static_cast<int>(integerOption(options, QStringLiteral("limit"), 20)),
             100);
  const int offset =
      qBound(0, static_cast<int>(integerOption(options, QStringLiteral("offset"), 0)),
             10000);
  QStringList kinds = stringListOption(options, QStringLiteral("kinds"));
  if (kinds.isEmpty() && options.contains(QStringLiteral("kind")))
    kinds = {options.value(QStringLiteral("kind")).toString()};
  for (QString &kind : kinds)
    kind = kind.trimmed().toLower();
  QStringList extensions =
      stringListOption(options, QStringLiteral("extensions"));
  if (extensions.isEmpty() && options.contains(QStringLiteral("extension")))
    extensions = {options.value(QStringLiteral("extension")).toString()};
  for (QString &extension : extensions) {
    extension = extension.trimmed().toLower();
    if (extension.startsWith(QLatin1Char('.')))
      extension.remove(0, 1);
  }
  const bool includeHidden =
      options.value(QStringLiteral("includeHidden"), false).toBool();
  const qint64 minSize = integerOption(options, QStringLiteral("minSize"), -1);
  const qint64 maxSize = integerOption(options, QStringLiteral("maxSize"), -1);
  const qint64 modifiedAfter =
      integerOption(options, QStringLiteral("modifiedAfter"), -1);
  const qint64 modifiedBefore =
      integerOption(options, QStringLiteral("modifiedBefore"), -1);
  const QString scope =
      options.value(QStringLiteral("scope"), QStringLiteral("all"))
          .toString()
          .toLower();
  const QString cwd = QFileInfo(options.value(QStringLiteral("cwd")).toString())
                          .absoluteFilePath();

  QVariantList filtered;
  const QVariantList source = result.value(QStringLiteral("rows")).toList();
  for (const QVariant &value : source) {
    QVariantMap row = value.toMap();
    const QString kind = row.value(QStringLiteral("kind")).toString();
    const QString path = row.value(QStringLiteral("path")).toString();
    if (!kinds.isEmpty() && !kinds.contains(kind))
      continue;
    if (!extensions.isEmpty() &&
        !extensions.contains(row.value(QStringLiteral("extension"))
                                 .toString()
                                 .toLower()))
      continue;
    const QString hiddenRoot =
        (scope == QLatin1String("cwd") ||
         scope == QLatin1String("recursive"))
            ? cwd
            : QString();
    if (!includeHidden &&
        (row.value(QStringLiteral("hidden")).toBool() ||
         hasHiddenPathComponent(path, hiddenRoot)))
      continue;
    const qint64 size = row.value(QStringLiteral("size")).toLongLong();
    const qint64 mtime = row.value(QStringLiteral("mtime")).toLongLong();
    if (minSize >= 0 && size < minSize)
      continue;
    if (maxSize >= 0 && size > maxSize)
      continue;
    if (modifiedAfter >= 0 && mtime < modifiedAfter)
      continue;
    if (modifiedBefore >= 0 && mtime > modifiedBefore)
      continue;
    if ((scope == QLatin1String("cwd") ||
         scope == QLatin1String("recursive")) &&
        !cwd.isEmpty() && path != cwd &&
        !path.startsWith(cwd + QLatin1Char('/')))
      continue;
    if (!path.isEmpty())
      row.insert(QStringLiteral("uri"), QUrl::fromLocalFile(path).toString());
    filtered.append(row);
  }

  const bool candidateWindowFull =
      source.size() >= options.value(QStringLiteral("_fetchLimit")).toInt();
  const int end = qMin(filtered.size(), offset + requestedLimit);
  QVariantList page;
  for (int i = qMin(offset, filtered.size()); i < end; ++i)
    page.append(filtered.at(i));
  const bool hasMore = end < filtered.size();
  result.insert(QStringLiteral("rows"), page);
  result.insert(QStringLiteral("count"), page.size());
  result.insert(QStringLiteral("offset"), offset);
  result.insert(QStringLiteral("limit"), requestedLimit);
  result.insert(QStringLiteral("hasMore"), hasMore);
  result.insert(QStringLiteral("candidateWindowFull"), candidateWindowFull);
  result.insert(QStringLiteral("candidateLimit"),
                options.value(QStringLiteral("_fetchLimit")));
  if (hasMore)
    result.insert(QStringLiteral("nextOffset"),
                  offset + qMax(page.size(), requestedLimit));
  result.insert(QStringLiteral("catalog"),
                FileCatalog::catalogStatus(
                    options.value(QStringLiteral("cwd")).toString()));
  result.insert(QStringLiteral("apiVersion"), kApiVersion);
  return result;
}

} // namespace

class SearchApiAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.omarchy.Synchro.Search1")

public:
  explicit SearchApiAdaptor(SearchApi *api) : QDBusAbstractAdaptor(api), m_api(api) {
    connect(api, &SearchApi::resultsReady, this,
            &SearchApiAdaptor::ResultsReady);
  }

public slots:
  QVariantMap Describe() const { return m_api->describe(); }
  QVariantMap Status(const QVariantMap &options) const {
    return m_api->status(options);
  }
  qulonglong StartSearch(const QString &query, const QVariantMap &options) {
    return m_api->startSearch(query, options);
  }
  qulonglong StartQuery(const QString &sql, const QVariantMap &options) {
    return m_api->startQuery(sql, options);
  }
  QVariantMap Result(qulonglong requestId) const {
    return m_api->result(requestId);
  }
  bool Cancel(qulonglong requestId) { return m_api->cancel(requestId); }
  QVariantMap Open(const QString &path, const QVariantMap &options) {
    return m_api->open(path, options);
  }
  QVariantMap Reveal(const QString &path, const QVariantMap &options) {
    return m_api->reveal(path, options);
  }
  QVariantMap Show(const QString &sql, const QVariantMap &options) {
    return m_api->show(sql, options);
  }

signals:
  void ResultsReady(qulonglong requestId, const QVariantMap &result);

private:
  SearchApi *m_api = nullptr;
};

struct SearchApi::Request {
  qulonglong id = 0;
  QString kind;
  QString state = QStringLiteral("running");
  qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
  QVariantMap payload;
  std::shared_ptr<std::atomic_bool> canceled =
      std::make_shared<std::atomic_bool>(false);
  QFutureWatcher<QVariantMap> *watcher = nullptr;
};

QString SearchApi::defaultServiceName() {
  return QStringLiteral("org.omarchy.Synchro.Search1");
}

QString SearchApi::objectPath() {
  return QStringLiteral("/org/omarchy/Synchro/Search1");
}

QString SearchApi::interfaceName() { return defaultServiceName(); }

SearchApi::SearchApi(QObject *parent)
    : QObject(parent), m_connection(QDBusConnection::sessionBus()) {
  m_pool.setMaxThreadCount(2);
  m_pool.setExpiryTimeout(30000);
  m_pool.setThreadPriority(QThread::LowPriority);
}

SearchApi::~SearchApi() {
  for (Request *request : std::as_const(m_requests))
    request->canceled->store(true);
  m_pool.waitForDone();
  qDeleteAll(m_requests);
  m_requests.clear();
  if (m_connection.isConnected() && !m_serviceName.isEmpty()) {
    m_connection.unregisterObject(objectPath());
    m_connection.unregisterService(m_serviceName);
  }
}

bool SearchApi::start(const QDBusConnection &connection,
                      const QString &serviceName) {
  m_connection = connection;
  m_serviceName = serviceName;
  if (!m_connection.isConnected()) {
    m_error = QStringLiteral("no session bus");
    return false;
  }
  if (!m_adaptor)
    m_adaptor = new SearchApiAdaptor(this);
  if (!m_connection.registerService(serviceName)) {
    m_error = QStringLiteral("could not own %1").arg(serviceName);
    return false;
  }
  if (!m_connection.registerObject(objectPath(), this,
                                   QDBusConnection::ExportAdaptors)) {
    m_connection.unregisterService(serviceName);
    m_error = QStringLiteral("could not export %1").arg(objectPath());
    return false;
  }
  return true;
}

QVariantMap SearchApi::describe() const {
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("apiVersion"), kApiVersion},
          {QStringLiteral("service"), defaultServiceName()},
          {QStringLiteral("objectPath"), objectPath()},
          {QStringLiteral("operations"),
           QStringList{QStringLiteral("search"), QStringLiteral("query"),
                       QStringLiteral("status"), QStringLiteral("open"),
                       QStringLiteral("reveal"), QStringLiteral("show"),
                       QStringLiteral("cancel")}},
          {QStringLiteral("searchModes"),
           QStringList{QStringLiteral("name"), QStringLiteral("content")}},
          {QStringLiteral("filters"),
           QStringList{QStringLiteral("kind"),
                       QStringLiteral("extensions"),
                       QStringLiteral("includeHidden"),
                       QStringLiteral("minSize"), QStringLiteral("maxSize"),
                       QStringLiteral("modifiedAfter"),
                       QStringLiteral("modifiedBefore"),
                       QStringLiteral("scope"), QStringLiteral("cwd")}},
          {QStringLiteral("pagination"), true},
          {QStringLiteral("cancellation"), true},
          {QStringLiteral("signals"),
           QStringList{QStringLiteral("ResultsReady")}},
          {QStringLiteral("contentIndexed"), false},
          {QStringLiteral("contentBackend"),
           QStringLiteral("bounded live ripgrep")},
          {QStringLiteral("currentWindowNavigation"), false}};
}

QVariantMap SearchApi::status(const QVariantMap &options) const {
  const QString cwd = options.value(QStringLiteral("cwd"), QDir::homePath())
                          .toString();
  int pending = 0;
  for (const Request *request : std::as_const(m_requests)) {
    if (request->state == QLatin1String("running"))
      ++pending;
  }
  QVariantMap out{{QStringLiteral("ok"), true},
                  {QStringLiteral("apiVersion"), kApiVersion},
                  {QStringLiteral("service"), defaultServiceName()},
                  {QStringLiteral("pendingRequests"), pending},
                  {QStringLiteral("catalog"), FileCatalog::catalogStatus(cwd)},
                  {QStringLiteral("shadow"), FileCatalog::shadowStatus()}};
  return out;
}

QVariantMap SearchApi::executeSearch(const QString &query,
                                     const QVariantMap &rawOptions) {
  QVariantMap options = rawOptions;
  const int limit =
      qBound(1, static_cast<int>(integerOption(options, QStringLiteral("limit"), 20)),
             100);
  const int offset =
      qBound(0, static_cast<int>(integerOption(options, QStringLiteral("offset"), 0)),
             10000);
  const bool filtered = options.contains(QStringLiteral("kind")) ||
                        options.contains(QStringLiteral("kinds")) ||
                        options.contains(QStringLiteral("extension")) ||
                        options.contains(QStringLiteral("extensions")) ||
                        options.contains(QStringLiteral("minSize")) ||
                        options.contains(QStringLiteral("maxSize")) ||
                        options.contains(QStringLiteral("modifiedAfter")) ||
                        options.contains(QStringLiteral("modifiedBefore")) ||
                        options.value(QStringLiteral("scope")).toString() ==
                            QLatin1String("cwd") ||
                        options.value(QStringLiteral("scope")).toString() ==
                            QLatin1String("recursive") ||
                        !options.value(QStringLiteral("includeHidden"), false)
                             .toBool();
  const QString mode =
      options.value(QStringLiteral("mode"), QStringLiteral("name"))
          .toString()
          .toLower();
  // Keep a generous deterministic candidate window so post-index filters do
  // not yield sparse first pages. The returned page remains bounded; later
  // offsets expand the same ranked prefix up to the documented 500-row
  // operational window.
  const int desiredCandidates =
      (offset + limit + 1) * (filtered ? 8 : 2);
  const int fetchLimit =
      mode == QLatin1String("content")
          ? qBound(1, desiredCandidates, 500)
          : (filtered ? 500 : qBound(128, desiredCandidates, 500));
  options.insert(QStringLiteral("_fetchLimit"), fetchLimit);
  const QString cwd =
      options.value(QStringLiteral("cwd"), QDir::homePath()).toString();
  QVariantMap result;
  if (mode == QLatin1String("content")) {
    const int timeout = qBound(
        250,
        static_cast<int>(integerOption(options, QStringLiteral("timeoutMs"), 5000)),
        30000);
    result = FileCatalog::contentSearchSync(query, cwd, fetchLimit, timeout);
  } else {
    Config config;
    const bool restrictToCwd =
        options.value(QStringLiteral("scope")).toString() ==
            QLatin1String("cwd") ||
        options.value(QStringLiteral("scope")).toString() ==
            QLatin1String("recursive");
    result = FileCatalog::searchSync(query, cwd, config.pins(),
                                     config.sqlBookmarks(), fetchLimit,
                                     restrictToCwd);
    result.insert(QStringLiteral("mode"), QStringLiteral("name"));
  }
  return filterSearchResult(result, options);
}

QVariantMap SearchApi::executeQuery(const QString &sql,
                                    const QVariantMap &options) {
  const QString cwd =
      options.value(QStringLiteral("cwd"), QDir::homePath()).toString();
  const int limit =
      qBound(1, static_cast<int>(integerOption(options, QStringLiteral("limit"), 200)),
             500);
  const QStringList selection =
      stringListOption(options, QStringLiteral("selection"));
  QVariantMap result = FileCatalog::querySync(sql, cwd, selection, limit);
  result.insert(QStringLiteral("count"),
                result.value(QStringLiteral("rows")).toList().size());
  result.insert(QStringLiteral("limit"), limit);
  result.insert(QStringLiteral("catalog"), FileCatalog::catalogStatus(cwd));
  result.insert(QStringLiteral("apiVersion"), kApiVersion);
  result.insert(QStringLiteral("mode"), QStringLiteral("sql"));
  return result;
}

qulonglong SearchApi::startTask(const QString &kind,
                                const std::function<QVariantMap()> &work) {
  auto *request = new Request;
  request->id = m_nextRequestId++;
  request->kind = kind;
  request->watcher = new QFutureWatcher<QVariantMap>(this);
  m_requests.insert(request->id, request);
  const qulonglong id = request->id;
  connect(request->watcher, &QFutureWatcher<QVariantMap>::finished, this,
          [this, id] {
            Request *current = m_requests.value(id);
            if (!current || current->canceled->load())
              return;
            finishRequest(id, current->watcher->result());
          });
  const auto canceled = request->canceled;
  request->watcher->setFuture(QtConcurrent::run(&m_pool, [work, canceled] {
    if (canceled->load())
      return errorResult(QStringLiteral("request canceled"));
    return work();
  }));
  return id;
}

qulonglong SearchApi::startSearch(const QString &query,
                                  const QVariantMap &options) {
  return startTask(QStringLiteral("search"),
                   [query, options] { return executeSearch(query, options); });
}

qulonglong SearchApi::startQuery(const QString &sql,
                                 const QVariantMap &options) {
  return startTask(QStringLiteral("query"),
                   [sql, options] { return executeQuery(sql, options); });
}

void SearchApi::finishRequest(qulonglong requestId,
                              const QVariantMap &payload) {
  Request *request = m_requests.value(requestId);
  if (!request || request->canceled->load())
    return;
  request->state = payload.value(QStringLiteral("ok")).toBool()
                       ? QStringLiteral("ready")
                       : QStringLiteral("failed");
  request->payload = payload;
  request->payload.insert(QStringLiteral("requestId"), requestId);
  request->payload.insert(QStringLiteral("requestKind"), request->kind);
  request->payload.insert(QStringLiteral("state"), request->state);
  request->payload.insert(QStringLiteral("startedAt"), request->startedAt);
  request->payload.insert(QStringLiteral("completedAt"),
                          QDateTime::currentMSecsSinceEpoch());
  emit resultsReady(requestId, request->payload);
  QTimer::singleShot(60000, this,
                     [this, requestId] { expireRequest(requestId); });
}

QVariantMap SearchApi::result(qulonglong requestId) const {
  const Request *request = m_requests.value(requestId);
  if (!request)
    return errorResult(QStringLiteral("unknown request"));
  if (!request->payload.isEmpty())
    return request->payload;
  return {{QStringLiteral("ok"), true},
          {QStringLiteral("apiVersion"), kApiVersion},
          {QStringLiteral("requestId"), requestId},
          {QStringLiteral("requestKind"), request->kind},
          {QStringLiteral("state"), request->state},
          {QStringLiteral("startedAt"), request->startedAt}};
}

bool SearchApi::cancel(qulonglong requestId) {
  Request *request = m_requests.value(requestId);
  if (!request || request->state != QLatin1String("running"))
    return false;
  request->canceled->store(true);
  request->state = QStringLiteral("canceled");
  request->payload = {{QStringLiteral("ok"), false},
                      {QStringLiteral("error"), QStringLiteral("request canceled")},
                      {QStringLiteral("apiVersion"), kApiVersion},
                      {QStringLiteral("requestId"), requestId},
                      {QStringLiteral("requestKind"), request->kind},
                      {QStringLiteral("state"), request->state},
                      {QStringLiteral("startedAt"), request->startedAt},
                      {QStringLiteral("completedAt"),
                       QDateTime::currentMSecsSinceEpoch()}};
  emit resultsReady(requestId, request->payload);
  QTimer::singleShot(60000, this,
                     [this, requestId] { expireRequest(requestId); });
  return true;
}

void SearchApi::expireRequest(qulonglong requestId) {
  Request *request = m_requests.take(requestId);
  if (!request)
    return;
  if (request->watcher && !request->watcher->isFinished()) {
    m_requests.insert(requestId, request);
    QTimer::singleShot(10000, this,
                       [this, requestId] { expireRequest(requestId); });
    return;
  }
  if (request->watcher)
    request->watcher->deleteLater();
  delete request;
}

QVariantMap SearchApi::launchSynchro(const QStringList &arguments,
                                     const QString &operation,
                                     const QVariantMap &details) const {
  const QString executable = QCoreApplication::applicationFilePath();
  const QString systemdRun =
      QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
  bool launched = false;
  if (!systemdRun.isEmpty()) {
    QStringList args{QStringLiteral("--user"), QStringLiteral("--collect"),
                     QStringLiteral("--quiet"),
                     QStringLiteral("--service-type=exec"), executable};
    args.append(arguments);
    launched = QProcess::startDetached(systemdRun, args);
  } else {
    launched = QProcess::startDetached(executable, arguments);
  }
  QVariantMap out = details;
  out.insert(QStringLiteral("ok"), launched);
  out.insert(QStringLiteral("launched"), launched);
  out.insert(QStringLiteral("operation"), operation);
  out.insert(QStringLiteral("apiVersion"), kApiVersion);
  if (!launched)
    out.insert(QStringLiteral("error"), QStringLiteral("launch failed"));
  return out;
}

QVariantMap SearchApi::open(const QString &input,
                            const QVariantMap &options) {
  Q_UNUSED(options);
  const QString path = normalizedInputPath(input);
  const QFileInfo info(path);
  if (!info.exists())
    return errorResult(QStringLiteral("path does not exist"));
  if (info.isDir())
    return launchSynchro({path}, QStringLiteral("open"),
                         {{QStringLiteral("path"), path}});
  const QString xdgOpen = QStandardPaths::findExecutable(QStringLiteral("xdg-open"));
  const bool launched = !xdgOpen.isEmpty() && QProcess::startDetached(xdgOpen, {path});
  QVariantMap out{{QStringLiteral("ok"), launched},
                  {QStringLiteral("launched"), launched},
                  {QStringLiteral("operation"), QStringLiteral("open")},
                  {QStringLiteral("path"), path},
                  {QStringLiteral("apiVersion"), kApiVersion}};
  if (!launched)
    out.insert(QStringLiteral("error"), QStringLiteral("could not open path"));
  return out;
}

QVariantMap SearchApi::reveal(const QString &input,
                              const QVariantMap &options) {
  Q_UNUSED(options);
  const QString path = normalizedInputPath(input);
  if (!QFileInfo::exists(path))
    return errorResult(QStringLiteral("path does not exist"));
  return launchSynchro(
      {QStringLiteral("--select"), path}, QStringLiteral("reveal"),
      {{QStringLiteral("path"), path},
       {QStringLiteral("currentWindow"), false}});
}

QVariantMap SearchApi::show(const QString &sql, const QVariantMap &options) {
  QString error;
  if (!FileCatalog::validateReadOnlySql(sql, &error))
    return errorResult(error);
  const QString cwd =
      options.value(QStringLiteral("cwd"), QDir::homePath()).toString();
  const QString label =
      options.value(QStringLiteral("label"), QStringLiteral("search result"))
          .toString();
  return launchSynchro(
      {QStringLiteral("--sql-query"), sql, QStringLiteral("--sql-cwd"), cwd,
       QStringLiteral("--sql-label"), label},
      QStringLiteral("show"),
      {{QStringLiteral("cwd"), cwd}, {QStringLiteral("label"), label},
       {QStringLiteral("currentWindow"), false}});
}

QVariantMap SearchApiClient::call(const QString &method,
                                  const QVariantList &arguments,
                                  int timeoutMs) {
  QDBusInterface api(SearchApi::defaultServiceName(), SearchApi::objectPath(),
                     SearchApi::interfaceName(),
                     QDBusConnection::sessionBus());
  api.setTimeout(timeoutMs);
  QDBusMessage reply = api.callWithArgumentList(QDBus::Block, method, arguments);
  if (reply.type() == QDBusMessage::ErrorMessage)
    return errorResult(reply.errorMessage(), true);
  QDBusReply<QVariantMap> typedReply(reply);
  if (!typedReply.isValid())
    return errorResult(typedReply.error().message(), true);
  return normalizeDbusValue(typedReply.value()).toMap();
}

bool SearchApiClient::cancel(qulonglong requestId, int timeoutMs) {
  QDBusInterface api(SearchApi::defaultServiceName(), SearchApi::objectPath(),
                     SearchApi::interfaceName(),
                     QDBusConnection::sessionBus());
  api.setTimeout(timeoutMs);
  QDBusReply<bool> reply = api.call(QStringLiteral("Cancel"), requestId);
  return reply.isValid() && reply.value();
}

QVariantMap SearchApiClient::describe(int timeoutMs) {
  return call(QStringLiteral("Describe"), {}, timeoutMs);
}

QVariantMap SearchApiClient::status(const QVariantMap &options,
                                    int timeoutMs) {
  return call(QStringLiteral("Status"), {options}, timeoutMs);
}

QVariantMap SearchApiClient::waitFor(qulonglong requestId, int timeoutMs) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    QVariantMap result = call(QStringLiteral("Result"), {requestId}, 1000);
    const QString state = result.value(QStringLiteral("state")).toString();
    if (state == QLatin1String("ready") || state == QLatin1String("failed") ||
        state == QLatin1String("canceled")) {
      result.insert(QStringLiteral("transport"), QStringLiteral("dbus"));
      return result;
    }
    if (!result.value(QStringLiteral("ok"), true).toBool()) {
      result.insert(QStringLiteral("transport"), QStringLiteral("dbus"));
      return result;
    }
    if (result.value(QStringLiteral("transportError")).toBool())
      return result;
    QThread::msleep(20);
  }
  cancel(requestId);
  return errorResult(QStringLiteral("search service timed out"), true);
}

QVariantMap SearchApiClient::search(const QString &query,
                                    const QVariantMap &options,
                                    int timeoutMs) {
  QDBusInterface api(SearchApi::defaultServiceName(), SearchApi::objectPath(),
                     SearchApi::interfaceName(),
                     QDBusConnection::sessionBus());
  api.setTimeout(qMin(timeoutMs, 3000));
  QDBusReply<qulonglong> reply =
      api.call(QStringLiteral("StartSearch"), query, options);
  if (!reply.isValid())
    return errorResult(reply.error().message(), true);
  return waitFor(reply.value(), timeoutMs);
}

QVariantMap SearchApiClient::query(const QString &sql,
                                   const QVariantMap &options,
                                   int timeoutMs) {
  QDBusInterface api(SearchApi::defaultServiceName(), SearchApi::objectPath(),
                     SearchApi::interfaceName(),
                     QDBusConnection::sessionBus());
  api.setTimeout(qMin(timeoutMs, 3000));
  QDBusReply<qulonglong> reply =
      api.call(QStringLiteral("StartQuery"), sql, options);
  if (!reply.isValid())
    return errorResult(reply.error().message(), true);
  return waitFor(reply.value(), timeoutMs);
}

#include "SearchApi.moc"
