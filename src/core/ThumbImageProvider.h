#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

class QQmlEngine;

class ThumbImageProvider : public QQuickAsyncImageProvider {
public:
  ThumbImageProvider();
  ~ThumbImageProvider() override;

  QQuickImageResponse *
  requestImageResponse(const QString &id,
                       const QSize &requestedSize) override;
  static void install(QQmlEngine *engine);

private:
  QThreadPool m_pool;
};
