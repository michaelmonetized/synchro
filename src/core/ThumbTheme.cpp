#include "ThumbTheme.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>

namespace {

const QColor kFg(QStringLiteral("#d7e9cb"));
const QColor kBg(QStringLiteral("#020000"));
const QColor kAccent(QStringLiteral("#55937c"));
const QColor kMuted(QStringLiteral("#707880"));

QMutex gThemeCacheMutex;
ThumbTheme gCachedTheme;
qint64 gCachedThemeMtime = -1;
QString gCachedThemePath;
bool gThemeCacheInitialized = false;

QString colorsPath() {
  return QDir::homePath() +
         QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

QColor withAlpha(const QColor &c, qreal a) {
  QColor o = c;
  o.setAlphaF(qBound(0.0, a, 1.0));
  return o;
}

ThumbTheme fallback() {
  ThumbTheme t;
  t.foreground = kFg;
  t.subtleForeground = kMuted;
  t.background = kBg;
  t.surfaceRaised = withAlpha(kFg, 0.09);
  t.muted = kMuted;
  t.accent = kAccent;
  t.border = withAlpha(kFg, 0.4);
  t.fontFamily = QStringLiteral("monospace");
  return t;
}

ThumbTheme parseColors(const QString &raw) {
  ThumbTheme t = fallback();
  bool gotFg = false;
  bool gotBg = false;
  bool gotAccent = false;
  bool gotMuted = false;
  QString color0, color4, color7, color8;
  static const QRegularExpression re(
      QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*[\"']?(#[0-9A-Fa-f]{6})"));
  const QStringList lines = raw.split(QLatin1Char('\n'));
  for (const QString &line : lines) {
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch())
      continue;
    const QString key = m.captured(1);
    const QColor val(m.captured(2));
    if (!val.isValid())
      continue;
    if (key == QLatin1String("foreground")) {
      t.foreground = val;
      gotFg = true;
    } else if (key == QLatin1String("background")) {
      t.background = val;
      gotBg = true;
    } else if (key == QLatin1String("dark_foreground")) {
      t.subtleForeground = val;
    } else if (key == QLatin1String("lighter_background")) {
      t.surfaceRaised = val;
    } else if (key == QLatin1String("accent")) {
      t.accent = val;
      gotAccent = true;
    } else if (key == QLatin1String("muted")) {
      t.muted = val;
      gotMuted = true;
    } else if (key == QLatin1String("color0")) {
      color0 = m.captured(2);
    } else if (key == QLatin1String("color4")) {
      color4 = m.captured(2);
    } else if (key == QLatin1String("color7")) {
      color7 = m.captured(2);
    } else if (key == QLatin1String("color8")) {
      color8 = m.captured(2);
    }
  }
  if (!gotBg && !color0.isEmpty())
    t.background = QColor(color0);
  if (!gotFg && !color7.isEmpty())
    t.foreground = QColor(color7);
  if (!gotAccent && !color4.isEmpty())
    t.accent = QColor(color4);
  if (!gotMuted)
    t.muted = color8.isEmpty() ? t.foreground : QColor(color8);
  if (!t.subtleForeground.isValid())
    t.subtleForeground = t.muted;
  if (!t.surfaceRaised.isValid())
    t.surfaceRaised = withAlpha(t.foreground, 0.09);
  t.border = withAlpha(t.foreground, 0.4);
  return t;
}

} // namespace

QString ThumbTheme::cacheId() const {
  const QByteArray raw =
      (background.name() + surfaceRaised.name() + foreground.name() +
       subtleForeground.name() + muted.name() + accent.name())
          .toLatin1();
  return QString::fromLatin1(
      QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex().left(8));
}

ThumbTheme ThumbTheme::current() {
  QMutexLocker lock(&gThemeCacheMutex);
  if (!gThemeCacheInitialized) {
    gCachedTheme = fallback();
    gThemeCacheInitialized = true;
  }
  const QString path = colorsPath();
  const qint64 mt = QFileInfo(path).lastModified().toMSecsSinceEpoch();
  if (gCachedThemePath == path && gCachedThemeMtime == mt && mt > 0)
    return gCachedTheme;
  gCachedThemePath = path;
  gCachedThemeMtime = mt;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    gCachedTheme = fallback();
    return gCachedTheme;
  }
  gCachedTheme = parseColors(QString::fromUtf8(f.readAll()));
  return gCachedTheme;
}

void ThumbTheme::invalidateCache() {
  QMutexLocker lock(&gThemeCacheMutex);
  gCachedThemePath.clear();
  gCachedThemeMtime = -1;
}
