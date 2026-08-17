#include "ThumbImageProvider.h"

#include "ThumbCache.h"

#include <QQmlEngine>

ThumbImageProvider::ThumbImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage ThumbImageProvider::requestImage(const QString &id, QSize *size,
                                        const QSize &) {
  const QImage img = ThumbCache::instance().imageForKey(id);
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
