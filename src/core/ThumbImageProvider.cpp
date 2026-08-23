#include "ThumbImageProvider.h"

#include "ThumbCache.h"

#include <QQmlEngine>

ThumbImageProvider::ThumbImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbImageProvider::requestImage(const QString &id, QSize *size,
                                        const QSize &) {
  // A suffix gives QML a fresh source URL after explicit invalidation while
  // the packed cache key remains stable for the real filesystem mtime.
  const QImage img = ThumbCache::instance().imageForKey(
      id.section(QLatin1Char('/'), 0, 0));
  if (size)
    *size = img.size();
  return img;
}

void ThumbImageProvider::install(QQmlEngine *engine) {
  if (!engine)
    return;
  engine->addImageProvider(QStringLiteral("synchrothumb"),
                           new ThumbImageProvider);
}
