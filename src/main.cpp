#include "Config.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "SearchModel.h"
#include "XdgOpen.h"
#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <cstdio>
#include <cstring>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

int main(int argc, char *argv[]) {
  if (argc >= 2 && std::strcmp(argv[1], "handler") == 0) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("synchro"));
    app.setApplicationVersion(QStringLiteral(SYNCHRO_VERSION));
    app.setOrganizationName(QStringLiteral("omarchy"));
    app.setOrganizationDomain(QStringLiteral("omarchy.org"));
    return runHandlerCli(argc, argv);
  }

  QGuiApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("synchro"));
  app.setApplicationDisplayName(QStringLiteral("Synchro"));
  app.setApplicationVersion(QStringLiteral(SYNCHRO_VERSION));
  app.setOrganizationName(QStringLiteral("omarchy"));
  app.setOrganizationDomain(QStringLiteral("omarchy.org"));
  app.setDesktopFileName(QStringLiteral("org.omarchy.synchro"));

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
  parser.addPositionalArgument(QStringLiteral("path"),
                               QStringLiteral("Directory to open."),
                               QStringLiteral("[path]"));
  parser.process(app);
  Q_UNUSED(parser.isSet(newWindowOption));

  Config config;
  QString startPath = QDir::homePath();
  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty())
    startPath = positional.first();
  else if (!config.lastPath().isEmpty())
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
  KeyMachine keyMachine(&directoryModel, &filterProxy, &navStack);
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
  HandlerLoader handlerLoader;
  XdgOpen xdgOpen;
  if (!xdgOpen.load()) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(xdgOpen.lastError()));
  }

  directoryModel.setPath(startPath);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
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
  keyMachine.setPeekHost(&hostApi);
  QObject::connect(&keyMachine, &KeyMachine::terminalRequested, &hostApi,
                   &HostApi::runTerminal);
  QObject::connect(&keyMachine, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"),
                                           &hostApi);

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

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("Synchro", "Main");
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}
