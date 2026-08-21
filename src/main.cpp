#include "Config.h"
#include "DirectoryModel.h"
#include "FileOpEngine.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "PortalService.h"
#include "RecentStore.h"
#include "SearchModel.h"
#include "SelectionModel.h"
#include "ThumbnailService.h"
#include "ThumbImageProvider.h"
#include "VolumeStore.h"
#include "XdgOpen.h"
#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdio>
#include <cstring>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

namespace {

bool argvHasFlag(int argc, char **argv, const char *flag) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0)
      return true;
  }
  return false;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc >= 2 && std::strcmp(argv[1], "handler") == 0) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("synchro"));
    app.setApplicationVersion(QStringLiteral(SYNCHRO_VERSION));
    app.setOrganizationName(QStringLiteral("omarchy"));
    app.setOrganizationDomain(QStringLiteral("omarchy.org"));
    return runHandlerCli(argc, argv);
  }

  // --portal is a FileChooser *impl*. Qt's QPA otherwise registers this
  // process as a host-portal *client* (org.freedesktop.host.portal.Registry),
  // which warns "Connection already associated with an application ID" and
  // would recurse if the session FileChooser is synchro.
  if (argvHasFlag(argc, argv, "--portal"))
    qputenv("QT_NO_XDG_DESKTOP_PORTAL", QByteArrayLiteral("1"));

  QGuiApplication::setDesktopFileName(QStringLiteral("org.omarchy.synchro"));
  QGuiApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("synchro"));
  app.setApplicationDisplayName(QStringLiteral("Synchro"));
  app.setApplicationVersion(QStringLiteral(SYNCHRO_VERSION));
  app.setOrganizationName(QStringLiteral("omarchy"));
  app.setOrganizationDomain(QStringLiteral("omarchy.org"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Omarchy file OS"));
  parser.addHelpOption();
  parser.addVersionOption();
  // Accepted for Nautilus-launcher compatibility. Each invocation is its
  // own process; v1 does not implement single-instance.
  const QCommandLineOption newWindowOption(
      QStringLiteral("new-window"),
      QStringLiteral("Open a new window (new process; this is the default)."));
  parser.addOption(newWindowOption);
  const QCommandLineOption portalOption(
      QStringLiteral("portal"),
      QStringLiteral(
          "Run as xdg-desktop-portal FileChooser backend (long-running)."));
  parser.addOption(portalOption);
  const QCommandLineOption chooserOption(
      QStringLiteral("chooser"),
      QStringLiteral("Standalone file chooser; print file:// URIs on stdout."));
  parser.addOption(chooserOption);
  const QCommandLineOption titleOption(
      QStringLiteral("title"), QStringLiteral("Chooser window title."),
      QStringLiteral("title"));
  parser.addOption(titleOption);
  const QCommandLineOption multipleOption(
      QStringLiteral("multiple"),
      QStringLiteral("Allow selecting more than one file."));
  parser.addOption(multipleOption);
  const QCommandLineOption directoryOption(
      QStringLiteral("directory"),
      QStringLiteral("Select a folder instead of files."));
  parser.addOption(directoryOption);
  const QCommandLineOption saveOption(
      QStringLiteral("save"), QStringLiteral("SaveFile / SaveFiles mode."));
  parser.addOption(saveOption);
  const QCommandLineOption currentFolderOption(
      QStringLiteral("current-folder"),
      QStringLiteral("Starting directory for --chooser."),
      QStringLiteral("path"));
  parser.addOption(currentFolderOption);
  const QCommandLineOption currentNameOption(
      QStringLiteral("current-name"),
      QStringLiteral("Suggested file name for --chooser --save."),
      QStringLiteral("name"));
  parser.addOption(currentNameOption);
  parser.addPositionalArgument(QStringLiteral("path"),
                               QStringLiteral("Directory to open."),
                               QStringLiteral("[path]"));
  parser.process(app);
  Q_UNUSED(parser.isSet(newWindowOption));

  if (parser.isSet(portalOption)) {
    PortalService portal;
    if (!portal.start()) {
      std::fprintf(stderr, "synchro: portal: %s\n",
                   qPrintable(portal.lastError()));
      return 1;
    }
    std::fprintf(stderr, "synchro: portal ready (%s)\n",
                 qPrintable(portal.serviceName()));
    return app.exec();
  }

  if (parser.isSet(chooserOption)) {
    PortalService portal;
    QVariantMap options;
    options.insert(QStringLiteral("multiple"), parser.isSet(multipleOption));
    options.insert(QStringLiteral("directory"), parser.isSet(directoryOption));
    if (parser.isSet(currentFolderOption)) {
      QByteArray folder = parser.value(currentFolderOption).toUtf8();
      folder.append('\0');
      options.insert(QStringLiteral("current_folder"), folder);
    }
    if (parser.isSet(currentNameOption))
      options.insert(QStringLiteral("current_name"),
                     parser.value(currentNameOption));
    const bool save = parser.isSet(saveOption);
    const auto kind = save ? (parser.isSet(directoryOption)
                                  ? ChooserSession::Kind::SaveFiles
                                  : ChooserSession::Kind::SaveFile)
                           : ChooserSession::Kind::OpenFile;
    const QString title = parser.value(titleOption);
    QObject::connect(
        &portal, &PortalService::lastSessionFinished, &app,
        [&](uint response, const QVariantMap &results) {
          if (response == 0) {
            const QStringList uris =
                results.value(QStringLiteral("uris")).toStringList();
            for (const QString &uri : uris)
              std::printf("%s\n", qPrintable(uri));
            QCoreApplication::exit(uris.isEmpty() ? 1 : 0);
          } else {
            QCoreApplication::exit(1);
          }
        });
    if (!portal.openStandalone(kind, title, options)) {
      std::fprintf(stderr, "synchro: chooser failed to open\n");
      return 2;
    }
    return app.exec();
  }

  Config config;
  QString startPath = QDir::homePath();
  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty())
    startPath = positional.first();
  else if (!config.lastPath().isEmpty() &&
           !config.lastPath().startsWith(QLatin1String("search:")))
    startPath = config.lastPath();

  DirectoryModel directoryModel;
  SearchModel searchModel;
  directoryModel.setSearchModel(&searchModel);
  directoryModel.setShowHidden(config.showHidden());
  FilterProxy filterProxy;
  filterProxy.setDirectoryModel(&directoryModel);
  filterProxy.setSortRoleName(config.sortRole());
  filterProxy.setSortOrder(config.sortOrder());
  NavStack navStack(&directoryModel);
  SelectionModel selectionModel(&filterProxy, &directoryModel);
  FileOpEngine fileOpEngine;
  fileOpEngine.setSelection(&selectionModel);
  fileOpEngine.setDirectoryModel(&directoryModel);
  KeyMachine keyMachine(&directoryModel, &filterProxy, &navStack);
  keyMachine.setPanelSide(config.panelSide());
  if (config.panelOpen())
    keyMachine.setPanelId(config.panelApp());
  keyMachine.setSelection(&selectionModel);
  keyMachine.setFileOps(&fileOpEngine);
  keyMachine.setSearchModel(&searchModel);
  keyMachine.setGridMode(config.view() == QLatin1String("grid"));
  MimeMap mimeMap;
  HandlerRegistry handlerRegistry;
  handlerRegistry.scan();
  LocationChips locationChips;
  locationChips.setRegistry(&handlerRegistry);
  locationChips.setConfig(&config);
  locationChips.setNav(&navStack);
  locationChips.setDirectoryModel(&directoryModel);
  keyMachine.setLocationChips(&locationChips);
  HandlerLoader handlerLoader;
  XdgOpen xdgOpen;
  if (!xdgOpen.load()) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(xdgOpen.lastError()));
  }

  if (ThumbnailService *thumbs = directoryModel.thumbnailService()) {
    QVector<ExecThumbnailer> extra;
    for (const auto &rec : handlerRegistry.handlers()) {
      if (!rec.enabled || !rec.manifest.hasKind(QStringLiteral("thumbnail")))
        continue;
      if (rec.manifest.runtime(QStringLiteral("thumbnail")) ==
          QLatin1String("core"))
        continue;
      const QString exec = rec.manifest.execLine(QStringLiteral("thumbnail"));
      if (exec.isEmpty())
        continue;
      ExecThumbnailer t;
      t.mimes = rec.manifest.match.mime;
      t.exec = exec;
      t.tryExec = rec.manifest.tryExec(QStringLiteral("thumbnail"));
      extra.append(t);
    }
    thumbs->setHandlerThumbnailers(extra);
  }

  directoryModel.setPath(startPath);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &directoryModel);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &filterProxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"),
                                           &navStack);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"),
                                           &keyMachine);
  engine.rootContext()->setContextProperty(QStringLiteral("locationChips"),
                                           &locationChips);
  engine.rootContext()->setContextProperty(QStringLiteral("appConfig"),
                                           &config);

  HostApi hostApi(&directoryModel, &filterProxy, &navStack, &handlerRegistry,
                  &handlerLoader, &xdgOpen, &mimeMap, &engine);
  hostApi.setSelection(&selectionModel);
  hostApi.setFileOps(&fileOpEngine);
  keyMachine.setPeekHost(&hostApi);
  hostApi.setGridMode(keyMachine.gridMode());
  QObject::connect(&keyMachine, &KeyMachine::gridModeChanged, &hostApi, [&] {
    hostApi.setGridMode(keyMachine.gridMode());
  });
  QObject::connect(&keyMachine, &KeyMachine::terminalRequested, &hostApi,
                   &HostApi::runTerminal);
  QObject::connect(&keyMachine, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
  // Volume watching talks to UDisks over DBus; keep it off the critical
  // path so the first frame and first rows land sooner.
  QTimer::singleShot(0, &app, [] { VolumeStore::instance().startWatching(); });
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"),
                                           &hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("selectionModel"),
                                           &selectionModel);
  engine.rootContext()->setContextProperty(QStringLiteral("fileOpEngine"),
                                           &fileOpEngine);
  QObject::connect(&fileOpEngine, &FileOpEngine::errorStringChanged,
                   &keyMachine, [&] {
                     if (!fileOpEngine.errorString().isEmpty())
                       keyMachine.setStatusMessage(fileOpEngine.errorString());
                   });
  QObject::connect(&fileOpEngine, &FileOpEngine::lastMessageChanged,
                   &keyMachine, [&] {
                     if (!fileOpEngine.lastMessage().isEmpty())
                       keyMachine.setStatusMessage(fileOpEngine.lastMessage());
                   });

  for (const auto &rec : handlerRegistry.handlers()) {
    if (rec.enabled && rec.manifest.hasKind(QStringLiteral("action")))
      keyMachine.registerAction(rec.manifest.id, rec.manifest.name);
  }
  keyMachine.setActionRunner([&](const QString &id, QString *error) {
    if (hostApi.runAction(id))
      return true;
    if (error)
      *error = hostApi.lastError();
    return false;
  });

  QTimer persistTimer;
  persistTimer.setSingleShot(true);
  persistTimer.setInterval(200);
  QObject::connect(&persistTimer, &QTimer::timeout, &config, [&] {
    if (config.writable())
      config.save();
  });
  auto schedulePersist = [&] { persistTimer.start(); };
  QObject::connect(&directoryModel, &DirectoryModel::showHiddenChanged, &config,
                   [&] {
                     config.setShowHidden(directoryModel.showHidden());
                     schedulePersist();
                   });
  QObject::connect(&directoryModel, &DirectoryModel::pathChanged, &config, [&] {
    config.setLastPath(directoryModel.path());
    schedulePersist();
  });
  QObject::connect(&keyMachine, &KeyMachine::gridModeChanged, &config, [&] {
    config.setView(keyMachine.gridMode() ? QStringLiteral("grid")
                                         : QStringLiteral("list"));
    schedulePersist();
  });
  QObject::connect(&filterProxy, &FilterProxy::sortChanged, &config, [&] {
    config.setSortRole(filterProxy.sortRoleName());
    config.setSortOrder(filterProxy.sortOrder());
    schedulePersist();
  });
  QObject::connect(&keyMachine, &KeyMachine::panelChanged, &config, [&] {
    config.setPanelOpen(!keyMachine.panelId().isEmpty());
    if (!keyMachine.panelId().isEmpty())
      config.setPanelApp(keyMachine.panelId());
    config.setPanelSide(keyMachine.panelSide());
    schedulePersist();
  });
  QObject::connect(&config, &Config::panelChanged, &config,
                   [&] { schedulePersist(); });
  QObject::connect(&config, &Config::pinsChanged, &config,
                   [&] { schedulePersist(); });
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &config, [&] {
    persistTimer.stop();
    if (config.writable())
      config.save();
  });

  // Declared last so it dies first and drops this connection before hostApi.
  RecentStore recents;
  keyMachine.setRecentStore(&recents);
  directoryModel.setRecentStore(&recents);
  QObject::connect(
      &directoryModel, &DirectoryModel::fileActivated, &recents,
      [&](const QString &path, const QString &mime) {
        QString resolved = mime;
        if (resolved.isEmpty())
          resolved = mimeMap.mimeForFile(path);
        if (!hostApi.openFile(path, resolved)) {
          std::fprintf(stderr, "synchro: open %s: %s\n", qPrintable(path),
                       qPrintable(hostApi.lastError()));
          return;
        }
        recents.record(path, resolved);
      });
  QObject::connect(&hostApi, &HostApi::fileCommitted, &recents,
                   [&](const QString &path, const QString &mime) {
                     recents.record(path, mime);
                   });

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("Synchro", "Main");
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}
