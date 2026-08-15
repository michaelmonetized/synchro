#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "XdgOpen.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

namespace {

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

int findProxy(const FilterProxy &proxy, const QString &name) {
  for (int i = 0; i < proxy.rowCount(); ++i) {
    if (proxy.data(proxy.index(i, 0), DirectoryModel::NameRole).toString() ==
        name)
      return i;
  }
  return -1;
}

QString nameAt(const FilterProxy &proxy, int row) {
  return proxy.data(proxy.index(row, 0), DirectoryModel::NameRole).toString();
}

// 1x1 PNG so Image {} has a real decode target.
bool writePng(const QString &path) {
  static const unsigned char kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xde, 0x00, 0x00, 0x00,
      0x0c, 0x49, 0x44, 0x41, 0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
      0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xfe, 0xd4, 0xef, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write(reinterpret_cast<const char *>(kPng), sizeof(kPng)) ==
         qint64(sizeof(kPng));
}

} // namespace

class PeekOverlayTest : public QObject {
  Q_OBJECT

private slots:
  void spaceTogglesImagePeekAndJSteps();
};

void PeekOverlayTest::spaceTogglesImagePeekAndJSteps() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.image")));
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  const int b = findProxy(proxy, QStringLiteral("b.png"));
  QVERIFY(a >= 0);
  QVERIFY(b >= 0);
  const int first = qMin(a, b);
  const int second = qMax(a, b);
  proxy.setCurrentIndex(first);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  QString errors;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                   &engine,
                   [&]() { errors = QStringLiteral("create failed"); });
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY2(!engine.rootObjects().isEmpty(),
           qPrintable(errors.isEmpty() ? QStringLiteral("Main.qml produced no "
                                                        "root object")
                                       : errors));

  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("peekOverlay"));
  QVERIFY(overlay);

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));

  QTest::keyClick(window, Qt::Key_Space);
  QVERIFY(QTest::qWaitFor([&] { return hostApi.isOpen(); }, 2000));
  QVERIFY(overlay->isVisible());
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          2000));
  QVERIFY2(list->hasActiveFocus(),
           "preview must not steal list focus; KeyMachine owns peek keys");

  QTest::keyClick(window, Qt::Key_Space);
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.isOpen(); }, 2000));
  QVERIFY(!overlay->isVisible());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  QTest::keyClick(window, Qt::Key_Space);
  QVERIFY(QTest::qWaitFor([&] { return hostApi.isOpen(); }, 2000));
  QVERIFY(list->hasActiveFocus());
  QTest::keyClick(window, Qt::Key_J);
  QVERIFY(QTest::qWaitFor([&] { return proxy.currentIndex() == second; },
                          2000));
  QCOMPARE(nameAt(proxy, proxy.currentIndex()),
           nameAt(proxy, second));
  QVERIFY(hostApi.isOpen());
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  PeekOverlayTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "peek_overlay_test.moc"
