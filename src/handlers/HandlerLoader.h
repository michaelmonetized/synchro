#pragma once

#include "HandlerRegistry.h"

#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariantList>

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
                     const QVariantList &selection);

  QString lastError() const { return m_error; }

private:
  QQmlComponent *componentFor(QQmlEngine *engine,
                              const HandlerRegistry::Record &rec,
                              const QString &kind, const QUrl &url);

  QHash<QString, QQmlComponent *> m_warm;
  QString m_error;
};
