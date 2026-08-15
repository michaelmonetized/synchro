#include "MimeMap.h"

#include <QFileInfo>
#include <QIcon>
#include <QMimeType>

QString MimeMap::mimeForFile(const QString &path) const {
  const QFileInfo fi(path);
  if (fi.isDir())
    return QStringLiteral("inode/directory");
  const QMimeDatabase::MatchMode mode =
      fi.suffix().isEmpty() ? QMimeDatabase::MatchContent
                            : QMimeDatabase::MatchExtension;
  return m_db.mimeTypeForFile(path, mode).name();
}

QString MimeMap::iconNameForMime(const QString &mimeName) const {
  if (mimeName == QLatin1String("inode/directory"))
    return QStringLiteral("folder");
  const QMimeType mime = m_db.mimeTypeForName(mimeName);
  QString icon = mime.genericIconName();
  if (icon.isEmpty())
    icon = mime.iconName();
  if (icon.isEmpty())
    icon = QStringLiteral("application-x-executable");
  return icon;
}

QString MimeMap::iconNameForFile(const QString &path) const {
  return iconNameForMime(mimeForFile(path));
}

QString MimeMap::resolveIcon(const QString &iconName) const {
  if (!iconName.isEmpty() && !QIcon::fromTheme(iconName).isNull())
    return iconName;
  const QString fallback = QStringLiteral("application-x-executable");
  if (!QIcon::fromTheme(fallback).isNull())
    return fallback;
  return iconName.isEmpty() ? fallback : iconName;
}
