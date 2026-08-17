#pragma once

#include <QQuickImageProvider>

class QQmlEngine;

class ThumbImageProvider : public QQuickImageProvider {
public:
  ThumbImageProvider();
  QImage requestImage(const QString &id, QSize *size,
                      const QSize &requestedSize) override;
  static void install(QQmlEngine *engine);
};
