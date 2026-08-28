#include "ThemeBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QProcess>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kDebounceMs = 50;
constexpr int kMaxMissingRetries = 40;

const QColor kFallbackForeground(QStringLiteral("#d7e9cb"));
const QColor kFallbackBackground(QStringLiteral("#020000"));
const QColor kFallbackAccent(QStringLiteral("#55937c"));
const QColor kFallbackUrgent(QStringLiteral("#a55555"));
const QColor kFallbackMuted(QStringLiteral("#707880"));

QString joinPath(const QString &a, const QString &b) {
  return QDir(a).filePath(b);
}

} // namespace

ThemeBridge::ThemeBridge(QObject *parent) : QObject(parent) {
  applyFallbackPalette();

  const QString home = QDir::homePath();
  m_currentDir = joinPath(joinPath(joinPath(home, QStringLiteral(".local")),
                                   QStringLiteral("state")),
                          QStringLiteral("omarchy/current"));
  m_themeDir = joinPath(m_currentDir, QStringLiteral("theme"));
  m_themeNamePath = joinPath(m_currentDir, QStringLiteral("theme.name"));
  m_colorsPath = joinPath(m_themeDir, QStringLiteral("colors.toml"));
  m_themeShellPath = joinPath(m_themeDir, QStringLiteral("shell.toml"));
  m_iconThemePath = joinPath(m_themeDir, QStringLiteral("icons.theme"));
  m_userConfigDir = joinPath(joinPath(home, QStringLiteral(".config")),
                             QStringLiteral("omarchy"));
  m_userShellPath = joinPath(m_userConfigDir, QStringLiteral("shell.toml"));

  m_reloadTimer.setSingleShot(true);
  m_reloadTimer.setInterval(kDebounceMs);
  connect(&m_reloadTimer, &QTimer::timeout, this, &ThemeBridge::reloadNow);

  setupWatchers();
  reloadNow();
  refreshHyprland();
}

int ThemeBridge::space(int px) const {
  if (px <= 0)
    return 0;
  const qreal fontScale = std::max(1.0 / 12.0, m_fontBaseSize / 12.0);
  const qreal scale =
      m_spacingScale * (m_spacingScaleWithFont ? fontScale : 1.0);
  const qreal n = static_cast<qreal>(px) * scale;
  if (n <= 0)
    return 0;
  return std::max(1, static_cast<int>(std::lround(n)));
}

void ThemeBridge::applyFallbackPalette() {
  m_foreground = kFallbackForeground;
  m_background = kFallbackBackground;
  m_accent = kFallbackAccent;
  m_urgent = kFallbackUrgent;
  m_muted = kFallbackMuted;
  m_sansFontFamily = QStringLiteral("sans-serif");
  m_monoFontFamily = QStringLiteral("monospace");
  m_fontBaseSize = 12;
  m_fontBodyOverride = 0;
  m_fontBody = 12;
  m_spacingScale = 1.0;
  m_spacingScaleWithFont = true;
  m_hoverFillAlpha = 0.08;
  m_selectedFillAlpha = 0.18;
  m_normalBorderAlpha = 0.4;
  m_hoverColorToken = QStringLiteral("foreground");
  m_selectedColorToken = QStringLiteral("foreground");
  m_normalColorToken = QStringLiteral("foreground");
  composeDerived();
  m_appliedAny = true;
}

void ThemeBridge::setupWatchers() {
  connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
          [this](const QString &) { scheduleReload(); });
  connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
          [this](const QString &) { scheduleReload(); });
  rearmWatches();
}

void ThemeBridge::rearmWatches() {
  const auto add = [this](const QString &path) {
    if (path.isEmpty() || !QFileInfo::exists(path))
      return;
    if (m_watcher.files().contains(path) ||
        m_watcher.directories().contains(path))
      return;
    m_watcher.addPath(path);
  };

  add(m_currentDir);
  add(m_themeDir);
  add(m_themeNamePath);
  add(m_colorsPath);
  add(m_themeShellPath);
  add(m_iconThemePath);
  add(m_userConfigDir);
  add(m_userShellPath);
}

void ThemeBridge::scheduleReload() {
  rearmWatches();
  m_reloadTimer.start();
}

void ThemeBridge::reloadNow() {
  rearmWatches();

  if (!bothThemeFilesReadable()) {
    if (!m_appliedAny)
      applyFallbackPalette();
    // Mid-swap: parent fired before mv finished. Retry until both exist.
    if (m_hadThemeFiles && m_missingRetries < kMaxMissingRetries) {
      ++m_missingRetries;
      m_reloadTimer.start();
    } else if (!m_hadThemeFiles) {
      loadUserShell();
      applyShellValues(m_userShell);
      composeDerived();
      rebuildTokens(m_userShell);
      publish();
    }
    return;
  }

  m_missingRetries = 0;
  if (!loadThemeFiles()) {
    if (m_missingRetries < kMaxMissingRetries) {
      ++m_missingRetries;
      m_reloadTimer.start();
    }
    return;
  }

  m_hadThemeFiles = true;
  loadUserShell();

  QHash<QString, QString> merged = m_themeShell;
  for (auto it = m_userShell.cbegin(); it != m_userShell.cend(); ++it)
    merged.insert(it.key(), it.value());
  applyShellValues(merged);
  composeDerived();
  loadIconTheme();
  rebuildTokens(merged);
  publish();
  refreshHyprland();
}

bool ThemeBridge::bothThemeFilesReadable() const {
  return fileReadable(m_colorsPath) && fileReadable(m_themeShellPath);
}

bool ThemeBridge::loadThemeFiles() {
  bool colorsOk = false;
  bool shellOk = false;
  const QString colors = readUtf8(m_colorsPath, &colorsOk);
  const QString shell = readUtf8(m_themeShellPath, &shellOk);
  if (!colorsOk || !shellOk)
    return false;
  loadColors(colors);
  m_themeShell = parseShell(shell);
  return true;
}

void ThemeBridge::loadUserShell() {
  if (!fileReadable(m_userShellPath)) {
    m_userShell.clear();
    return;
  }
  bool ok = false;
  const QString raw = readUtf8(m_userShellPath, &ok);
  if (!ok)
    return;
  m_userShell = parseShell(raw);
}

void ThemeBridge::loadColors(const QString &raw) {
  QColor foreground = kFallbackForeground;
  QColor background = kFallbackBackground;
  QColor accent = kFallbackAccent;
  QColor urgent = kFallbackUrgent;
  QColor muted = kFallbackMuted;

  bool foundAccent = false;
  bool foundMuted = false;
  bool loadedForeground = false;
  bool loadedBackground = false;
  QString color0;
  QString color4;
  QString color7;
  QString color8;
  m_colorTokens.clear();

  static const QRegularExpression re(
      QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*[\"']?(#[0-9A-Fa-f]{6})"));

  const QStringList lines = raw.split(QLatin1Char('\n'));
  for (const QString &line : lines) {
    const QRegularExpressionMatch match = re.match(line);
    if (!match.hasMatch())
      continue;
    const QString key = match.captured(1);
    const QString val = match.captured(2);
    const QColor parsed(val);
    if (parsed.isValid())
      m_colorTokens.insert(key, parsed);
    if (key == QLatin1String("foreground")) {
      foreground = QColor(val);
      loadedForeground = true;
    } else if (key == QLatin1String("background")) {
      background = QColor(val);
      loadedBackground = true;
    } else if (key == QLatin1String("accent")) {
      accent = QColor(val);
      foundAccent = true;
    } else if (key == QLatin1String("muted")) {
      muted = QColor(val);
      foundMuted = true;
    } else if (key == QLatin1String("color0")) {
      color0 = val;
    } else if (key == QLatin1String("color4")) {
      color4 = val;
    } else if (key == QLatin1String("color7")) {
      color7 = val;
    } else if (key == QLatin1String("color8")) {
      color8 = val;
    } else if (key == QLatin1String("red") || key == QLatin1String("color1")) {
      urgent = QColor(val);
    }
  }

  if (!loadedBackground && !color0.isEmpty())
    background = QColor(color0);
  if (!loadedForeground && !color7.isEmpty())
    foreground = QColor(color7);
  if (!foundAccent && !color4.isEmpty())
    accent = QColor(color4);
  if (!foundMuted)
    muted = color8.isEmpty() ? foreground : QColor(color8);

  m_foreground = foreground;
  m_background = background;
  m_accent = accent;
  m_urgent = urgent;
  m_muted = muted;
}

void ThemeBridge::loadIconTheme() {
  bool ok = false;
  const QString next = readUtf8(m_iconThemePath, &ok).trimmed();
  if (!ok || next.isEmpty())
    return;
  m_iconTheme = next;
  QIcon::setThemeName(next);
}

void ThemeBridge::rebuildTokens(const QHash<QString, QString> &values) {
  QVariantMap out;
  for (auto it = m_colorTokens.cbegin(); it != m_colorTokens.cend(); ++it)
    out.insert(QStringLiteral("colors.") + it.key(), it.value());
  for (auto it = values.cbegin(); it != values.cend(); ++it)
    out.insert(it.key(), it.value());
  out.insert(QStringLiteral("theme.icon-name"), m_iconTheme);
  m_tokens = out;
}

QHash<QString, QString> ThemeBridge::parseShell(const QString &raw) const {
  QHash<QString, QString> parsed;
  if (raw.isEmpty())
    return parsed;

  static const QRegularExpression sectionRe(
      QStringLiteral("^\\[([A-Za-z0-9_-]+)\\]\\s*(#.*)?$"));
  static const QRegularExpression stringKv(QStringLiteral(
      "^([A-Za-z0-9_-]+)\\s*=\\s*[\"']([^\"']+)[\"']\\s*(#.*)?$"));
  static const QRegularExpression numKv(QStringLiteral(
      "^([A-Za-z0-9_-]+)\\s*=\\s*(-?\\d+(?:\\.\\d+)?)\\s*(#.*)?$"));
  static const QRegularExpression widthKv(
      QStringLiteral("^([A-Za-z0-9_-]+)\\s*=\\s*(-?\\d+(?:\\.\\d+)?(?:\\s+-?"
                     "\\d+(?:\\.\\d+)?){1,3})\\s*(#.*)?$"));
  static const QRegularExpression bareKv(QStringLiteral(
      "^([A-Za-z0-9_-]+)\\s*=\\s*([A-Za-z][A-Za-z0-9_-]*)\\s*(#.*)?$"));

  QString section;
  const QStringList lines = raw.split(QLatin1Char('\n'));
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
      continue;
    const QRegularExpressionMatch sectionMatch = sectionRe.match(line);
    if (sectionMatch.hasMatch()) {
      section = sectionMatch.captured(1);
      continue;
    }
    QRegularExpressionMatch kv = stringKv.match(line);
    if (!kv.hasMatch())
      kv = numKv.match(line);
    if (!kv.hasMatch())
      kv = widthKv.match(line);
    if (!kv.hasMatch())
      kv = bareKv.match(line);
    if (!kv.hasMatch() || section.isEmpty())
      continue;
    parsed.insert(section + QLatin1Char('.') + kv.captured(1), kv.captured(2));
  }
  return parsed;
}

void ThemeBridge::applyShellValues(const QHash<QString, QString> &values) {
  int nextBase = 12;
  int bodyOverride = 0;
  qreal nextSpacingScale = 1.0;
  bool nextSpacingScaleWithFont = true;
  qreal hoverFillAlpha = 0.08;
  qreal selectedFillAlpha = 0.18;
  qreal normalBorderAlpha = 0.4;
  QString hoverColor = QStringLiteral("foreground");
  QString selectedColor = QStringLiteral("foreground");
  QString normalColor = QStringLiteral("foreground");

  for (auto it = values.cbegin(); it != values.cend(); ++it) {
    const int dot = it.key().indexOf(QLatin1Char('.'));
    if (dot < 0)
      continue;
    const QString section = it.key().left(dot);
    const QString key = it.key().mid(dot + 1);
    const QString raw = it.value();

    if (section == QLatin1String("font")) {
      bool ok = false;
      const int ival = raw.toInt(&ok);
      if (!ok)
        continue;
      if (key == QLatin1String("base-size"))
        nextBase = ival;
      else if (key == QLatin1String("body"))
        bodyOverride = ival;
    } else if (section == QLatin1String("spacing")) {
      if (key == QLatin1String("scale-with-font")) {
        nextSpacingScaleWithFont =
            parseBoolToken(raw, nextSpacingScaleWithFont);
      } else if (key == QLatin1String("scale")) {
        bool ok = false;
        const qreal fval = raw.toDouble(&ok);
        if (ok)
          nextSpacingScale = fval;
      }
    } else if (section == QLatin1String("controls") ||
               section == QLatin1String("style")) {
      if (key == QLatin1String("hover-cursor-fill-alpha")) {
        bool ok = false;
        const qreal v = raw.toDouble(&ok);
        if (ok)
          hoverFillAlpha = v;
      } else if (key == QLatin1String("selected-fill-alpha")) {
        bool ok = false;
        const qreal v = raw.toDouble(&ok);
        if (ok)
          selectedFillAlpha = v;
      } else if (key == QLatin1String("normal-border-alpha")) {
        bool ok = false;
        const qreal v = raw.toDouble(&ok);
        if (ok)
          normalBorderAlpha = v;
      } else if (key == QLatin1String("hover-cursor-color")) {
        hoverColor = raw;
      } else if (key == QLatin1String("selected-color")) {
        selectedColor = raw;
      } else if (key == QLatin1String("normal-color")) {
        normalColor = raw;
      }
    }
  }

  if (nextBase < 1)
    nextBase = 1;
  if (!std::isfinite(nextSpacingScale) || nextSpacingScale < 0)
    nextSpacingScale = 1.0;

  m_fontBaseSize = nextBase;
  m_fontBodyOverride = bodyOverride > 0 ? bodyOverride : 0;
  m_fontBody =
      m_fontBodyOverride > 0 ? m_fontBodyOverride : std::max(1, nextBase);
  m_spacingScale = nextSpacingScale;
  m_spacingScaleWithFont = nextSpacingScaleWithFont;
  m_hoverFillAlpha = std::clamp(hoverFillAlpha, 0.0, 1.0);
  m_selectedFillAlpha = std::clamp(selectedFillAlpha, 0.0, 1.0);
  m_normalBorderAlpha = std::clamp(normalBorderAlpha, 0.0, 1.0);
  m_hoverColorToken = hoverColor;
  m_selectedColorToken = selectedColor;
  m_normalColorToken = normalColor;
}

void ThemeBridge::composeDerived() {
  const QColor hoverBase = resolveColorToken(m_hoverColorToken, m_foreground);
  const QColor selectedBase =
      resolveColorToken(m_selectedColorToken, m_foreground);
  const QColor normalBase = resolveColorToken(m_normalColorToken, m_foreground);
  m_hoverFill = withAlpha(hoverBase, m_hoverFillAlpha);
  m_selectedFill = withAlpha(selectedBase, m_selectedFillAlpha);
  m_normalBorder = withAlpha(normalBase, m_normalBorderAlpha);
}

void ThemeBridge::publish() {
  ++m_epoch;
  emit themeChanged();
}

void ThemeBridge::refreshHyprland() {
  runHyprctl({QStringLiteral("-j"), QStringLiteral("getoption"),
              QStringLiteral("decoration:rounding")},
             &ThemeBridge::applyRoundingOutput);
  runHyprctl({QStringLiteral("-j"), QStringLiteral("getoption"),
              QStringLiteral("general:gaps_out")},
             &ThemeBridge::applyGapsOutOutput);
}

void ThemeBridge::runHyprctl(const QStringList &args,
                             void (ThemeBridge::*slot)(const QByteArray &)) {
  auto *proc = new QProcess(this);
  connect(proc, &QProcess::finished, this,
          [this, proc, slot](int, QProcess::ExitStatus) {
            (this->*slot)(proc->readAllStandardOutput());
            proc->deleteLater();
          });
  connect(proc, &QProcess::errorOccurred, this,
          [proc](QProcess::ProcessError) { proc->deleteLater(); });
  proc->start(QStringLiteral("hyprctl"), args);
}

void ThemeBridge::applyRoundingOutput(const QByteArray &raw) {
  int n = -1;
  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  if (doc.isObject()) {
    const QJsonValue v = doc.object().value(QLatin1String("int"));
    if (v.isDouble())
      n = v.toInt();
  }
  if (n < 0) {
    static const QRegularExpression re(QStringLiteral("int:\\s*(-?\\d+)"));
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(raw));
    if (m.hasMatch())
      n = m.captured(1).toInt();
  }
  if (n < 0)
    return;
  if (m_radius == n)
    return;
  m_radius = n;
  emit themeChanged();
}

void ThemeBridge::applyGapsOutOutput(const QByteArray &raw) {
  int n = -1;
  const QJsonDocument doc = QJsonDocument::fromJson(raw);
  if (doc.isObject()) {
    const QJsonObject obj = doc.object();
    const QString css = obj.value(QLatin1String("css")).toString();
    static const QRegularExpression numRe(QStringLiteral("-?\\d+(?:\\.\\d+)?"));
    const QRegularExpressionMatch m = numRe.match(css);
    if (m.hasMatch())
      n = static_cast<int>(std::lround(m.captured().toDouble()));
    else if (obj.value(QLatin1String("int")).isDouble())
      n = obj.value(QLatin1String("int")).toInt();
  }
  if (n < 0) {
    static const QRegularExpression re(QStringLiteral("int:\\s*(-?\\d+)"));
    const QRegularExpressionMatch m = re.match(QString::fromUtf8(raw));
    if (m.hasMatch())
      n = m.captured(1).toInt();
  }
  if (n < 0)
    return;
  const int gaps = std::max(0, static_cast<int>(std::lround(n / 2.0)));
  if (m_gapsOut == gaps)
    return;
  m_gapsOut = gaps;
  emit themeChanged();
}

QColor ThemeBridge::resolveColorToken(const QString &token,
                                      const QColor &fallback) const {
  const QString s = token.trimmed();
  if (s.isEmpty())
    return fallback;
  const QString role = s.toLower();
  if (role == QLatin1String("foreground") || role == QLatin1String("text"))
    return m_foreground;
  if (role == QLatin1String("accent"))
    return m_accent;
  if (role == QLatin1String("urgent"))
    return m_urgent;
  if (role == QLatin1String("muted"))
    return m_muted;
  if (role == QLatin1String("background"))
    return m_background;
  if (role == QLatin1String("transparent"))
    return QColor(0, 0, 0, 0);
  if (s.startsWith(QLatin1Char('#'))) {
    const QColor c(s);
    if (c.isValid())
      return c;
  }
  return fallback;
}

QColor ThemeBridge::withAlpha(const QColor &c, qreal alpha) {
  QColor out = c.isValid() ? c : QColor(0, 0, 0);
  out.setAlphaF(std::clamp(alpha, 0.0, 1.0));
  return out;
}

bool ThemeBridge::fileReadable(const QString &path) {
  QFile f(path);
  return f.exists() && f.open(QIODevice::ReadOnly);
}

QString ThemeBridge::readUtf8(const QString &path, bool *ok) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (ok)
      *ok = false;
    return {};
  }
  if (ok)
    *ok = true;
  return QString::fromUtf8(f.readAll());
}

bool ThemeBridge::parseBoolToken(const QString &value, bool fallback) {
  const QString s = value.trimmed().toLower();
  if (s == QLatin1String("true") || s == QLatin1String("1") ||
      s == QLatin1String("yes") || s == QLatin1String("on"))
    return true;
  if (s == QLatin1String("false") || s == QLatin1String("0") ||
      s == QLatin1String("no") || s == QLatin1String("off"))
    return false;
  return fallback;
}
