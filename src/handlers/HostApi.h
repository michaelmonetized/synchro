#pragma once

#include "Manifest.h"
#include "PeekHost.h"

#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class DirectoryModel;
class FilterProxy;
class HandlerLoader;
class HandlerRegistry;
class MimeMap;
class NavStack;
class QQmlEngine;
class XdgOpen;

// Peek + location chrome. No setListing — handlers cannot replace the model.
class HostApi : public PeekHost {
  Q_OBJECT
  Q_PROPERTY(QUrl file READ file NOTIFY fileChanged)
  Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
  Q_PROPERTY(bool open READ isOpen NOTIFY openChanged)
  Q_PROPERTY(QObject *previewItem READ previewItem NOTIFY previewItemChanged)
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)

public:
  HostApi(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
          HandlerRegistry *registry, HandlerLoader *loader, XdgOpen *xdg,
          MimeMap *mime, QQmlEngine *engine, QObject *parent = nullptr);

  QUrl file() const { return m_file; }
  QVariantList selection() const { return m_selection; }
  bool isOpen() const override { return m_open; }
  QObject *previewItem() const { return m_previewItem; }
  QString title() const { return m_title; }

  Q_INVOKABLE void close() override;
  Q_INVOKABLE bool toggle() override;
  Q_INVOKABLE bool openCurrent() override;
  Q_INVOKABLE void step(int delta) override;
  Q_INVOKABLE void openExternal(const QUrl &url);
  Q_INVOKABLE void reveal(const QUrl &url);
  Q_INVOKABLE void navigate(const QUrl &url);
  Q_INVOKABLE void setTitle(const QString &title);
  Q_INVOKABLE QVariantMap stat(const QUrl &url) const;
  Q_INVOKABLE void registerSurface(QObject *surface);

signals:
  void fileChanged();
  void selectionChanged();
  void previewItemChanged();
  void titleChanged();
  void statReady(const QUrl &url, const QVariantMap &st);

private:
  Manifest::Item currentItem() const;
  Manifest::Item itemAt(int proxyRow) const;
  void setCurrentFromModel();
  void reloadPreview();
  void destroyPreview();
  void onEntryStat(const QString &path, const QVariantMap &st);

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  NavStack *m_nav = nullptr;
  HandlerRegistry *m_registry = nullptr;
  HandlerLoader *m_loader = nullptr;
  XdgOpen *m_xdg = nullptr;
  MimeMap *m_mime = nullptr;
  QQmlEngine *m_engine = nullptr;

  QUrl m_file;
  QVariantList m_selection;
  QString m_title;
  QObject *m_previewItem = nullptr;
  QString m_loadedId;
  bool m_open = false;
};
