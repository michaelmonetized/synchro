#include "HandlerLoader.h"

#include <QDir>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>

#include <cstdio>

QUrl HandlerLoader::entryPointUrl(const HandlerRegistry::Record &rec,
                                  const QString &kind) {
  const QString rel = rec.manifest.entryPoints.value(kind);
  if (!Manifest::isSafeEntryPoint(rel))
    return {};
  const QString base = QDir::cleanPath(rec.sourceDir);
  const QString resolved = QDir::cleanPath(QDir(base).filePath(rel));
  if (resolved != base && !resolved.startsWith(base + QLatin1Char('/')))
    return {};
  const QFileInfo info(resolved);
  if (!info.isFile())
    return {};
  return QUrl::fromLocalFile(info.absoluteFilePath());
}

QQmlComponent *HandlerLoader::componentFor(QQmlEngine *engine,
                                           const HandlerRegistry::Record &rec,
                                           const QString &kind,
                                           const QUrl &url) {
  const QString key = rec.manifest.id + QLatin1Char('#') + kind;
  if (rec.manifest.keepLoaded) {
    if (auto *warm = m_warm.value(key))
      return warm;
  }
  auto *comp = new QQmlComponent(engine, url, engine);
  if (rec.manifest.keepLoaded)
    m_warm.insert(key, comp);
  return comp;
}

QQuickItem *HandlerLoader::create(QQmlEngine *engine,
                                  const HandlerRegistry::Record &rec,
                                  const QString &kind, QObject *host,
                                  const QUrl &file,
                                  const QVariantList &selection) {
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
  if (comp->isError() || !comp->isReady()) {
    QStringList lines;
    for (const QQmlError &e : comp->errors())
      lines.append(e.toString());
    m_error = lines.join(QLatin1Char('\n'));
    if (!rec.manifest.keepLoaded)
      delete comp;
    return nullptr;
  }

  QObject *obj = comp->beginCreate(engine->rootContext());
  auto *item = qobject_cast<QQuickItem *>(obj);
  if (!item) {
    delete obj;
    m_error = QStringLiteral("entry point did not create an Item");
    if (!rec.manifest.keepLoaded)
      delete comp;
    return nullptr;
  }
  item->setProperty("file", file);
  item->setProperty("host", QVariant::fromValue(host));
  item->setProperty("selection", selection);
  item->setProperty("manifest", rec.manifest.toVariantMap());
  comp->completeCreate();
  if (!rec.manifest.keepLoaded)
    delete comp;
  return item;
}
