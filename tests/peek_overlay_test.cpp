#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "ThumbImageProvider.h"
#include "XdgOpen.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
#include <sqlite3.h>

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
  void ctrlKClosesOpenWithOverlay();
  void textAndMarkdownHandlersResolve();
  void videoHandlerAndWebpRaster();
  void spaceOnFolderDrillsAndEscReturns();
  void folderPeekWasdUsesOwnStride();
  void folderPeekFileBackKeepsListingAndScroll();
  void folderPeekSpaceOnFileReturnsListing();
  void folderPeekStepsPastUnpreviewableFile();
  void filePeekAdHopsAndPreviewScrolls();
  void sqliteAndDuckdbHandlersResolve();
  void rootFilePeekShowsIndexAndQCloses();
  void gridPeekIndexUsesThumbs();
  void emptyFolderShowsHintInRootAndPeek();
  void gridPeekIndexHidesFolders();
  void peekEnterCommitsFileAndFolder();
  void doLayerVerbsKeysAndCopyAs();
  void doLayerOverlaySplitChrome();
  void doLayerShowsFilePreviewAndFolderMosaic();
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
  ThumbImageProvider::install(&engine);
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

void PeekOverlayTest::ctrlKClosesOpenWithOverlay() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

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
  QVERIFY(registry.contains(QStringLiteral("synchro.action.open-with")));
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("a.png"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
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
  auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("actionOverlay"));
  QVERIFY(overlay);

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::ControlModifier, QString()));
  QVERIFY2(QTest::qWaitFor([&] { return hostApi.actionOpen(); }, 2000),
           qPrintable(hostApi.lastError()));
  QVERIFY(overlay->isVisible());
  QVERIFY(keys.actionOpen());
  auto *verbs = window->findChild<QQuickItem *>(QStringLiteral("doVerbList"));
  QVERIFY(verbs);
  auto *caption = window->findChild<QQuickItem *>(QStringLiteral("doCaption"));
  QVERIFY(caption);
  QCOMPARE(caption->property("text").toString().left(4), QStringLiteral("do ·"));

  keys.focusFilter();
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.actionOpen(); }, 2000));
  QVERIFY(!overlay->isVisible());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
}

void PeekOverlayTest::textAndMarkdownHandlersResolve() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# hi\n");
  }
  {
    QFile f(tmp.filePath(QStringLiteral("plain.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello text\n");
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.text")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.markdown")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.pdf")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.parquet")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.archive")));

  Manifest::Item md;
  md.path = tmp.filePath(QStringLiteral("notes.md"));
  md.mime = QStringLiteral("text/markdown");
  const auto mdHits = registry.resolve(QStringLiteral("preview"), {md});
  QVERIFY(!mdHits.isEmpty());
  QCOMPARE(mdHits.constFirst().id, QStringLiteral("synchro.preview.markdown"));

  Manifest::Item txt;
  txt.path = tmp.filePath(QStringLiteral("plain.txt"));
  txt.mime = QStringLiteral("text/plain");
  const auto txtHits = registry.resolve(QStringLiteral("preview"), {txt});
  QVERIFY(!txtHits.isEmpty());
  QCOMPARE(txtHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item yaml;
  yaml.path = tmp.filePath(QStringLiteral("app.yaml"));
  yaml.mime = QStringLiteral("application/yaml");
  const auto yamlHits = registry.resolve(QStringLiteral("preview"), {yaml});
  QVERIFY(!yamlHits.isEmpty());
  QCOMPARE(yamlHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item sql;
  sql.path = tmp.filePath(QStringLiteral("schema.sql"));
  sql.mime = QStringLiteral("application/sql");
  const auto sqlHits = registry.resolve(QStringLiteral("preview"), {sql});
  QVERIFY(!sqlHits.isEmpty());
  QCOMPARE(sqlHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item docker;
  docker.path = tmp.filePath(QStringLiteral("Dockerfile"));
  docker.mime = QStringLiteral("application/octet-stream");
  const auto dockerHits = registry.resolve(QStringLiteral("preview"), {docker});
  QVERIFY(!dockerHits.isEmpty());
  QCOMPARE(dockerHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item ts;
  ts.path = tmp.filePath(QStringLiteral("app.ts"));
  ts.mime = QStringLiteral("video/mp2t");
  const auto tsHits = registry.resolve(QStringLiteral("preview"), {ts});
  QVERIFY(!tsHits.isEmpty());
  QCOMPARE(tsHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  {
    QFile f(tmp.filePath(QStringLiteral("NOTES")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("loose notes\n");
  }
  QVERIFY(MimeMap::isProbablyText(tmp.filePath(QStringLiteral("NOTES")),
                                  QStringLiteral("application/octet-stream")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  const QVariantMap preview = host.readPreview(
      QUrl::fromLocalFile(txt.path), 1024);
  QVERIFY(preview.value(QStringLiteral("ok")).toBool());
  QVERIFY(preview.value(QStringLiteral("text")).toString().contains(
      QStringLiteral("hello text")));
}

void PeekOverlayTest::videoHandlerAndWebpRaster() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("clip.mp4")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a real mp4");
  }
  static const unsigned char kWebp[] = {
      0x52, 0x49, 0x46, 0x46, 0x40, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
      0x56, 0x50, 0x38, 0x20, 0x34, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00, 0x9d,
      0x01, 0x2a, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x34, 0x25, 0x98, 0x02,
      0x74, 0x01, 0x0e, 0xfe, 0x03, 0xc8, 0x00, 0x00, 0xfe, 0xe2, 0x9f, 0x61,
      0x0f, 0x3c, 0xf5, 0xbf, 0xc8, 0x2e, 0x6e, 0xa4, 0xa1, 0x1b, 0x3d, 0x0c,
      0x01, 0x32, 0x3f, 0xf0, 0x1f, 0xc4, 0xbf, 0xb0, 0x22, 0xb2, 0xb0, 0x00};
  {
    QFile f(tmp.filePath(QStringLiteral("tile.webp")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(reinterpret_cast<const char *>(kWebp), sizeof(kWebp));
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.video")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.image")));

  Manifest::Item vid;
  vid.path = tmp.filePath(QStringLiteral("clip.mp4"));
  vid.mime = QStringLiteral("video/mp4");
  const auto vidHits = registry.resolve(QStringLiteral("preview"), {vid});
  QVERIFY(!vidHits.isEmpty());
  QCOMPARE(vidHits.constFirst().id, QStringLiteral("synchro.preview.video"));

  Manifest::Item webp;
  webp.path = tmp.filePath(QStringLiteral("tile.webp"));
  webp.mime = QStringLiteral("image/webp");
  const auto webpHits = registry.resolve(QStringLiteral("preview"), {webp});
  QVERIFY(!webpHits.isEmpty());
  QCOMPARE(webpHits.constFirst().id, QStringLiteral("synchro.preview.image"));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  HandlerRegistry::Record rec =
      registry.handler(QStringLiteral("synchro.preview.video"));
  QQuickItem *preview = loader.create(
      &engine, rec, QStringLiteral("preview"), &host,
      QUrl::fromLocalFile(vid.path), {});
  QVERIFY2(preview, qPrintable(loader.lastError()));
  preview->deleteLater();

  const QUrl src = QUrl::fromLocalFile(webp.path);
  QVERIFY(QImage(webp.path).isNull());
  const QUrl raster = host.rasterUrl(src);
  if (raster == src)
    QSKIP("WebP raster fallback unavailable (no libwebp/ffmpeg decode)");
  QVERIFY(raster.isLocalFile());
  QVERIFY(raster.toLocalFile().endsWith(QStringLiteral(".png")));
  QVERIFY(!QImage(raster.toLocalFile()).isNull());
}

void PeekOverlayTest::spaceOnFolderDrillsAndEscReturns() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("photos")));
  QVERIFY(QDir(tmp.filePath(QStringLiteral("photos"))).mkdir(QStringLiteral("trip")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("photos/a.png"))));

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
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.folder")));

  Manifest::Item dir;
  dir.path = tmp.filePath(QStringLiteral("photos"));
  dir.mime = QStringLiteral("inode/directory");
  dir.isDir = true;
  const auto hits = registry.resolve(QStringLiteral("preview"), {dir});
  QVERIFY(!hits.isEmpty());
  QCOMPARE(hits.constFirst().id, QStringLiteral("synchro.preview.folder"));

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("photos"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  const QString rootPath = QFileInfo(tmp.path()).canonicalFilePath();

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() > 0;
      },
      3000));
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(), QStringLiteral("photos"));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int tripRow = findProxy(*peekProxy, QStringLiteral("trip"));
  QVERIFY(tripRow >= 0);
  peekProxy->setCurrentIndex(tripRow);
  keys.setGridMode(true);
  keys.setGridStride(4);
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(), QStringLiteral("photos"));
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QVERIFY(hostApi.folderPeek());
  peekProxy->setCurrentIndex(tripRow);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(hostApi.folderPath()).fileName() ==
               QStringLiteral("trip");
      },
      3000));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);

  hostApi.peekBack();
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(), QStringLiteral("photos"));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());
  QVERIFY(!hostApi.folderPeek());
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);
}

void PeekOverlayTest::folderPeekWasdUsesOwnStride() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("grid")));
  for (int i = 0; i < 16; ++i) {
    QFile f(tmp.filePath(QStringLiteral("grid/f%1.txt").arg(i, 2, 10,
                                                           QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x\n");
  }

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
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(keys.gridMode());
  QObject::connect(&keys, &KeyMachine::gridModeChanged, &hostApi, [&] {
    hostApi.setGridMode(keys.gridMode());
  });

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("grid"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() >= 16;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  peekProxy->setCurrentIndex(7);
  keys.setGridMode(true);
  keys.setGridStride(8);
  hostApi.setPeekGridStride(3);
  QCOMPARE(hostApi.peekGridStride(), 3);
  QVERIFY(keys.handleListKey(Qt::Key_W, Qt::NoModifier, QStringLiteral("w")));
  QCOMPARE(peekProxy->currentIndex(), 4);
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(peekProxy->currentIndex(), 7);
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QCOMPARE(peekProxy->currentIndex(), 6);
}

void PeekOverlayTest::folderPeekFileBackKeepsListingAndScroll() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  for (int i = 0; i < 40; ++i) {
    QFile f(tmp.filePath(QStringLiteral("docs/n%1.txt").arg(i, 2, 10,
                                                           QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("note\n");
  }

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
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("docs"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(keys.gridMode());
  QObject::connect(&keys, &KeyMachine::gridModeChanged, &hostApi, [&] {
    hostApi.setGridMode(keys.gridMode());
  });
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
  window->resize(800, 520);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderPeek());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return hostApi.folderListingItem() && pm && !pm->listing() &&
               pm->count() >= 40;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  peekProxy->setCurrentIndex(32);

  QObject *listing = hostApi.folderListingItem();
  QVERIFY(listing);
  auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("peekOverlay"));
  QVERIFY(overlay);
  QMetaObject::invokeMethod(overlay, "reparentPreview");

  QQuickItem *folderList = nullptr;
  QVERIFY(QTest::qWaitFor(
      [&] {
        folderList = qobject_cast<QQuickItem *>(listing)->findChild<QQuickItem *>(
            QStringLiteral("peekFolderList"));
        if (!folderList)
          folderList = window->findChild<QQuickItem *>(
              QStringLiteral("peekFolderList"));
        return folderList && folderList->height() > 40 &&
               folderList->property("contentHeight").toReal() > 200;
      },
      2000));
  folderList->setProperty("contentY", 160.0);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return qAbs(folderList->property("contentY").toReal() - 160.0) < 2.0;
      },
      1000));
  const qreal yBefore = folderList->property("contentY").toReal();

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor(
      [&] { return hostApi.isOpen() && !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.folderListingItem(), listing);
  QVERIFY(hostApi.folderPeek());
  QVERIFY(hostApi.peekFileName().endsWith(QStringLiteral(".txt")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *name = window->findChild<QQuickItem *>(
            QStringLiteral("peekFileName"));
        auto *idx = window->findChild<QQuickItem *>(
            QStringLiteral("peekFileIndex"));
        return name && name->isVisible() && idx && idx->isVisible() &&
               idx->width() > 8;
      },
      2000));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.folderListingItem(), listing);
  QCOMPARE(hostApi.previewItem(), listing);
  folderList = qobject_cast<QQuickItem *>(listing)->findChild<QQuickItem *>(
      QStringLiteral("peekFolderList"));
  QVERIFY(folderList);
  const qreal yAfter = folderList->property("contentY").toReal();
  QVERIFY2(qAbs(yAfter - yBefore) < 2.0,
           qPrintable(QStringLiteral("contentY jumped %1 -> %2")
                          .arg(yBefore)
                          .arg(yAfter)));
  auto *nameAfter =
      window->findChild<QQuickItem *>(QStringLiteral("peekFileName"));
  QVERIFY(!nameAfter || !nameAfter->isVisible());
}

void PeekOverlayTest::folderPeekSpaceOnFileReturnsListing() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  {
    QFile a(tmp.filePath(QStringLiteral("docs/alpha.txt")));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write("a\n");
    QFile b(tmp.filePath(QStringLiteral("docs/beta.txt")));
    QVERIFY(b.open(QIODevice::WriteOnly));
    b.write("b\n");
  }

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
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("docs"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() >= 2;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int fileRow = findProxy(*peekProxy, QStringLiteral("beta.txt"));
  QVERIFY(fileRow >= 0);
  peekProxy->setCurrentIndex(fileRow);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("beta.txt"));

  QSignalSpy listingSpy(&hostApi, &HostApi::folderPeekChanged);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(listingSpy.count() >= 1);

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.folderPeek());
  QVERIFY(!hostApi.isOpen());
}

void PeekOverlayTest::folderPeekStepsPastUnpreviewableFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("mix")));
  QVERIFY(QDir(tmp.filePath(QStringLiteral("mix"))).mkdir(QStringLiteral("subdir")));
  {
    QFile a(tmp.filePath(QStringLiteral("mix/aa.txt")));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write("one\n");
    QFile z(tmp.filePath(QStringLiteral("mix/mystery.7z")));
    QVERIFY(z.open(QIODevice::WriteOnly));
    z.write("7z\xbc\xaf\x27\x1cnot-a-real-7z");
    QFile b(tmp.filePath(QStringLiteral("mix/zz.txt")));
    QVERIFY(b.open(QIODevice::WriteOnly));
    b.write("two\n");
  }

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
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("mix"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return hostApi.folderListing() && pm && !pm->listing() &&
               pm->count() >= 4;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int aRow = findProxy(*peekProxy, QStringLiteral("aa.txt"));
  const int zipRow = findProxy(*peekProxy, QStringLiteral("mystery.7z"));
  const int zRow = findProxy(*peekProxy, QStringLiteral("zz.txt"));
  QVERIFY(aRow >= 0 && zipRow >= 0 && zRow >= 0);
  peekProxy->setCurrentIndex(aRow);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("aa.txt"));
  QVERIFY(hostApi.previewItem() != nullptr);

  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("mystery.7z"));
  QVERIFY2(!hostApi.folderListing(),
           "unpreviewable file must not kick back to folder listing");
  QVERIFY(hostApi.previewItem() == nullptr);

  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("zz.txt"));
  QVERIFY(!hostApi.folderListing());
  QVERIFY(hostApi.previewItem() != nullptr);
  QVERIFY(hostApi.previewItem() != hostApi.folderListingItem());
}

void PeekOverlayTest::filePeekAdHopsAndPreviewScrolls() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  auto writeLines = [&](const QString &name) {
    QFile f(tmp.filePath(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return false;
    for (int i = 0; i < 80; ++i)
      f.write(QStringLiteral("line %1 of %2\n").arg(i).arg(name).toUtf8());
    return true;
  };
  QVERIFY(writeLines(QStringLiteral("a.txt")));
  QVERIFY(writeLines(QStringLiteral("b.txt")));

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

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.txt"));
  const int b = findProxy(proxy, QStringLiteral("b.txt"));
  QVERIFY(a >= 0 && b >= 0);
  proxy.setCurrentIndex(qMin(a, b));
  const QString firstName = nameAt(proxy, proxy.currentIndex());

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  QVERIFY(!hostApi.peekPreviewFocused());

  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QVERIFY(hostApi.peekPreviewFocused());
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(nameAt(proxy, proxy.currentIndex()), firstName);

  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVariant consumed;
  QVERIFY(QMetaObject::invokeMethod(preview, "peekKey", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, consumed),
                                    Q_ARG(QVariant, int(Qt::Key_S)),
                                    Q_ARG(QVariant, int(Qt::NoModifier))));
  QVERIFY(consumed.toBool());

  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QVERIFY(proxy.currentIndex() != qMin(a, b));
  QVERIFY(hostApi.peekPreviewFocused());

  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QVERIFY(!hostApi.peekPreviewFocused());
}

void PeekOverlayTest::sqliteAndDuckdbHandlersResolve() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.sqlite")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.duckdb")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.archive")));

  Manifest::Item db;
  db.path = tmp.filePath(QStringLiteral("app.sqlite"));
  db.mime = QStringLiteral("application/vnd.sqlite3");
  const auto dbHits = registry.resolve(QStringLiteral("preview"), {db});
  QVERIFY(!dbHits.isEmpty());
  QCOMPARE(dbHits.constFirst().id, QStringLiteral("synchro.preview.sqlite"));

  Manifest::Item duck;
  duck.path = tmp.filePath(QStringLiteral("lake.duckdb"));
  duck.mime = QStringLiteral("application/x-duckdb");
  const auto duckHits = registry.resolve(QStringLiteral("preview"), {duck});
  QVERIFY(!duckHits.isEmpty());
  QCOMPARE(duckHits.constFirst().id, QStringLiteral("synchro.preview.duckdb"));

  Manifest::Item zip;
  zip.path = tmp.filePath(QStringLiteral("pack.zip"));
  zip.mime = QStringLiteral("application/zip");
  const auto zipHits = registry.resolve(QStringLiteral("preview"), {zip});
  QVERIFY(!zipHits.isEmpty());
  QCOMPARE(zipHits.constFirst().id, QStringLiteral("synchro.preview.archive"));

  sqlite3 *raw = nullptr;
  QVERIFY(sqlite3_open(QFile::encodeName(db.path).constData(), &raw) ==
          SQLITE_OK);
  QVERIFY(sqlite3_exec(raw,
                       "CREATE TABLE t(id INTEGER); INSERT INTO t VALUES (3);",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  const QVariantMap info = host.readDatabase(
      QUrl::fromLocalFile(db.path), QStringLiteral("sqlite"));
  QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
           qPrintable(info.value(QStringLiteral("error")).toString()));
  QCOMPARE(info.value(QStringLiteral("table")).toString(),
           QStringLiteral("t"));
}

void PeekOverlayTest::rootFilePeekShowsIndexAndQCloses() {
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

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  const int b = findProxy(proxy, QStringLiteral("b.png"));
  QVERIFY(a >= 0 && b >= 0);
  proxy.setCurrentIndex(qMin(a, b));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
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

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(!hostApi.folderPeek());
  QCOMPARE(hostApi.peekProxy(), static_cast<QObject *>(&proxy));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));

  auto *idx = window->findChild<QQuickItem *>(QStringLiteral("peekFileIndex"));
  QVERIFY(idx);
  QVERIFY(QTest::qWaitFor([&] { return idx->isVisible() && idx->width() > 0; },
                          2000));

  const QString first = hostApi.peekFileName();
  QVERIFY(!first.isEmpty());
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QVERIFY(hostApi.peekFileName() != first);

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void PeekOverlayTest::gridPeekIndexUsesThumbs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setGridMode(true);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  QVERIFY(a >= 0);
  proxy.setCurrentIndex(a);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(true);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.gridMode());

  auto *grid = window->findChild<QQuickItem *>(
      QStringLiteral("peekFileIndexGrid"));
  auto *list = window->findChild<QQuickItem *>(
      QStringLiteral("peekFileIndexList"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor([&] { return grid->isVisible() && grid->width() > 0; },
                          2000));
  QVERIFY(!list || !list->isVisible());
}

void PeekOverlayTest::emptyFolderShowsHintInRootAndPeek() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("hollow")));

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

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.filePath(QStringLiteral("hollow")));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.count(), 0);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
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

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto effectivelyVisible = [](QQuickItem *it) {
    for (QQuickItem *p = it; p; p = p->parentItem()) {
      if (!p->isVisible())
        return false;
    }
    return true;
  };
  QVERIFY(QTest::qWaitFor(
      [&] {
        const auto hints =
            window->findChildren<QQuickItem *>(QStringLiteral("emptyListing"));
        for (auto *h : hints) {
          if (effectivelyVisible(h) &&
              h->property("text").toString() == QStringLiteral("empty folder"))
            return true;
        }
        return false;
      },
      2000));

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("hollow"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing();
      },
      3000));

  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  auto *peekHint = preview->findChild<QQuickItem *>(
      QStringLiteral("peekEmptyListing"));
  if (!peekHint)
    peekHint = window->findChild<QQuickItem *>(
        QStringLiteral("peekEmptyListing"));
  QVERIFY2(peekHint, "folder peek surface should include EmptyListing");
  QVERIFY(QTest::qWaitFor(
      [&] {
        return peekHint->property("text").toString() ==
               QStringLiteral("empty folder");
      },
      3000));
}

void PeekOverlayTest::gridPeekIndexHidesFolders() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("keep")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setGridMode(true);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("a.png")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(true);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  auto *all = qobject_cast<QAbstractItemModel *>(hostApi.peekProxy());
  auto *files = qobject_cast<QAbstractItemModel *>(hostApi.peekFileProxy());
  QVERIFY(all);
  QVERIFY(files);
  QVERIFY(all->rowCount() >= 3);
  QCOMPARE(files->rowCount(), 2);
}

void PeekOverlayTest::peekEnterCommitsFileAndFolder() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("shot.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("shot.png")));

  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("peek-open"));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("docs")));
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderPeek());
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());
  QCOMPARE(QFileInfo(model.path()).fileName(), QStringLiteral("docs"));
}

void PeekOverlayTest::doLayerVerbsKeysAndCopyAs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString file = tmp.filePath(QStringLiteral("notes.md"));
  {
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hi\n");
  }

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
  QVERIFY(registry.contains(QStringLiteral("synchro.action.copy-as")));
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("notes.md"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);

  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::ControlModifier, QString()));
  QVERIFY2(hostApi.actionOpen(), qPrintable(hostApi.lastError()));
  QCOMPARE(hostApi.listHint(), QStringLiteral("Enter open · Ctrl+Enter do"));
  QVERIFY(hostApi.doCaption().contains(QStringLiteral("notes.md")));

  QStringList ids;
  for (const QVariant &rowVar : hostApi.doVerbs()) {
    const QVariantMap m = rowVar.toMap();
    ids.append(m.value(QStringLiteral("id")).toString());
  }
  QVERIFY(ids.contains(QStringLiteral("synchro.do.open")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.open-with")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.copy-as")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.trash")));
  QCOMPARE(ids.constFirst(), QStringLiteral("synchro.do.open"));
  QVERIFY(ids.indexOf(QStringLiteral("synchro.action.open-with")) <
          ids.indexOf(QStringLiteral("synchro.action.copy-as")));
  QCOMPARE(hostApi.doIndex(), 0);
  QCOMPARE(hostApi.doBriefTitle(), QStringLiteral("Open"));
  QVERIFY(!hostApi.doHasParams());
  QVERIFY2(hostApi.doPreviewItem(),
           "markdown should mount a peek preview in the do content box");
  QVERIFY(!hostApi.doTargetIsDir());
  QVERIFY(hostApi.doMosaicUrl().isEmpty());

  const int listing = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(proxy.currentIndex(), listing);
  QCOMPARE(hostApi.doIndex(), 1);
  QVERIFY(hostApi.doHasParams());
  QVERIFY(hostApi.actionItem());

  QVERIFY(hostApi.openDoLayer(QStringLiteral("synchro.action.copy-as")));
  QCOMPARE(hostApi.doBriefTitle(), QStringLiteral("Copy path"));
  QVERIFY(hostApi.doHasParams());
  QVERIFY(hostApi.actionItem());
  QVERIFY2(hostApi.runDoVerb(), qPrintable(hostApi.lastError()));
  QVERIFY(!hostApi.actionOpen());
  QCOMPARE(QGuiApplication::clipboard()->text(), file);

  QVERIFY(hostApi.openDoLayer());
  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.actionOpen());
  QCOMPARE(proxy.currentIndex(), listing);
}

void PeekOverlayTest::doLayerOverlaySplitChrome() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

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
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("a.png")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  QVERIFY2(hostApi.openDoLayer(QStringLiteral("synchro.action.open-with")),
           qPrintable(hostApi.lastError()));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.actionOpen(); }, 2000));
  auto *frame = window->findChild<QQuickItem *>(QStringLiteral("doOverlay"));
  QVERIFY(frame);
  QVERIFY(frame->isVisible());
  auto *brief = window->findChild<QQuickItem *>(QStringLiteral("doBriefTitle"));
  QVERIFY(brief);
  QCOMPARE(brief->property("text").toString(), QStringLiteral("Open with…"));
  QVERIFY2(hostApi.doHasParams(), qPrintable(hostApi.lastError()));
  auto *params = hostApi.actionItem();
  QVERIFY2(params, qPrintable(hostApi.lastError().isEmpty()
                                 ? QStringLiteral("no mounted params QML")
                                 : hostApi.lastError()));
  QVERIFY(params->findChild<QQuickItem *>(QStringLiteral("openWithList")));
  auto *surface =
      window->findChild<QQuickItem *>(QStringLiteral("doParamSurface"));
  QVERIFY(surface);
  QVERIFY(QTest::qWaitFor([&] { return params->parentItem() == surface; },
                          2000));
  QVERIFY2(hostApi.doPreviewItem(),
           "png should keep a file preview above the params strip");
  auto *content =
      window->findChild<QQuickItem *>(QStringLiteral("doContentSurface"));
  QVERIFY(content);
  QVERIFY(QTest::qWaitFor(
      [&] { return hostApi.doPreviewItem()->parentItem() == content; }, 2000));
}

void PeekOverlayTest::doLayerShowsFilePreviewAndFolderMosaic() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("album")));
  const QString album = tmp.filePath(QStringLiteral("album"));
  QVERIFY(writePng(QDir(album).filePath(QStringLiteral("one.png"))));
  QVERIFY(writePng(QDir(album).filePath(QStringLiteral("two.png"))));
  {
    QFile f(tmp.filePath(QStringLiteral("notes.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# hi\n");
  }

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
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("notes.md")));
  QVERIFY2(hostApi.openDoLayer(), qPrintable(hostApi.lastError()));
  QVERIFY(hostApi.doPreviewItem());
  QVERIFY(!hostApi.doTargetIsDir());
  QVERIFY(hostApi.doMosaicUrl().isEmpty());
  hostApi.closeAction();

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("album")));
  QVERIFY2(hostApi.openDoLayer(), qPrintable(hostApi.lastError()));
  QVERIFY(hostApi.doTargetIsDir());
  QVERIFY(hostApi.doPreviewItem() == nullptr);
  QVERIFY2(!hostApi.doMosaicUrl().isEmpty(),
           "folder do-layer should paint a large mosaic, not a peek listing");
  QVERIFY(hostApi.doMosaicUrl().startsWith(QStringLiteral("image://synchrothumb/")));
  QCOMPARE(hostApi.doTargetName(), QStringLiteral("album"));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  PeekOverlayTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "peek_overlay_test.moc"
