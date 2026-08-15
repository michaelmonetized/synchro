#include "DirectoryModel.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

int main(int argc, char *argv[]) {
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
  directoryModel.setPath(startPath);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &directoryModel);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("Synchro", "Main");
  if (engine.rootObjects().isEmpty())
    return -1;

  return app.exec();
}
