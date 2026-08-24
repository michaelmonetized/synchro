#pragma once

#include "HandlerRegistry.h"

#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QObject;
class QQmlComponent;
class QQmlEngine;
class QQuickItem;

// QQmlComponent from file:// inside the handler dir only.
class HandlerLoader {
public:
  static QUrl entryPointUrl(const HandlerRegistry::Record &rec,
                            const QString &kind);

  QQuickItem *create(QQmlEngine *engine, const HandlerRegistry::Record &rec,
                     const QString &kind, QObject *host, const QUrl &file,
                     const QVariantList &selection,
                     const QVariantMap &initialProperties = {});

  bool isReady(const HandlerRegistry::Record &rec,
               const QString &kind) const;
  void prepareAsync(QQmlEngine *engine, const HandlerRegistry::Record &rec,
                    const QString &kind, QObject *context,
                    std::function<void(bool)> finished);

  QString lastError() const { return m_error; }

private:
  QQmlComponent *componentFor(QQmlEngine *engine,
                              const HandlerRegistry::Record &rec,
                              const QString &kind, const QUrl &url);

  QHash<QString, QQmlComponent *> m_warm;
  QString m_error;
};
