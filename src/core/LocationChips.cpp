#include "LocationChips.h"

#include "Config.h"
#include "DirectoryModel.h"
#include "HandlerRegistry.h"
#include "Manifest.h"
#include "NavStack.h"

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
  if (runtime == QLatin1String("core") && adapter == QLatin1String("recent"))
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
  out.insert(QStringLiteral("active"), chipActive(out));
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

QVariantMap LocationChips::chipMap(const QString &id) const {
  if (isPinId(id))
    return pinChipMap(id.mid(4));
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
  out.insert(QStringLiteral("active"), chipActive(out));
  return out;
}

bool LocationChips::chipActive(const QVariantMap &chip) const {
  if (!m_model)
    return false;
  const QString cwd = m_model->path();
  const QString target = chip.value(QStringLiteral("path")).toString();
  if (target.isEmpty())
    return false;
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
  bool injectedPins = false;
  auto appendPins = [&] {
    if (injectedPins || !m_config)
      return;
    injectedPins = true;
    for (const QString &path : m_config->pins()) {
      const QVariantMap pin = pinChipMap(path);
      if (!pin.isEmpty())
        next.append(pin);
    }
  };
  for (const QString &id : ids) {
    const QVariantMap chip = chipMap(id);
    if (!chip.isEmpty())
      next.append(chip);
    if (id == QLatin1String("synchro.location.home"))
      appendPins();
  }
  appendPins();
  if (next == m_chips)
    return;
  m_chips = next;
  emit chipsChanged();
}

void LocationChips::activate(const QString &id) {
  QVariantMap chip = chipMap(id);
  if (chip.isEmpty())
    return;
  const QString dest = chip.value(QStringLiteral("path")).toString();
  if (dest.isEmpty())
    return;
  if (m_nav)
    m_nav->navigate(dest);
  else if (m_model)
    m_model->setPath(dest);
}
