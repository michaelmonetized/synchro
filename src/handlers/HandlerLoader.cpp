#include "HandlerLoader.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QMetaObject>
#include <QQuickItem>

#include <cstdio>

QUrl HandlerLoader::entryPointUrl(const HandlerRegistry::Record &rec,
                                  const QString &kind) {
  const QString rel = rec.manifest.entryPoints.value(kind);
  QString resolved;
  if (!Manifest::confineEntryPoint(rec.sourceDir, rel, &resolved, nullptr))
    return {};
  return QUrl::fromLocalFile(resolved);
}

QQmlComponent *HandlerLoader::componentFor(QQmlEngine *engine,
                                           const HandlerRegistry::Record &rec,
                                           const QString &kind,
                                           const QUrl &url) {
  const QString key = rec.manifest.id + QLatin1Char('#') + kind;
  if (auto *warm = m_warm.value(key)) {
    if (warm->isReady())
      return warm;
    // Modal Peek is allowed to win a race with ambient preparation. Give it
    // an immediate component without disturbing the warming cache entry.
    if (warm->isLoading())
      return new QQmlComponent(engine, url, engine);
    m_warm.remove(key);
    warm->deleteLater();
  }
  auto *comp = new QQmlComponent(engine, url, engine);
  if (rec.manifest.keepLoaded)
    m_warm.insert(key, comp);
  return comp;
}

bool HandlerLoader::isReady(const HandlerRegistry::Record &rec,
                            const QString &kind) const {
  const QString key = rec.manifest.id + QLatin1Char('#') + kind;
  const QQmlComponent *component = m_warm.value(key);
  return component && component->isReady();
}

void HandlerLoader::prepareAsync(QQmlEngine *engine,
                                 const HandlerRegistry::Record &rec,
                                 const QString &kind, QObject *context,
                                 std::function<void(bool)> finished) {
  if (!engine || !context) {
    finished(false);
    return;
  }
  const QString key = rec.manifest.id + QLatin1Char('#') + kind;
  QQmlComponent *component = m_warm.value(key);
  if (!component) {
    const QUrl url = entryPointUrl(rec, kind);
    if (!url.isValid() || url.scheme() != QLatin1String("file")) {
      finished(false);
      return;
    }
    component = new QQmlComponent(engine, url, QQmlComponent::Asynchronous,
                                  engine);
    m_warm.insert(key, component);
  }
  if (component->isReady() || component->isError()) {
    const bool ready = component->isReady();
    QMetaObject::invokeMethod(
        context, [finished = std::move(finished), ready] { finished(ready); },
        Qt::QueuedConnection);
    return;
  }
  QObject::connect(
      component, &QQmlComponent::statusChanged, context,
      [component, finished = std::move(finished)](QQmlComponent::Status status) {
        if (status == QQmlComponent::Ready || status == QQmlComponent::Error)
          finished(status == QQmlComponent::Ready);
      },
      Qt::SingleShotConnection);
}

QQuickItem *HandlerLoader::create(QQmlEngine *engine,
                                  const HandlerRegistry::Record &rec,
                                  const QString &kind, QObject *host,
                                  const QUrl &file,
                                  const QVariantList &selection,
                                  const QVariantMap &initialProperties) {
  m_error.clear();
  if (!engine) {
    m_error = QStringLiteral("no engine");
    return nullptr;
  }
  const QUrl url = entryPointUrl(rec, kind);
  if (!url.isValid() || url.scheme() != QLatin1String("file")) {
    m_error = QStringLiteral("unsafe or missing entry point");
    return nullptr;
  }
  QQmlComponent *comp = componentFor(engine, rec, kind, url);
  if (!comp) {
    m_error = QStringLiteral("failed to create component");
    return nullptr;
  }
  const QString cacheKey = rec.manifest.id + QLatin1Char('#') + kind;
  const bool cached = m_warm.value(cacheKey) == comp;
  if (comp->isError() || !comp->isReady()) {
    QStringList lines;
    for (const QQmlError &e : comp->errors())
      lines.append(e.toString());
    m_error = lines.join(QLatin1Char('\n'));
    if (!cached)
      delete comp;
    return nullptr;
  }

  QObject *obj = comp->beginCreate(engine->rootContext());
  auto *item = qobject_cast<QQuickItem *>(obj);
  if (!item) {
    delete obj;
    m_error = QStringLiteral("entry point did not create an Item");
    if (!cached)
      delete comp;
    return nullptr;
  }
  item->setProperty("file", file);
  item->setProperty("host", QVariant::fromValue(host));
  item->setProperty("selection", selection);
  item->setProperty("manifest", rec.manifest.toVariantMap());
  for (auto it = initialProperties.cbegin();
       it != initialProperties.cend(); ++it)
    item->setProperty(it.key().toUtf8().constData(), it.value());
  comp->completeCreate();
  if (!cached)
    delete comp;
  return item;
}
