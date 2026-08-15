#pragma once

#include "HandlerActions.h"
#include "HandlerExec.h"
#include "Manifest.h"
#include "PeekHost.h"

#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

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
  Q_PROPERTY(bool actionOpen READ actionOpen NOTIFY actionOpenChanged)
  Q_PROPERTY(QObject *actionItem READ actionItem NOTIFY actionItemChanged)
  Q_PROPERTY(QVariantList openCandidates READ openCandidates NOTIFY
                 openCandidatesChanged)

public:
  HostApi(DirectoryModel *model, FilterProxy *proxy, NavStack *nav,
          HandlerRegistry *registry, HandlerLoader *loader, XdgOpen *xdg,
          MimeMap *mime, QQmlEngine *engine, QObject *parent = nullptr);

  QUrl file() const { return m_file; }
  QVariantList selection() const { return m_selection; }
  bool isOpen() const override { return m_open; }
  QObject *previewItem() const { return m_previewItem; }
  QString title() const { return m_title; }
  bool actionOpen() const override { return m_actionOpen; }
  QObject *actionItem() const { return m_actionItem; }
  QVariantList openCandidates() const { return m_openCandidates; }
  QString lastError() const { return m_error; }
  HandlerExec &exec() { return m_exec; }

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
  Q_INVOKABLE bool openFile(const QString &path, const QString &mime);
  Q_INVOKABLE bool runOpen(const QString &handlerId);
  Q_INVOKABLE bool runTerminal();
  Q_INVOKABLE bool runTrash();
  Q_INVOKABLE bool openWithPalette();
  Q_INVOKABLE void closeAction() override;

signals:
  void fileChanged();
  void selectionChanged();
  void previewItemChanged();
  void titleChanged();
  void statReady(const QUrl &url, const QVariantMap &st);
  void actionItemChanged();
  void openCandidatesChanged();

private:
  Manifest::Item currentItem() const;
  Manifest::Item itemAt(int proxyRow) const;
  QVector<Manifest::Item> currentItems() const;
  void setCurrentFromModel();
  void reloadPreview();
  void destroyPreview();
  void destroyAction();
  void refreshOpenCandidates();
  void onEntryStat(const QString &path, const QVariantMap &st);

  DirectoryModel *m_model = nullptr;
  FilterProxy *m_proxy = nullptr;
  NavStack *m_nav = nullptr;
  HandlerRegistry *m_registry = nullptr;
  HandlerLoader *m_loader = nullptr;
  XdgOpen *m_xdg = nullptr;
  MimeMap *m_mime = nullptr;
  QQmlEngine *m_engine = nullptr;

  HandlerExec m_exec;
  HandlerActions m_actions;

  QUrl m_file;
  QVariantList m_selection;
  QVariantList m_openCandidates;
  QString m_title;
  QString m_error;
  QObject *m_previewItem = nullptr;
  QObject *m_actionItem = nullptr;
  QString m_loadedId;
  bool m_open = false;
  bool m_actionOpen = false;
};
