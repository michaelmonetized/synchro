#include "LocationChips.h"

#include "Config.h"
#include "DirectoryModel.h"
#include "HandlerRegistry.h"
#include "Manifest.h"
#include "NavStack.h"
#include "VolumeStore.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString envOr(const QString &name, const QString &fallback) {
  const QString v = QProcessEnvironment::systemEnvironment().value(name);
  return v.isEmpty() ? fallback : v;
}

QString xdgUserDir(QStandardPaths::StandardLocation loc, const QString &env) {
  const QString fromEnv = QProcessEnvironment::systemEnvironment().value(env);
  if (!fromEnv.isEmpty())
    return fromEnv;
  const QStringList dirs = QStandardPaths::standardLocations(loc);
  return dirs.isEmpty() ? QDir::homePath() : dirs.first();
}

} // namespace

LocationChips::LocationChips(QObject *parent) : QObject(parent) {}

void LocationChips::setRegistry(HandlerRegistry *registry) {
  m_registry = registry;
  rebuild();
}

void LocationChips::setConfig(Config *config) {
  if (m_config == config)
    return;
  if (m_config)
    disconnect(m_config, nullptr, this, nullptr);
  m_config = config;
  if (m_config) {
    connect(m_config, &Config::locationChipsChanged, this,
            &LocationChips::rebuild);
    connect(m_config, &Config::pinsChanged, this, &LocationChips::rebuild);
    connect(m_config, &Config::sqlBookmarksChanged, this,
            &LocationChips::rebuild);
  }
  rebuild();
}

void LocationChips::setNav(NavStack *nav) { m_nav = nav; }

void LocationChips::setDirectoryModel(DirectoryModel *model) {
  if (m_model == model)
    return;
  if (m_model)
    disconnect(m_model, nullptr, this, nullptr);
  m_model = model;
  if (m_model)
    connect(m_model, &DirectoryModel::pathChanged, this,
            &LocationChips::rebuild);
  connect(&VolumeStore::instance(), &VolumeStore::changed, this,
          &LocationChips::rebuild);
  rebuild();
}

void LocationChips::setChooserMode(bool on) {
  if (m_chooserMode == on)
    return;
  m_chooserMode = on;
  emit chooserModeChanged();
  rebuild();
}

void LocationChips::refresh() { rebuild(); }

QVariantList LocationChips::chipsInGroup(const QString &group) const {
  QVariantList out;
  for (const QVariant &row : m_chips) {
    if (row.toMap().value(QStringLiteral("group")).toString() == group)
      out.append(row);
  }
  return out;
}

QVariantList LocationChips::placeChips() const {
  return chipsInGroup(QStringLiteral("place"));
}

QVariantList LocationChips::diskChips() const {
  return chipsInGroup(QStringLiteral("disk"));
}

QVariantMap LocationChips::volumesChip() const {
  for (const QVariant &row : m_chips) {
    const QVariantMap chip = row.toMap();
    if (chip.value(QStringLiteral("group")).toString() ==
        QLatin1String("volumes"))
      return chip;
  }
  return {};
}

QString LocationChips::expandPath(const QString &path) {
  QString out = path.trimmed();
  if (out.isEmpty())
    return {};
  static const QRegularExpression re(
      QStringLiteral(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\}|\$([A-Za-z_][A-Za-z0-9_]*))"));
  int guard = 0;
  QRegularExpressionMatch m;
  while (guard++ < 16 && (m = re.match(out)).hasMatch()) {
    const QString name =
        m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
    QString value;
    if (name == QLatin1String("HOME"))
      value = QDir::homePath();
    else if (name == QLatin1String("XDG_PICTURES_DIR"))
      value = xdgUserDir(QStandardPaths::PicturesLocation, name);
    else if (name == QLatin1String("XDG_VIDEOS_DIR"))
      value = xdgUserDir(QStandardPaths::MoviesLocation, name);
    else if (name == QLatin1String("XDG_DOWNLOAD_DIR"))
      value = xdgUserDir(QStandardPaths::DownloadLocation, name);
    else if (name == QLatin1String("XDG_DOCUMENTS_DIR"))
      value = xdgUserDir(QStandardPaths::DocumentsLocation, name);
    else if (name == QLatin1String("XDG_MUSIC_DIR"))
      value = xdgUserDir(QStandardPaths::MusicLocation, name);
    else if (name == QLatin1String("XDG_DESKTOP_DIR"))
      value = xdgUserDir(QStandardPaths::DesktopLocation, name);
    else
      value = envOr(name, QString());
    out.replace(m.capturedStart(), m.capturedLength(), value);
  }
  if (out == QLatin1String("~"))
    return QDir::homePath();
  if (out.startsWith(QLatin1String("~/")))
    return QDir::homePath() + out.mid(1);
  return out;
}

bool LocationChips::allowedInChooser(const QString &adapter,
                                     const QString &runtime) {
  if (runtime == QLatin1String("path"))
    return true;
  if (runtime == QLatin1String("core") &&
      (adapter == QLatin1String("recent") ||
       adapter == QLatin1String("volumes")))
    return true;
  return false;
}

QString LocationChips::pinId(const QString &path) {
  const QString abs = Config::normalizePin(path);
  if (abs.isEmpty())
    return {};
  return QStringLiteral("pin:") + abs;
}

bool LocationChips::isPinId(const QString &id) {
  return id.startsWith(QLatin1String("pin:"));
}

QString LocationChips::sqlBookmarkId(const QString &id) {
  const QString clean = id.trimmed();
  return clean.isEmpty() ? QString()
                         : QStringLiteral("sql-bookmark:") + clean;
}

bool LocationChips::isSqlBookmarkId(const QString &id) {
  return id.startsWith(QLatin1String("sql-bookmark:"));
}

QVariantMap LocationChips::pinChipMap(const QString &path) const {
  const QString abs = Config::normalizePin(path);
  QVariantMap out;
  if (abs.isEmpty())
    return out;
  if (m_chooserMode && DirectoryModel::isVirtualPath(abs))
    return out;
  const QFileInfo fi(abs);
  const QString name = fi.fileName().isEmpty() ? abs : fi.fileName();
  out.insert(QStringLiteral("id"), pinId(abs));
  out.insert(QStringLiteral("name"), name);
  out.insert(QStringLiteral("label"), name);
  out.insert(QStringLiteral("runtime"), QStringLiteral("path"));
  out.insert(QStringLiteral("path"), abs);
  out.insert(QStringLiteral("pinned"), true);
  out.insert(QStringLiteral("group"), QStringLiteral("place"));
  out.insert(QStringLiteral("active"), chipActive(out));
  return out;
}

QVariantMap LocationChips::sqlBookmarkChipMap(const QString &id) const {
  QVariantMap out;
  if (!m_config || m_chooserMode)
    return out;
  const QString wanted = isSqlBookmarkId(id) ? id.mid(13) : id;
  for (const QVariant &value : m_config->sqlBookmarks()) {
    const QVariantMap bookmark = value.toMap();
    if (bookmark.value(QStringLiteral("id")).toString() != wanted)
      continue;
    const QString name = bookmark.value(QStringLiteral("name")).toString();
    out.insert(QStringLiteral("id"), sqlBookmarkId(wanted));
    out.insert(QStringLiteral("name"), name);
    out.insert(QStringLiteral("label"), name);
    out.insert(QStringLiteral("runtime"), QStringLiteral("sql"));
    out.insert(QStringLiteral("sql"),
               bookmark.value(QStringLiteral("sql")));
    out.insert(QStringLiteral("cwd"),
               bookmark.value(QStringLiteral("cwd")));
    out.insert(QStringLiteral("bookmarkId"), wanted);
    out.insert(QStringLiteral("pinned"), true);
    out.insert(QStringLiteral("group"), QStringLiteral("place"));
    out.insert(QStringLiteral("active"), chipActive(out));
    return out;
  }
  return out;
}

bool LocationChips::isPinned(const QString &path) const {
  if (!m_config)
    return false;
  const QString abs = Config::normalizePin(path);
  return !abs.isEmpty() && m_config->pins().contains(abs);
}

void LocationChips::persistPins() {
  if (m_config && m_config->writable())
    m_config->save();
}

bool LocationChips::pin(const QString &path) {
  if (!m_config)
    return false;
  const QString abs = Config::normalizePin(path);
  if (abs.isEmpty() || DirectoryModel::isVirtualPath(abs))
    return false;
  if (!QFileInfo(abs).isDir())
    return false;
  QStringList pins = m_config->pins();
  if (pins.contains(abs))
    return true;
  if (pins.size() >= 16)
    return false;
  pins.append(abs);
  m_config->setPins(pins);
  persistPins();
  return true;
}

bool LocationChips::unpin(const QString &path) {
  if (!m_config)
    return false;
  const QString abs = Config::normalizePin(path);
  if (abs.isEmpty())
    return false;
  QStringList pins = m_config->pins();
  if (!pins.removeOne(abs))
    return false;
  m_config->setPins(pins);
  persistPins();
  return true;
}

bool LocationChips::removeSqlBookmark(const QString &id) {
  if (!m_config)
    return false;
  const QString bookmarkId = isSqlBookmarkId(id) ? id.mid(13) : id;
  return m_config->removeSqlBookmark(bookmarkId);
}

QVariantMap LocationChips::chipMap(const QString &id) const {
  if (isPinId(id))
    return pinChipMap(id.mid(4));
  if (isSqlBookmarkId(id))
    return sqlBookmarkChipMap(id);
  if (id.startsWith(QLatin1String("volume:"))) {
    const QString mount = id.mid(7);
    const auto v = VolumeStore::instance().findMount(mount);
    if (v.mountPoint.isEmpty())
      return {};
    QVariantMap out;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("name"), v.label);
    out.insert(QStringLiteral("label"), VolumeStore::chipLabel(v));
    out.insert(QStringLiteral("runtime"), QStringLiteral("path"));
    out.insert(QStringLiteral("path"), v.mountPoint);
    out.insert(QStringLiteral("group"), QStringLiteral("disk"));
    out.insert(QStringLiteral("active"), chipActive(out));
    return out;
  }
  QVariantMap out;
  if (!m_registry || id.isEmpty())
    return out;
  const HandlerRegistry::Record rec = m_registry->handler(id);
  if (!rec.enabled || !rec.manifest.hasKind(QStringLiteral("location")))
    return out;
  const Manifest &m = rec.manifest;
  const QString runtime = m.runtime(QStringLiteral("location"));
  const QString adapter = m.coreVerb(QStringLiteral("location"));
  if (m_chooserMode && !allowedInChooser(adapter, runtime))
    return out;
  out.insert(QStringLiteral("id"), m.id);
  out.insert(QStringLiteral("name"), m.name);
  QString label = m.name.toLower();
  if (label.endsWith(QLatin1Char('s')) && adapter == QLatin1String("recent"))
    label = QStringLiteral("recent");
  out.insert(QStringLiteral("label"), label);
  out.insert(QStringLiteral("runtime"), runtime);
  out.insert(QStringLiteral("adapter"), adapter);
  if (runtime == QLatin1String("path"))
    out.insert(QStringLiteral("path"),
               expandPath(m.location.value(QStringLiteral("path")).toString()));
  else if (adapter == QLatin1String("trash"))
    out.insert(QStringLiteral("path"), QStringLiteral("trash://"));
  else if (adapter == QLatin1String("recent"))
    out.insert(QStringLiteral("path"), QStringLiteral("recent://"));
  else if (adapter == QLatin1String("search"))
    out.insert(QStringLiteral("path"), QStringLiteral("search://"));
  else if (adapter == QLatin1String("volumes"))
    out.insert(QStringLiteral("path"), QStringLiteral("volumes://"));
  out.insert(QStringLiteral("group"),
             adapter == QLatin1String("volumes") ? QStringLiteral("volumes")
                                                 : QStringLiteral("place"));
  out.insert(QStringLiteral("active"), chipActive(out));
  return out;
}

bool LocationChips::chipActive(const QVariantMap &chip) const {
  if (!m_model)
    return false;
  const QString cwd = m_model->path();
  const QString id = chip.value(QStringLiteral("id")).toString();
  const QString target = chip.value(QStringLiteral("path")).toString();
  if (isSqlBookmarkId(id))
    return m_model->isSql() &&
           m_model->sqlLabel() == chip.value(QStringLiteral("name")).toString();
  if (target.isEmpty())
    return false;
  if (id.startsWith(QLatin1String("volume:"))) {
    const QString mount = id.mid(7);
    if (mount == QLatin1String("/")) {
      if (DirectoryModel::isVirtualPath(cwd))
        return false;
      return !VolumeStore::instance().extraRoot(cwd).extra;
    }
    return cwd == mount || cwd.startsWith(mount + QLatin1Char('/'));
  }
  if (target.startsWith(QLatin1String("volumes:")))
    return cwd.startsWith(QLatin1String("volumes:"));
  if (DirectoryModel::isVirtualPath(target))
    return cwd.startsWith(target.left(target.indexOf(QLatin1Char(':')) + 1));
  const QString a = QFileInfo(cwd).canonicalFilePath();
  const QString b = QFileInfo(target).canonicalFilePath();
  if (!a.isEmpty() && !b.isEmpty())
    return a == b;
  return cwd == target;
}

void LocationChips::rebuild() {
  QVariantList next;
  QStringList ids = m_config ? m_config->locationChips()
                             : Config::defaultLocationChips();
  if (ids.isEmpty())
    ids = Config::defaultLocationChips();
  bool injectedSavedLocations = false;
  auto appendSavedLocations = [&] {
    if (injectedSavedLocations || !m_config)
      return;
    injectedSavedLocations = true;
    for (const QString &path : m_config->pins()) {
      const QVariantMap pin = pinChipMap(path);
      if (!pin.isEmpty())
        next.append(pin);
    }
    if (!m_chooserMode) {
      for (const QVariant &value : m_config->sqlBookmarks()) {
        const QVariantMap bookmark = value.toMap();
        const QVariantMap chip = sqlBookmarkChipMap(
            bookmark.value(QStringLiteral("id")).toString());
        if (!chip.isEmpty())
          next.append(chip);
      }
    }
  };
  for (const QString &id : ids) {
    const QVariantMap chip = chipMap(id);
    if (!chip.isEmpty())
      next.append(chip);
    if (id == QLatin1String("synchro.location.home"))
      appendSavedLocations();
  }
  appendSavedLocations();
  bool hasVolumes = false;
  for (const QVariant &row : next) {
    if (row.toMap().value(QStringLiteral("id")).toString() ==
        QLatin1String("synchro.location.volumes")) {
      hasVolumes = true;
      break;
    }
  }
  if (!hasVolumes) {
    const QVariantMap vol = chipMap(QStringLiteral("synchro.location.volumes"));
    if (!vol.isEmpty())
      next.append(vol);
  }
  const QStringList pins = m_config ? m_config->pins() : QStringList();
  for (const QVariant &row : VolumeStore::instance().extraChips()) {
    QVariantMap chip = row.toMap();
    const QString path = chip.value(QStringLiteral("path")).toString();
    if (pins.contains(Config::normalizePin(path)))
      continue;
    chip.insert(QStringLiteral("group"), QStringLiteral("disk"));
    chip.insert(QStringLiteral("active"), chipActive(chip));
    next.append(chip);
  }
  if (next == m_chips)
    return;
  m_chips = next;
  emit chipsChanged();
}

void LocationChips::activate(const QString &id) {
  QVariantMap chip = chipMap(id);
  if (chip.isEmpty())
    return;
  if (isSqlBookmarkId(id)) {
    emit sqlBookmarkActivated(
        chip.value(QStringLiteral("name")).toString(),
        chip.value(QStringLiteral("sql")).toString(),
        chip.value(QStringLiteral("cwd")).toString(),
        chip.value(QStringLiteral("bookmarkId")).toString());
    return;
  }
  const QString dest = chip.value(QStringLiteral("path")).toString();
  if (dest.isEmpty())
    return;
  if (m_nav)
    m_nav->navigate(dest);
  else if (m_model)
    m_model->setPath(dest);
}
