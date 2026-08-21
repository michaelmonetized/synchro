#include "Config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

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
  m_lastPath.clear();
  m_panelSide = QStringLiteral("bottom");
  m_panelSize = 260;
  m_panelOpen = false;
  m_panelApp = QStringLiteral("synchro.panel.terminal");
}

static QString normalizePanelSide(const QString &side) {
  if (side == QLatin1String("left") || side == QLatin1String("right") ||
      side == QLatin1String("top"))
    return side;
  return QStringLiteral("bottom");
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
  obj.insert(QStringLiteral("lastPath"), m_lastPath);
  QJsonObject panel;
  panel.insert(QStringLiteral("side"), m_panelSide);
  panel.insert(QStringLiteral("size"), m_panelSize);
  panel.insert(QStringLiteral("open"), m_panelOpen);
  panel.insert(QStringLiteral("app"), m_panelApp);
  obj.insert(QStringLiteral("panel"), panel);
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
