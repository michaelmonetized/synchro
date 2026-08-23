#include "IconImageProvider.h"

#include <QIcon>
#include <QQmlEngine>
#include <QStringList>

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

QPixmap IconImageProvider::requestPixmap(const QString &id, QSize *size,
                                         const QSize &requestedSize) {
  const QString clean = id.section(QLatin1Char('?'), 0, 0);
  const QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  const QString name = parts.value(0);
  bool parsed = false;
  const int requestedPathSize = parts.value(1).toInt(&parsed);
  const int fallback = parsed ? requestedPathSize : 20;
  const QSize wanted = requestedSize.isValid()
                           ? requestedSize
                           : QSize(fallback, fallback);
  QPixmap pixmap;
  if (!name.isEmpty())
    pixmap = QIcon::fromTheme(name).pixmap(wanted);
  if (size)
    *size = pixmap.size();
  return pixmap;
}

void IconImageProvider::install(QQmlEngine *engine) {
  if (!engine)
    return;
  engine->addImageProvider(QStringLiteral("synchroicon"),
                           new IconImageProvider);
}
