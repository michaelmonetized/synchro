#pragma once

#include "Config.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "SearchModel.h"
#include "SelectionModel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class FileChooserAdaptor;
class HandlerLoader;
class HandlerRegistry;
class HostApi;
class MimeMap;
class PortalRequestAdaptor;
class QQmlEngine;
class QQuickWindow;
class XdgOpen;

struct PortalFilter {
  QString name;
  QVector<PortalFilterRule> rules;
};

struct PortalChoice {
  QString id;
  QString label;
  QStringList optionIds;
  QStringList optionLabels;
  QString value;
};

class ChooserSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)
  Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)
  Q_PROPERTY(bool saveMode READ saveMode CONSTANT)
  Q_PROPERTY(bool directoryMode READ directoryMode CONSTANT)
  Q_PROPERTY(bool multiple READ multiple CONSTANT)
  Q_PROPERTY(QString saveName READ saveName WRITE setSaveName NOTIFY
                 saveNameChanged)
  Q_PROPERTY(bool overwriteOpen READ overwriteOpen NOTIFY overwriteOpenChanged)
  Q_PROPERTY(QString overwriteName READ overwriteName NOTIFY overwriteOpenChanged)
  Q_PROPERTY(QStringList filterNames READ filterNames CONSTANT)
  Q_PROPERTY(int currentFilterIndex READ currentFilterIndex WRITE
                 setCurrentFilterIndex NOTIFY currentFilterIndexChanged)
  Q_PROPERTY(QVariantList choiceModels READ choiceModels NOTIFY choicesChanged)
  Q_PROPERTY(bool showFilters READ showFilters CONSTANT)
  Q_PROPERTY(bool showChoices READ showChoices CONSTANT)
  Q_PROPERTY(bool showSaveName READ showSaveName CONSTANT)
  Q_PROPERTY(QString destPreview READ destPreview NOTIFY destPreviewChanged)
  Q_PROPERTY(DirectoryModel *directoryModel READ directoryModel CONSTANT)
  Q_PROPERTY(FilterProxy *filterProxy READ filterProxy CONSTANT)
  Q_PROPERTY(NavStack *navStack READ navStack CONSTANT)
  Q_PROPERTY(SelectionModel *selectionModel READ selectionModel CONSTANT)
  Q_PROPERTY(KeyMachine *keyMachine READ keyMachine CONSTANT)
  Q_PROPERTY(QObject *hostApi READ hostApi CONSTANT)
  Q_PROPERTY(LocationChips *locationChips READ locationChips CONSTANT)

public:
  enum class Kind { OpenFile, SaveFile, SaveFiles };

  ChooserSession(Kind kind, const QDBusObjectPath &handle, const QString &title,
                 const QVariantMap &options, HandlerRegistry *registry,
                 HandlerLoader *loader, XdgOpen *xdg, MimeMap *mime,
                 QQmlEngine *engine, QObject *parent = nullptr);
  ~ChooserSession() override;

  // Hyprland floats titles matching ^(Open|Save|Select|Choose). Apps often
  // send a filename or empty title for Save As; keep a kind prefix.
  static QString windowTitle(Kind kind, bool directory, const QString &title,
                             const QString &appId = QString());

  QDBusObjectPath handle() const { return m_handle; }
  Kind kind() const { return m_kind; }
  bool done() const { return m_done; }

  void setDelayedReply(const QDBusConnection &connection,
                       const QDBusMessage &message);
  bool hasDelayedReply() const { return m_hasReply; }
  QDBusConnection replyConnection() const { return m_replyConnection; }
  QDBusMessage replyMessage() const { return m_replyMessage; }

  QString title() const { return m_title; }
  QString acceptLabel() const { return m_acceptLabel; }
  bool saveMode() const {
    return m_kind == Kind::SaveFile || m_kind == Kind::SaveFiles;
  }
  bool directoryMode() const { return m_directory; }
  bool multiple() const { return m_multiple; }
  QString saveName() const { return m_saveName; }
  bool overwriteOpen() const { return m_overwriteOpen; }
  QString overwriteName() const { return m_overwriteName; }
  QStringList filterNames() const;
  int currentFilterIndex() const { return m_filterIndex; }
  QVariantList choiceModels() const;
  bool showFilters() const { return !m_filters.isEmpty(); }
  bool showChoices() const { return !m_choices.isEmpty(); }
  bool showSaveName() const { return m_kind == Kind::SaveFile; }
  QString destPreview() const;

  DirectoryModel *directoryModel() { return &m_model; }
  FilterProxy *filterProxy() { return &m_filter; }
  NavStack *navStack() { return &m_nav; }
  SelectionModel *selectionModel() { return &m_selection; }
  KeyMachine *keyMachine() { return &m_keys; }
  QObject *hostApi() const;
  LocationChips *locationChips() { return &m_chips; }

  Q_INVOKABLE void setSaveName(const QString &name);
  Q_INVOKABLE void setCurrentFilterIndex(int index);
  Q_INVOKABLE void cycleFilter(int delta);
  Q_INVOKABLE void setChoiceValue(int index, const QString &value);
  Q_INVOKABLE void accept();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void confirmOverwrite();
  Q_INVOKABLE void dismissOverwrite();
  void fail();

  bool createWindow(QQmlEngine *engine);
  QVariantMap successResults(const QStringList &paths) const;

signals:
  void titleChanged();
  void saveNameChanged();
  void overwriteOpenChanged();
  void currentFilterIndexChanged();
  void destPreviewChanged();
  void choicesChanged();
  void finished(uint response, const QVariantMap &results);

private:
  void applyOptions(const QVariantMap &options);
  void applyCurrentFilter();
  void onAcceptRequested();
  void finish(uint response, const QVariantMap &results);
  void finishWithPaths(const QStringList &paths);
  void promptOverwrite(const QString &name);
  QString destFolder() const;
  QString saveFileDest() const;
  bool cursorIsDir() const;
  bool cursorIsFile() const;
  QStringList collectOpenPaths() const;
  QStringList collectSaveFilesPaths() const;
  QVariant currentFilterVariant() const;
  QVariant choicesVariant() const;

  Kind m_kind = Kind::OpenFile;
  QDBusObjectPath m_handle;
  QDBusConnection m_replyConnection = QDBusConnection::sessionBus();
  QDBusMessage m_replyMessage;
  bool m_hasReply = false;
  bool m_done = false;
  bool m_multiple = false;
  bool m_directory = false;
  bool m_overwriteOpen = false;
  int m_filterIndex = 0;
  QString m_title;
  QString m_acceptLabel;
  QString m_saveName;
  QString m_overwriteName;
  QStringList m_saveFiles;
  QVector<PortalFilter> m_filters;
  QVector<PortalChoice> m_choices;

  Config m_config;
  RecentStore m_recents;
  SearchModel m_search;
  DirectoryModel m_model;
  FilterProxy m_filter;
  NavStack m_nav;
  SelectionModel m_selection;
  KeyMachine m_keys;
  LocationChips m_chips;
  HostApi *m_host = nullptr;
  QQuickWindow *m_window = nullptr;
};

class PortalRequestAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
  explicit PortalRequestAdaptor(ChooserSession *session);

public slots:
  void Close();

private:
  ChooserSession *m_session = nullptr;
};

class FileChooserAdaptor : public QDBusAbstractAdaptor {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
  Q_CLASSINFO("org.qtproject.QtDBus.QtTypeName.In4", "QVariantMap")
  Q_CLASSINFO("org.qtproject.QtDBus.QtTypeName.Out1", "QVariantMap")

public:
  explicit FileChooserAdaptor(QObject *parent);

public slots:
  uint OpenFile(const QDBusObjectPath &handle, const QString &app_id,
                const QString &parent_window, const QString &title,
                const QVariantMap &options, QVariantMap &results);
  uint SaveFile(const QDBusObjectPath &handle, const QString &app_id,
                const QString &parent_window, const QString &title,
                const QVariantMap &options, QVariantMap &results);
  uint SaveFiles(const QDBusObjectPath &handle, const QString &app_id,
                 const QString &parent_window, const QString &title,
                 const QVariantMap &options, QVariantMap &results);
};

class PortalService : public QObject, public QDBusContext {
  Q_OBJECT

public:
  static QString defaultServiceName();
  static QString objectPath();

  explicit PortalService(QObject *parent = nullptr);
  ~PortalService() override;

  bool start(const QDBusConnection &connection = QDBusConnection::sessionBus(),
             const QString &serviceName = defaultServiceName());
  QString lastError() const { return m_error; }
  QString serviceName() const { return m_serviceName; }
  QDBusConnection connection() const { return m_connection; }

  ChooserSession *beginRequest(ChooserSession::Kind kind,
                               const QDBusObjectPath &handle,
                               const QString &appId,
                               const QString &parentWindow,
                               const QString &title,
                               const QVariantMap &options,
                               const QDBusConnection &connection,
                               const QDBusMessage &message);
  ChooserSession *openStandalone(ChooserSession::Kind kind,
                                 const QString &title,
                                 const QVariantMap &options);

  ChooserSession *session(const QString &handlePath) const;
  QStringList sessionHandles() const;

signals:
  void lastSessionFinished(uint response, const QVariantMap &results);

private:
  void onSessionFinished(ChooserSession *session, uint response,
                         const QVariantMap &results);

  QDBusConnection m_connection = QDBusConnection::sessionBus();
  QString m_serviceName;
  QString m_error;
  FileChooserAdaptor *m_adaptor = nullptr;
  QQmlEngine *m_engine = nullptr;
  HandlerRegistry *m_registry = nullptr;
  HandlerLoader *m_loader = nullptr;
  XdgOpen *m_xdg = nullptr;
  MimeMap *m_mime = nullptr;
  QHash<QString, ChooserSession *> m_sessions;
};
