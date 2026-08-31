#include "Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {

QStringList jsonStringList(const QJsonValue &v) {
  QStringList out;
  if (!v.isArray())
    return out;
  for (const QJsonValue &item : v.toArray()) {
    if (item.isString() && !item.toString().isEmpty())
      out.append(item.toString());
  }
  return out;
}

QString normalizeView(const QString &view) {
  if (view == QLatin1String("grid"))
    return QStringLiteral("grid");
  return QStringLiteral("list");
}

QString normalizeSortRole(const QString &role) {
  if (role == QLatin1String("size") || role == QLatin1String("mtime") ||
      role == QLatin1String("type"))
    return role;
  return QStringLiteral("name");
}

QString normalizeSortOrder(const QString &order) {
  if (order == QLatin1String("desc"))
    return QStringLiteral("desc");
  return QStringLiteral("asc");
}

QString sanitizeLastPath(const QString &path) {
  const QString t = path.trimmed();
  if (t.isEmpty() || t.startsWith(QLatin1String("search:")))
    return QDir::homePath();
  return t;
}

QVariantMap normalizeSqlBookmark(const QVariantMap &raw) {
  QVariantMap out;
  QString id = raw.value(QStringLiteral("id")).toString().trimmed();
  const QString name = raw.value(QStringLiteral("name")).toString().trimmed();
  const QString sql = raw.value(QStringLiteral("sql")).toString().trimmed();
  QString cwd = Config::normalizePin(
      raw.value(QStringLiteral("cwd")).toString());
  if (name.isEmpty() || sql.isEmpty() || cwd.isEmpty())
    return out;
  if (id.isEmpty())
    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  out.insert(QStringLiteral("id"), id.left(80));
  out.insert(QStringLiteral("name"), name.left(80));
  out.insert(QStringLiteral("sql"), sql.left(32768));
  out.insert(QStringLiteral("cwd"), cwd);
  return out;
}

} // namespace

Config::Config(QObject *parent) : Config(QString(), parent) {}

Config::Config(const QString &filePath, QObject *parent)
    : QObject(parent), m_path(filePath.isEmpty() ? defaultPath() : filePath) {
  applyDefaults();
  load();
}

QString Config::defaultPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericConfigLocation))
      .filePath(QStringLiteral("synchro/config.json"));
}

QStringList Config::defaultLocationChips() {
  return {QStringLiteral("synchro.location.home"),
          QStringLiteral("synchro.location.recent"),
          QStringLiteral("synchro.location.trash"),
          QStringLiteral("synchro.location.volumes")};
}

void Config::applyDefaults() {
  m_version = 1;
  m_writable = true;
  m_showHidden = false;
  m_view = QStringLiteral("list");
  m_sortRole = QStringLiteral("name");
  m_sortOrder = QStringLiteral("asc");
  m_chips = defaultLocationChips();
  m_pins.clear();
  m_sqlBookmarks.clear();
  m_lastPath.clear();
  m_panelSide = QStringLiteral("bottom");
  m_panelSize = 260;
  m_panelOpen = false;
  m_panelApp = QStringLiteral("synchro.panel.terminal");
  m_panelLookOpen = true;
  m_panelLookRatio = 0.34;
  m_lookSize = 360;
  m_lookSide.clear();
  m_gridSize = 132;
  m_foregroundThumbnailWorkers = 2;
  m_foregroundImageFacts = true;
  m_backgroundCatalogEnabled = true;
  m_backgroundRecursiveScan = true;
  m_backgroundScanIntervalMinutes = 360;
  m_backgroundWatchDebounceMs = 650;
  m_backgroundWatchNeighborhood = 128;
  m_backgroundMaxWatches = 2048;
  m_semanticImageEmbeddings = false;
  m_semanticBatchSize = 16;
  m_semanticIntervalSeconds = 60;
}

static QString normalizePanelSide(const QString &side) {
  if (side == QLatin1String("left") || side == QLatin1String("right") ||
      side == QLatin1String("top"))
    return side;
  return QStringLiteral("bottom");
}

static QString normalizeLookSide(const QString &side) {
  if (side == QLatin1String("left") || side == QLatin1String("right") ||
      side == QLatin1String("top") || side == QLatin1String("bottom"))
    return side;
  // Empty means automatic: share a compatible app panel, otherwise right.
  return {};
}

bool Config::load() {
  applyDefaults();
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  if (!doc.isObject())
    return false;
  const QJsonObject obj = doc.object();
  const int version = obj.value(QStringLiteral("version")).toInt(1);
  m_version = version;
  // Newer schema: read known keys, refuse to write.
  m_writable = version <= 1;
  if (obj.contains(QStringLiteral("showHidden")))
    m_showHidden = obj.value(QStringLiteral("showHidden")).toBool();
  if (obj.contains(QStringLiteral("view")))
    m_view = normalizeView(obj.value(QStringLiteral("view")).toString());
  const QJsonObject sort = obj.value(QStringLiteral("sort")).toObject();
  if (!sort.isEmpty()) {
    m_sortRole =
        normalizeSortRole(sort.value(QStringLiteral("role")).toString());
    m_sortOrder =
        normalizeSortOrder(sort.value(QStringLiteral("order")).toString());
  }
  if (obj.contains(QStringLiteral("locationChips"))) {
    const QStringList chips =
        jsonStringList(obj.value(QStringLiteral("locationChips")));
    if (!chips.isEmpty())
      m_chips = chips;
  }
  if (obj.contains(QStringLiteral("lastPath")))
    m_lastPath = sanitizeLastPath(obj.value(QStringLiteral("lastPath")).toString());
  const QJsonObject panel = obj.value(QStringLiteral("panel")).toObject();
  if (!panel.isEmpty()) {
    m_panelSide =
        normalizePanelSide(panel.value(QStringLiteral("side")).toString());
    const int px = panel.value(QStringLiteral("size")).toInt(260);
    m_panelSize = qBound(120, px, 2000);
    m_panelOpen = panel.value(QStringLiteral("open")).toBool(false);
    const QString app = panel.value(QStringLiteral("app")).toString();
    if (!app.isEmpty())
      m_panelApp = app;
    m_panelLookOpen = panel.value(QStringLiteral("lookOpen")).toBool(true);
    m_panelLookRatio =
        qBound(0.2, panel.value(QStringLiteral("lookRatio")).toDouble(0.34),
               0.5);
    m_lookSize = qBound(
        240, panel.value(QStringLiteral("lookSize")).toInt(360), 1200);
    m_lookSide =
        normalizeLookSide(panel.value(QStringLiteral("lookSide")).toString());
  }
  if (obj.contains(QStringLiteral("gridSize")))
    m_gridSize = qBound(88, obj.value(QStringLiteral("gridSize")).toInt(132), 240);
  const QJsonObject indexers = obj.value(QStringLiteral("indexers")).toObject();
  const QJsonObject foreground =
      indexers.value(QStringLiteral("foreground")).toObject();
  if (!foreground.isEmpty()) {
    m_foregroundThumbnailWorkers = qBound(
        1, foreground.value(QStringLiteral("thumbnailWorkers")).toInt(2), 4);
    m_foregroundImageFacts =
        foreground.value(QStringLiteral("imageFacts")).toBool(true);
  }
  const QJsonObject background =
      indexers.value(QStringLiteral("background")).toObject();
  if (!background.isEmpty()) {
    m_backgroundCatalogEnabled =
        background.value(QStringLiteral("catalogEnabled")).toBool(true);
    m_backgroundRecursiveScan =
        background.value(QStringLiteral("recursiveScan")).toBool(true);
    m_backgroundScanIntervalMinutes = qBound(
        15, background.value(QStringLiteral("scanIntervalMinutes")).toInt(360),
        7 * 24 * 60);
    m_backgroundWatchDebounceMs = qBound(
        25, background.value(QStringLiteral("watchDebounceMs")).toInt(650),
        10000);
    m_backgroundWatchNeighborhood = qBound(
        0, background.value(QStringLiteral("watchNeighborhood")).toInt(128),
        2048);
    m_backgroundMaxWatches = qBound(
        16, background.value(QStringLiteral("maxWatches")).toInt(2048),
        65536);
  }
  const QJsonObject semantic =
      indexers.value(QStringLiteral("semantic")).toObject();
  if (!semantic.isEmpty()) {
    m_semanticImageEmbeddings =
        semantic.value(QStringLiteral("imageEmbeddings")).toBool(false);
    m_semanticBatchSize = qBound(
        1, semantic.value(QStringLiteral("batchSize")).toInt(16), 64);
    m_semanticIntervalSeconds = qBound(
        15, semantic.value(QStringLiteral("intervalSeconds")).toInt(60),
        60 * 60);
  }
  if (obj.contains(QStringLiteral("pins"))) {
    QStringList pins;
    for (const QString &raw :
         jsonStringList(obj.value(QStringLiteral("pins")))) {
      const QString pin = normalizePin(raw);
      if (!pin.isEmpty() && !pins.contains(pin))
        pins.append(pin);
    }
    m_pins = pins;
  }
  const QJsonArray sqlBookmarks =
      obj.value(QStringLiteral("sqlBookmarks")).toArray();
  for (const QJsonValue &value : sqlBookmarks) {
    if (m_sqlBookmarks.size() >= 24 || !value.isObject())
      break;
    const QVariantMap bookmark =
        normalizeSqlBookmark(value.toObject().toVariantMap());
    if (!bookmark.isEmpty())
      m_sqlBookmarks.append(bookmark);
  }
  return true;
}

bool Config::save() const {
  if (!m_writable)
    return false;
  const QString dir = QFileInfo(m_path).absolutePath();
  if (!QDir().mkpath(dir))
    return false;
  QJsonObject sort;
  sort.insert(QStringLiteral("role"), m_sortRole);
  sort.insert(QStringLiteral("order"), m_sortOrder);
  QJsonArray chips;
  for (const QString &id : m_chips)
    chips.append(id);
  QJsonObject obj;
  obj.insert(QStringLiteral("version"), 1);
  obj.insert(QStringLiteral("showHidden"), m_showHidden);
  obj.insert(QStringLiteral("view"), m_view);
  obj.insert(QStringLiteral("sort"), sort);
  obj.insert(QStringLiteral("locationChips"), chips);
  QJsonArray pins;
  for (const QString &path : m_pins)
    pins.append(path);
  obj.insert(QStringLiteral("pins"), pins);
  obj.insert(QStringLiteral("sqlBookmarks"),
             QJsonArray::fromVariantList(m_sqlBookmarks));
  obj.insert(QStringLiteral("lastPath"), m_lastPath);
  QJsonObject panel;
  panel.insert(QStringLiteral("side"), m_panelSide);
  panel.insert(QStringLiteral("size"), m_panelSize);
  panel.insert(QStringLiteral("open"), m_panelOpen);
  panel.insert(QStringLiteral("app"), m_panelApp);
  panel.insert(QStringLiteral("lookOpen"), m_panelLookOpen);
  panel.insert(QStringLiteral("lookRatio"), m_panelLookRatio);
  panel.insert(QStringLiteral("lookSize"), m_lookSize);
  panel.insert(QStringLiteral("lookSide"), m_lookSide);
  obj.insert(QStringLiteral("panel"), panel);
  obj.insert(QStringLiteral("gridSize"), m_gridSize);
  QJsonObject foreground;
  foreground.insert(QStringLiteral("thumbnailWorkers"),
                    m_foregroundThumbnailWorkers);
  foreground.insert(QStringLiteral("imageFacts"), m_foregroundImageFacts);
  QJsonObject background;
  background.insert(QStringLiteral("catalogEnabled"),
                    m_backgroundCatalogEnabled);
  background.insert(QStringLiteral("recursiveScan"),
                    m_backgroundRecursiveScan);
  background.insert(QStringLiteral("scanIntervalMinutes"),
                    m_backgroundScanIntervalMinutes);
  background.insert(QStringLiteral("watchDebounceMs"),
                    m_backgroundWatchDebounceMs);
  background.insert(QStringLiteral("watchNeighborhood"),
                    m_backgroundWatchNeighborhood);
  background.insert(QStringLiteral("maxWatches"), m_backgroundMaxWatches);
  QJsonObject semantic;
  semantic.insert(QStringLiteral("imageEmbeddings"),
                  m_semanticImageEmbeddings);
  semantic.insert(QStringLiteral("batchSize"), m_semanticBatchSize);
  semantic.insert(QStringLiteral("intervalSeconds"),
                  m_semanticIntervalSeconds);
  semantic.insert(QStringLiteral("imageModel"), semanticImageModel());
  QJsonObject indexers;
  indexers.insert(QStringLiteral("foreground"), foreground);
  indexers.insert(QStringLiteral("background"), background);
  indexers.insert(QStringLiteral("semantic"), semantic);
  obj.insert(QStringLiteral("indexers"), indexers);
  QSaveFile out(m_path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
  return out.commit();
}

void Config::setShowHidden(bool show) {
  if (m_showHidden == show)
    return;
  m_showHidden = show;
  emit showHiddenChanged();
}

void Config::setView(const QString &view) {
  const QString next = normalizeView(view);
  if (m_view == next)
    return;
  m_view = next;
  emit viewChanged();
}

void Config::setSortRole(const QString &role) {
  const QString next = normalizeSortRole(role);
  if (m_sortRole == next)
    return;
  m_sortRole = next;
  emit sortChanged();
}

void Config::setSortOrder(const QString &order) {
  const QString next = normalizeSortOrder(order);
  if (m_sortOrder == next)
    return;
  m_sortOrder = next;
  emit sortChanged();
}

QString Config::normalizePin(const QString &path) {
  QString t = path.trimmed();
  if (t.isEmpty())
    return {};
  if (t == QLatin1String("~"))
    t = QDir::homePath();
  else if (t.startsWith(QLatin1String("~/")))
    t = QDir::homePath() + t.mid(1);
  t = QDir::cleanPath(t);
  if (!QFileInfo(t).isAbsolute())
    return {};
  return t;
}

void Config::setPins(const QStringList &paths) {
  QStringList next;
  next.reserve(paths.size());
  for (const QString &raw : paths) {
    const QString pin = normalizePin(raw);
    if (!pin.isEmpty() && !next.contains(pin))
      next.append(pin);
  }
  if (m_pins == next)
    return;
  m_pins = next;
  emit pinsChanged();
}

QString Config::saveSqlBookmark(const QString &name, const QString &sql,
                                const QString &cwd) {
  QVariantMap next = normalizeSqlBookmark(
      {{QStringLiteral("name"), name},
       {QStringLiteral("sql"), sql},
       {QStringLiteral("cwd"), cwd}});
  if (next.isEmpty())
    return {};

  const QString wanted = next.value(QStringLiteral("name")).toString();
  for (int i = 0; i < m_sqlBookmarks.size(); ++i) {
    QVariantMap prior = m_sqlBookmarks.at(i).toMap();
    if (prior.value(QStringLiteral("name"))
            .toString()
            .compare(wanted, Qt::CaseInsensitive) != 0)
      continue;
    next.insert(QStringLiteral("id"),
                prior.value(QStringLiteral("id")).toString());
    if (prior == next)
      return next.value(QStringLiteral("id")).toString();
    m_sqlBookmarks[i] = next;
    emit sqlBookmarksChanged();
    return next.value(QStringLiteral("id")).toString();
  }
  if (m_sqlBookmarks.size() >= 24)
    return {};
  next.insert(QStringLiteral("id"),
              QUuid::createUuid().toString(QUuid::WithoutBraces));
  m_sqlBookmarks.append(next);
  emit sqlBookmarksChanged();
  return next.value(QStringLiteral("id")).toString();
}

bool Config::removeSqlBookmark(const QString &id) {
  const QString wanted = id.trimmed();
  for (int i = 0; i < m_sqlBookmarks.size(); ++i) {
    if (m_sqlBookmarks.at(i).toMap().value(QStringLiteral("id")).toString() !=
        wanted)
      continue;
    m_sqlBookmarks.removeAt(i);
    emit sqlBookmarksChanged();
    return true;
  }
  return false;
}

void Config::setLocationChips(const QStringList &ids) {
  const QStringList next = ids.isEmpty() ? defaultLocationChips() : ids;
  if (m_chips == next)
    return;
  m_chips = next;
  emit locationChipsChanged();
}

void Config::setLastPath(const QString &path) {
  const QString next = sanitizeLastPath(path);
  if (m_lastPath == next)
    return;
  m_lastPath = next;
  emit lastPathChanged();
}

void Config::setPanelSide(const QString &side) {
  const QString next = normalizePanelSide(side);
  if (m_panelSide == next)
    return;
  m_panelSide = next;
  emit panelChanged();
}

void Config::setPanelSize(int px) {
  const int next = qBound(120, px, 2000);
  if (m_panelSize == next)
    return;
  m_panelSize = next;
  emit panelChanged();
}

void Config::setPanelOpen(bool open) {
  if (m_panelOpen == open)
    return;
  m_panelOpen = open;
  emit panelChanged();
}

void Config::setPanelApp(const QString &id) {
  if (m_panelApp == id || id.isEmpty())
    return;
  m_panelApp = id;
  emit panelChanged();
}

void Config::setPanelLookOpen(bool open) {
  if (m_panelLookOpen == open)
    return;
  m_panelLookOpen = open;
  emit panelChanged();
}

void Config::setPanelLookRatio(double ratio) {
  const double next = qBound(0.2, ratio, 0.5);
  if (qFuzzyCompare(m_panelLookRatio, next))
    return;
  m_panelLookRatio = next;
  emit panelChanged();
}

void Config::setLookSize(int px) {
  const int next = qBound(240, px, 1200);
  if (m_lookSize == next)
    return;
  m_lookSize = next;
  emit panelChanged();
}

void Config::setLookSide(const QString &side) {
  const QString next = normalizeLookSide(side);
  if (m_lookSide == next)
    return;
  m_lookSide = next;
  emit panelChanged();
}

void Config::setGridSize(int px) {
  const int next = qBound(88, px, 240);
  if (m_gridSize == next)
    return;
  m_gridSize = next;
  emit gridSizeChanged();
}

QVariantMap Config::indexerSettings() const {
  return {{QStringLiteral("foregroundThumbnailWorkers"),
           m_foregroundThumbnailWorkers},
          {QStringLiteral("foregroundImageFacts"), m_foregroundImageFacts},
          {QStringLiteral("backgroundCatalogEnabled"),
           m_backgroundCatalogEnabled},
          {QStringLiteral("backgroundRecursiveScan"),
           m_backgroundRecursiveScan},
          {QStringLiteral("backgroundScanIntervalMinutes"),
           m_backgroundScanIntervalMinutes},
          {QStringLiteral("backgroundWatchDebounceMs"),
           m_backgroundWatchDebounceMs},
          {QStringLiteral("backgroundWatchNeighborhood"),
           m_backgroundWatchNeighborhood},
          {QStringLiteral("backgroundMaxWatches"), m_backgroundMaxWatches},
          {QStringLiteral("semanticImageEmbeddings"),
           m_semanticImageEmbeddings},
          {QStringLiteral("semanticBatchSize"), m_semanticBatchSize},
          {QStringLiteral("semanticIntervalSeconds"),
           m_semanticIntervalSeconds},
          {QStringLiteral("semanticImageModel"), semanticImageModel()}};
}

bool Config::applyIndexerSettings(const QVariantMap &settings) {
  if (!m_writable)
    return false;
  const int foregroundWorkers = qBound(
      1, settings.value(QStringLiteral("foregroundThumbnailWorkers"),
                        m_foregroundThumbnailWorkers)
             .toInt(),
      4);
  const bool foregroundFacts =
      settings.value(QStringLiteral("foregroundImageFacts"),
                     m_foregroundImageFacts)
          .toBool();
  const bool backgroundEnabled =
      settings.value(QStringLiteral("backgroundCatalogEnabled"),
                     m_backgroundCatalogEnabled)
          .toBool();
  const bool recursiveScan =
      settings.value(QStringLiteral("backgroundRecursiveScan"),
                     m_backgroundRecursiveScan)
          .toBool();
  const int scanMinutes = qBound(
      15, settings.value(QStringLiteral("backgroundScanIntervalMinutes"),
                         m_backgroundScanIntervalMinutes)
              .toInt(),
      7 * 24 * 60);
  const int debounce = qBound(
      25, settings.value(QStringLiteral("backgroundWatchDebounceMs"),
                         m_backgroundWatchDebounceMs)
              .toInt(),
      10000);
  const int neighborhood = qBound(
      0, settings.value(QStringLiteral("backgroundWatchNeighborhood"),
                        m_backgroundWatchNeighborhood)
             .toInt(),
      2048);
  const int maxWatches = qBound(
      16, settings.value(QStringLiteral("backgroundMaxWatches"),
                         m_backgroundMaxWatches)
              .toInt(),
      65536);
  const bool semanticImages =
      settings.value(QStringLiteral("semanticImageEmbeddings"),
                     m_semanticImageEmbeddings)
          .toBool();
  const int semanticBatch = qBound(
      1, settings.value(QStringLiteral("semanticBatchSize"),
                        m_semanticBatchSize)
             .toInt(),
      64);
  const int semanticInterval = qBound(
      15, settings.value(QStringLiteral("semanticIntervalSeconds"),
                         m_semanticIntervalSeconds)
              .toInt(),
      60 * 60);

  const QVariantMap before = indexerSettings();
  m_foregroundThumbnailWorkers = foregroundWorkers;
  m_foregroundImageFacts = foregroundFacts;
  m_backgroundCatalogEnabled = backgroundEnabled;
  m_backgroundRecursiveScan = recursiveScan;
  m_backgroundScanIntervalMinutes = scanMinutes;
  m_backgroundWatchDebounceMs = debounce;
  m_backgroundWatchNeighborhood = neighborhood;
  m_backgroundMaxWatches = maxWatches;
  m_semanticImageEmbeddings = semanticImages;
  m_semanticBatchSize = semanticBatch;
  m_semanticIntervalSeconds = semanticInterval;
  if (before != indexerSettings())
    emit indexersChanged();
  return save();
}

QVariantMap Config::indexerStatus() const {
  QVariantMap result;
  QProcess process;
  process.setProgram(QCoreApplication::applicationFilePath());
  process.setArguments({QStringLiteral("index"), QStringLiteral("status"),
                        QStringLiteral("--compact")});
  process.start();
  if (!process.waitForStarted(1000) || !process.waitForFinished(3000)) {
    process.kill();
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"),
                  QStringLiteral("index service did not answer"));
    return result;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
  if (!doc.isObject()) {
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("error"),
                  QString::fromUtf8(process.readAllStandardError()).trimmed());
    return result;
  }
  return doc.object().toVariantMap();
}

void Config::refreshIndexerStatus() {
  if (m_indexerStatusProcess &&
      m_indexerStatusProcess->state() != QProcess::NotRunning)
    return;
  if (!m_indexerStatusProcess) {
    m_indexerStatusProcess = new QProcess(this);
    connect(m_indexerStatusProcess,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
              const QJsonDocument doc = QJsonDocument::fromJson(
                  m_indexerStatusProcess->readAllStandardOutput());
              m_indexerStatus = doc.isObject()
                                    ? doc.object().toVariantMap()
                                    : QVariantMap{
                                          {QStringLiteral("ok"), false},
                                          {QStringLiteral("error"),
                                           QString::fromUtf8(
                                               m_indexerStatusProcess
                                                   ->readAllStandardError())
                                               .trimmed()}};
              m_indexerStatusLoading = false;
              emit indexerStatusChanged();
            });
  }
  m_indexerStatusLoading = true;
  emit indexerStatusChanged();
  m_indexerStatusProcess->start(
      QCoreApplication::applicationFilePath(),
      {QStringLiteral("index"), QStringLiteral("status"),
       QStringLiteral("--quick"), QStringLiteral("--compact")});
  QTimer::singleShot(3500, m_indexerStatusProcess, [this] {
    if (m_indexerStatusProcess &&
        m_indexerStatusProcess->state() != QProcess::NotRunning)
      m_indexerStatusProcess->kill();
  });
}
