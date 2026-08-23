#pragma once

#include <QQuickImageProvider>

class QQmlEngine;

class IconImageProvider final : public QQuickImageProvider {
public:
  IconImageProvider();

  QPixmap requestPixmap(const QString &id, QSize *size,
                        const QSize &requestedSize) override;

  static void install(QQmlEngine *engine);
};
