#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "XdgOpen.h"
#include "cli.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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

  QString startPath = QDir::homePath();
  const QStringList positional = parser.positionalArguments();
  if (!positional.isEmpty())
    startPath = positional.first();

  DirectoryModel directoryModel;
  FilterProxy filterProxy;
  filterProxy.setDirectoryModel(&directoryModel);
  NavStack navStack(&directoryModel);
  KeyMachine keyMachine(&directoryModel, &filterProxy, &navStack);
  MimeMap mimeMap;
  HandlerRegistry handlerRegistry;
  handlerRegistry.scan();
  HandlerLoader handlerLoader;
  XdgOpen xdgOpen;
  if (!xdgOpen.load()) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(xdgOpen.lastError()));
  }
  // Declared last so it dies first and drops this connection before the
  // captured xdgOpen / mimeMap refs.
  RecentStore recents;

  QObject::connect(
      &directoryModel, &DirectoryModel::fileActivated, &recents,
      [&](const QString &path, const QString &mime) {
        QString resolved = mime;
        if (resolved.isEmpty())
          resolved = mimeMap.mimeForFile(path);
        if (!xdgOpen.open(path, resolved, directoryModel.path())) {
          std::fprintf(stderr, "synchro: open %s: %s\n", qPrintable(path),
                       qPrintable(xdgOpen.lastError()));
          return;
        }
        recents.record(path, resolved);
      });

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

  HostApi hostApi(&directoryModel, &filterProxy, &navStack, &handlerRegistry,
                  &handlerLoader, &xdgOpen, &mimeMap, &engine);
  keyMachine.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"),
                                           &hostApi);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("Synchro", "Main");
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}
