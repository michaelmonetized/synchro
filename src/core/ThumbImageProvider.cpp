#include "ThumbImageProvider.h"

#include "ThumbCache.h"

#include <QAtomicInteger>
#include <QQuickTextureFactory>
#include <QQmlEngine>
#include <QRunnable>
#include <QThread>

namespace {

class ThumbImageResponse final : public QQuickImageResponse, public QRunnable {
public:
  explicit ThumbImageResponse(QString id)
      : m_key(std::move(id).section(QLatin1Char('/'), 0, 0)) {
    // QML owns responses and deletes them after finished(). The runnable must
    // therefore never auto-delete itself on the worker thread.
    setAutoDelete(false);
  }

  void run() override {
    QImage image;
    if (!m_cancelled.loadAcquire())
      image = ThumbCache::instance().imageForKey(m_key);
    if (!m_cancelled.loadAcquire())
      m_image = std::move(image);
    // This must be the final operation: QML may delete the response as soon as
    // it receives finished().
    emit finished();
  }

  QQuickTextureFactory *textureFactory() const override {
    return QQuickTextureFactory::textureFactoryForImage(m_image);
  }

  QString errorString() const override {
    return !m_cancelled.loadAcquire() && m_image.isNull()
               ? QStringLiteral("thumbnail unavailable")
               : QString();
  }

  void cancel() override { m_cancelled.storeRelease(true); }

private:
  QString m_key;
  QImage m_image;
  QAtomicInteger<bool> m_cancelled = false;
};

} // namespace

ThumbImageProvider::ThumbImageProvider() {
  // PNG inflation is CPU-bound, while SQLite blob reads are short. A modest
  // cap fills a page quickly without letting a thumbnail burst monopolize the
  // machine or contend with generation workers.
  m_pool.setMaxThreadCount(qBound(2, QThread::idealThreadCount(), 6));
  m_pool.setExpiryTimeout(30000);
}

ThumbImageProvider::~ThumbImageProvider() { m_pool.waitForDone(); }

QQuickImageResponse *
ThumbImageProvider::requestImageResponse(const QString &id, const QSize &) {
  auto *response = new ThumbImageResponse(id);
  m_pool.start(response, -1);
  return response;
}

void ThumbImageProvider::install(QQmlEngine *engine) {
  if (!engine)
    return;
  engine->addImageProvider(QStringLiteral("synchrothumb"),
                           new ThumbImageProvider);
}
