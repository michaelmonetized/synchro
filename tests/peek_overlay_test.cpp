#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "FileCatalog.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "SearchModel.h"
#include "SearchService.h"
#include "SelectionModel.h"
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

class ScopedEnvironment {
public:
  explicit ScopedEnvironment(const QByteArray &name)
      : m_name(name), m_hadValue(qEnvironmentVariableIsSet(name.constData())),
        m_value(qgetenv(name.constData())) {}
  ~ScopedEnvironment() {
    if (m_hadValue)
      qputenv(m_name.constData(), m_value);
    else
      qunsetenv(m_name.constData());
  }

private:
  QByteArray m_name;
  bool m_hadValue = false;
  QByteArray m_value;
};

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

void collectVisual(QQuickItem *root, QVector<QQuickItem *> *out) {
  if (!root)
    return;
  out->append(root);
  const auto kids = root->childItems();
  for (QQuickItem *child : kids)
    collectVisual(child, out);
}

QVector<QQuickItem *> visualNamed(QQuickItem *root, const QString &name) {
  QVector<QQuickItem *> all;
  collectVisual(root, &all);
  QVector<QQuickItem *> hit;
  for (QQuickItem *item : all) {
    if (item->objectName() == name)
      hit.append(item);
  }
  return hit;
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
  void doLayerShowsFilePreviewAndFolderGrid();
  void volumesListingChromeUrls();
  void volumesListingChromeVisible();
  void pathBarTabsSitAboveCommandField();
  void fileGridCellsFillWidth();
  void searchGridCellsMatchRows();
  void searchGridDropsFolderTiles();
  void searchGridVirtualizesGroups();
  void fileGridFollowsProxySort();
  void contextualPanelRelevanceFollowsSelection();
  void panelLookUsesSafeInlineHandlersAndAsyncReads();
  void findInFilePastDefaultWindow();
  void textPeekFindCyclesHits();
  void textPeekFindJumpsPastWindow();
  void textPeekFindKeepsNewlines();
  void textPeekFindKeepsSyntaxColors();
  void contentSearchPeekOpensFind();
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

  const auto tooltips =
      visualNamed(window->contentItem(), QStringLiteral("tooltipOverlay"));
  QVERIFY(!tooltips.isEmpty());
  for (QQuickItem *tooltip : tooltips)
    QCOMPARE(tooltip->parentItem(), window->contentItem());

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  auto *overlay = window->findChild<QQuickItem *>(QStringLiteral("peekOverlay"));
  QVERIFY(overlay);

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));

  QTest::keyClick(window, Qt::Key_Space);
  QVERIFY(QTest::qWaitFor([&] { return hostApi.isOpen(); }, 2000));
  QVERIFY(overlay->isVisible());
  auto *frame = window->findChild<QQuickItem *>(QStringLiteral("peekPanel"));
  auto *keyline = window->findChild<QQuickItem *>(
      QStringLiteral("peekModalKeyline"));
  auto *blocker = window->findChild<QQuickItem *>(
      QStringLiteral("peekModalBlocker"));
  QVERIFY(frame);
  QVERIFY(keyline);
  QVERIFY(blocker);
  QVERIFY(blocker->isVisible());
  QCOMPARE(keyline->width(), frame->width());
  QCOMPARE(keyline->height(), frame->height());
  QVERIFY(frame->x() > 0);
  QVERIFY(frame->x() + frame->width() < overlay->width());
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          2000));
  auto *fileBackground = window->findChild<QQuickItem *>(
      QStringLiteral("peekFileSurfaceBackground"));
  QVERIFY(fileBackground);
  QVERIFY(fileBackground->isVisible());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !visualNamed(window->contentItem(),
                            QStringLiteral("peekIndexSelection")).isEmpty();
      },
      1000));
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

void PeekOverlayTest::panelLookUsesSafeInlineHandlersAndAsyncReads() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString textPath = tmp.filePath(QStringLiteral("notes.txt"));
  {
    QFile f(textPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ambient preview\n");
  }
  {
    QFile f(tmp.filePath(QStringLiteral("bundle.zip")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not really a zip");
  }
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child-folder")));
  {
    QFile f(tmp.filePath(QStringLiteral("child-folder/inside.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("inside the lens\n");
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

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

  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.terminal"),
                                      QStringLiteral("preview")));
  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.sql"),
                                      QStringLiteral("preview")));
  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.duckdb"),
                                      QStringLiteral("preview")));

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int textRow = findProxy(proxy, QStringLiteral("notes.txt"));
  const int zipRow = findProxy(proxy, QStringLiteral("bundle.zip"));
  const int folderRow = findProxy(proxy, QStringLiteral("child-folder"));
  QVERIFY(textRow >= 0);
  QVERIFY(zipRow >= 0);
  QVERIFY(folderRow >= 0);

  proxy.setCurrentIndex(textRow);
  host.setInlinePreviewActive(true);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("text"));
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.text"));
  QVERIFY(!host.inlinePreviewItem());

  QSignalSpy ready(&host, &HostApi::previewReady);
  const quint64 request =
      host.requestPreview(QUrl::fromLocalFile(textPath), 1024, 0);
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (const QList<QVariant> &args : ready) {
          if (args.at(0).toULongLong() == request)
            return true;
        }
        return false;
      },
      2000));
  QList<QVariant> result;
  for (const QList<QVariant> &args : ready) {
    if (args.at(0).toULongLong() == request) {
      result = args;
      break;
    }
  }
  QVERIFY(result.at(2).toMap().value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("ambient preview")));

  proxy.setCurrentIndex(zipRow);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("card"));
  QVERIFY(!host.inlinePreviewItem());
  QVERIFY(host.inlinePreviewHandler().isEmpty());

  proxy.setCurrentIndex(folderRow);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("folder"));
  auto *folderModel =
      qobject_cast<DirectoryModel *>(host.inlineFolderModel());
  auto *folderProxy = qobject_cast<FilterProxy *>(host.inlineFolderProxy());
  QVERIFY(folderModel);
  QVERIFY(folderProxy);
  QVERIFY(QTest::qWaitFor(
      [&] { return !folderModel->listing() && folderProxy->count() == 1; },
      2000));
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("inside.txt"));
  QVERIFY(host.commitInlineFolderRow(0));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(model.path()).canonicalFilePath() ==
               QFileInfo(tmp.filePath(QStringLiteral("child-folder")))
                   .canonicalFilePath();
      },
      2000));

  // Aggregate rows are query-backed folders too. The Look surface runs the
  // generated drill SQL asynchronously, and a newer cursor wins even if an
  // older query finishes later.
  ScopedEnvironment restoreHome(QByteArrayLiteral("SYNCHRO_HOME"));
  QTemporaryDir catalogHome;
  QVERIFY(catalogHome.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(catalogHome.path()));
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  FileCatalog catalog(&model);
  host.setFileCatalog(&catalog);
  catalog.scanTree(tmp.path(), 100);
  QVERIFY(QTest::qWaitFor([&] { return !catalog.indexing(); }, 10000));
  const QVariantMap groups = FileCatalog::querySync(
      QStringLiteral("select extension,count(*) as files from selection "
                     "where not is_dir group by extension order by extension"),
      tmp.path(),
      {textPath, tmp.filePath(QStringLiteral("bundle.zip")),
       tmp.filePath(QStringLiteral("child-folder/inside.txt"))});
  QVERIFY2(groups.value(QStringLiteral("ok")).toBool(),
           qPrintable(groups.value(QStringLiteral("error")).toString()));
  model.showSqlResult(groups, QStringLiteral("types"));
  const int textGroup = findProxy(proxy, QStringLiteral("txt"));
  const int zipGroup = findProxy(proxy, QStringLiteral("zip"));
  QVERIFY(textGroup >= 0);
  QVERIFY(zipGroup >= 0);

  proxy.setCurrentIndex(textGroup);
  host.refreshInlinePreview();
  proxy.setCurrentIndex(zipGroup);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("folder"));
  QVERIFY(host.inlineFolderLoading());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !host.inlineFolderLoading() && folderProxy->count() == 1;
      },
      10000));
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("bundle.zip"));
  QTest::qWait(100);
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("bundle.zip"));
  host.setFileCatalog(nullptr);

  host.setInlinePreviewActive(false);
  QVERIFY(host.inlinePreviewMode().isEmpty());
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
  QVERIFY(hostApi.doFolderProxy() == nullptr);

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

void PeekOverlayTest::doLayerShowsFilePreviewAndFolderGrid() {
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
  QVERIFY(hostApi.doFolderProxy() == nullptr);
  hostApi.closeAction();

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("album")));
  bool sawFolderProxy = false;
  QObject::connect(&hostApi, &HostApi::doPreviewChanged, &hostApi, [&] {
    if (hostApi.doFolderProxy())
      sawFolderProxy = true;
  });
  QVERIFY2(hostApi.openDoLayer(), qPrintable(hostApi.lastError()));
  QVERIFY2(sawFolderProxy,
           "doFolderProxy must be visible on doPreviewChanged so QML is not stuck");
  QVERIFY(hostApi.doTargetIsDir());
  QVERIFY(hostApi.doPreviewItem() == nullptr);
  auto *folder = qobject_cast<FilterProxy *>(hostApi.doFolderProxy());
  QVERIFY2(folder, "folder do-layer should list the folder, not a mosaic");
  QVERIFY(QTest::qWaitFor([&] { return folder->rowCount() >= 2; }, 2000));
  QVERIFY(findProxy(*folder, QStringLiteral("one.png")) >= 0);
  QVERIFY(findProxy(*folder, QStringLiteral("two.png")) >= 0);
  QCOMPARE(hostApi.doTargetName(), QStringLiteral("album"));
}

void PeekOverlayTest::volumesListingChromeUrls() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  QVERIFY(hostApi.listingRowUrl().isEmpty());
  model.setPath(QStringLiteral("volumes://"));
  QVERIFY(hostApi.listingRowUrl().isValid());
  QVERIFY(hostApi.listingRowUrl().toLocalFile().endsWith(
      QStringLiteral("Row.qml")));
  QVERIFY(hostApi.listingThumbUrl().toLocalFile().endsWith(
      QStringLiteral("Thumb.qml")));
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(hostApi.listingRowUrl().isEmpty());
}

void PeekOverlayTest::volumesListingChromeVisible() {
  QCOMPARE(int(SearchModel::PercentRole), int(DirectoryModel::PercentRole));
  QCOMPARE(int(SearchModel::DetailRole), int(DirectoryModel::DetailRole));

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
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

  model.setPath(QStringLiteral("volumes://"));
  QVERIFY(hostApi.listingRowUrl().isValid());
  QVERIFY(model.rowCount() > 0);

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  QVERIFY2(QTest::qWaitFor(
               [&] {
                 const auto chromes =
                     visualNamed(list, QStringLiteral("listingRowChrome"));
                 for (QQuickItem *chrome : chromes) {
                   if (chrome->isVisible() &&
                       chrome->property("percent").toInt() >= 0 &&
                       chrome->property("detail")
                           .toString()
                           .contains(QStringLiteral("free")))
                     return true;
                 }
                 return false;
               },
               2000),
           "volumes:// rows must bind percent/detail so the bar paints");
}

void PeekOverlayTest::pathBarTabsSitAboveCommandField() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(480, 360);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *bar = window->findChild<QQuickItem *>(QStringLiteral("pathBar"));
  auto *crumbs = window->findChild<QQuickItem *>(QStringLiteral("pathCrumbs"));
  auto *tabs = window->findChild<QQuickItem *>(QStringLiteral("locationTabs"));
  auto *field = window->findChild<QQuickItem *>(QStringLiteral("commandField"));
  QVERIFY(bar);
  QVERIFY(crumbs);
  QVERIFY(tabs);
  QVERIFY(field);
  QVERIFY(crumbs->y() + crumbs->height() <= tabs->y() + 1);
  QVERIFY(bar->y() + bar->height() <= field->y() + 1);
  QVERIFY(tabs->y() + tabs->height() <= field->y() + 1);
  QVERIFY(crumbs->width() > window->width() * 0.6);
}

void PeekOverlayTest::fileGridCellsFillWidth() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(733, 500);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor([&] { return grid->isVisible() && grid->width() > 0; },
                          1000));
  const int cols = grid->property("columns").toInt();
  const qreal cell = grid->property("cellWidth").toReal();
  QVERIFY(cols >= 1);
  QVERIFY(cell > 0);
  QCOMPARE(qRound(cell * cols), qRound(grid->width()));

  window->resize(501, 500);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return qAbs(grid->property("cellWidth").toReal() *
                        grid->property("columns").toInt() -
                    grid->width()) < 1.0;
      },
      1000));

  window->resize(1900, 700);
  QVERIFY(QTest::qWaitFor([&] { return grid->width() > 1800; }, 1000));
  const qreal layout = grid->property("layoutWidth").toReal();
  QVERIFY(layout > 0);
  QVERIFY(layout < grid->width());
  QVERIFY(qAbs(grid->property("cellWidth").toReal() *
                   grid->property("columns").toInt() -
               layout) < 1.0);
}

void PeekOverlayTest::searchGridCellsMatchRows() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("b")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("root.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a/alpha.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a/beta.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b/zeta.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() >= 3;
      },
      2000));

  // Stream hits into an already-visible grouped grid (creation-order
  // tiles used to stay stale while selection walked sorted indexes).
  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("png"), tmp.path(), false);

  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *groups = window->findChild<QQuickItem *>(
            QStringLiteral("searchGridGroups"));
        return !search.listing() && groups && groups->isVisible() &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() == 4;
      },
      5000));
  auto *groups =
      window->findChild<QQuickItem *>(QStringLiteral("searchGridGroups"));
  QVERIFY(groups);
  QCOMPARE(model.count(), 4);

  const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
  QCOMPARE(cells.size(), 4);
  QVector<int> seen;
  for (QQuickItem *cell : cells) {
    const int row = cell->property("rowIndex").toInt();
    QVERIFY(row >= 0 && row < model.count());
    QVERIFY(!seen.contains(row));
    seen.append(row);
    QCOMPARE(cell->property("name").toString(),
             model.data(model.index(row, 0), DirectoryModel::NameRole)
                 .toString());
    QCOMPARE(cell->property("path").toString(),
             model.data(model.index(row, 0), DirectoryModel::PathRole)
                 .toString());
  }
  std::sort(seen.begin(), seen.end());
  QCOMPARE(seen, (QVector<int>{0, 1, 2, 3}));

  const QString thumb =
      QStringLiteral("image://synchrothumb/search-grid-test");
  const QString path0 =
      model.data(model.index(0, 0), DirectoryModel::PathRole).toString();
  search.setThumbnail(path0, thumb);
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (QQuickItem *cell : visualNamed(grid, QStringLiteral("gridCell"))) {
          if (cell->property("rowIndex").toInt() == 0)
            return cell->property("thumbnail").toString() == thumb;
        }
        return false;
      },
      1000));

  model.setCurrentIndex(2);
  QCOMPARE(model.currentName(),
           model.data(model.index(2, 0), DirectoryModel::NameRole).toString());
}

void PeekOverlayTest::searchGridDropsFolderTiles() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("keep-me.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("unrelated-folder-tile.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() == 2;
      },
      2000));

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("keep-me"), tmp.path(), false);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
        return !search.listing() && model.count() == 1 &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               window->findChild<QQuickItem *>(
                   QStringLiteral("searchGridGroups")) &&
               cells.size() == 1 &&
               cells.constFirst()->property("name").toString() ==
                   QStringLiteral("keep-me.png");
      },
      5000));

  const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
  QCOMPARE(cells.size(), 1);
  QCOMPARE(cells.constFirst()->property("name").toString(),
           QStringLiteral("keep-me.png"));
  for (QQuickItem *cell : cells) {
    QVERIFY(cell->property("name").toString() !=
            QStringLiteral("unrelated-folder-tile.png"));
  }
}

void PeekOverlayTest::searchGridVirtualizesGroups() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString listing = tmp.filePath(QStringLiteral("cwd"));
  QVERIFY(QDir().mkpath(listing));
  for (int i = 0; i < 48; ++i) {
    const QString dir =
        tmp.filePath(QStringLiteral("hits/g%1").arg(i, 2, 10, QChar('0')));
    QVERIFY(QDir().mkpath(dir));
    QFile f(dir + QStringLiteral("/synchrohit.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("x") == 1);
  }

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(listing);
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 520);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(QStringLiteral("fileGridTiles"));
      },
      2000));

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("synchrohit"), tmp.path(), false);
  QVERIFY2(QTest::qWaitFor(
               [&] { return !search.listing() && model.count() == 48; }, 8000),
           qPrintable(QStringLiteral("listing=%1 count=%2")
                          .arg(search.listing())
                          .arg(model.count())));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(
                   QStringLiteral("searchGridGroups")) &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles"));
      },
      2000));

  auto *groups =
      window->findChild<QQuickItem *>(QStringLiteral("searchGridGroups"));
  QVERIFY(groups);
  auto *content = groups->property("contentItem").value<QQuickItem *>();
  QVERIFY(content);
  int delegates = 0;
  for (QQuickItem *child : content->childItems()) {
    if (child && child->width() > 0 && child->height() > 0)
      ++delegates;
  }
  QVERIFY2(delegates > 0 && delegates < 30,
           qPrintable(QStringLiteral("search instantiated %1 group delegates "
                                     "(expected viewport-sized)")
                          .arg(delegates)));
  QVERIFY(visualNamed(grid, QStringLiteral("gridCell")).size() < 30);
}

void PeekOverlayTest::contextualPanelRelevanceFollowsSelection() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile db(tmp.filePath(QStringLiteral("sample.duckdb")));
    QVERIFY(db.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(db.write("DUCK", 4) == 4);
    QFile note(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(note.write("plain", 5) == 5);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
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
  const int dbRow = findProxy(proxy, QStringLiteral("sample.duckdb"));
  const int noteRow = findProxy(proxy, QStringLiteral("notes.txt"));
  QVERIFY(dbRow >= 0);
  QVERIFY(noteRow >= 0);
  proxy.setCurrentIndex(dbRow);
  SelectionModel selection(&proxy, &model);

  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               nullptr);
  host.setSelection(&selection);
  const auto hasPanel = [&](const QString &id) {
    const QVariantList panels = host.relevantPanels();
    for (const QVariant &panel : panels) {
      if (panel.toMap().value(QStringLiteral("id")).toString() == id)
        return true;
    }
    return false;
  };

  QVERIFY(hasPanel(QStringLiteral("synchro.panel.terminal")));
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.duckdb")));

  selection.ctrlClick(dbRow);
  QCOMPARE(selection.selectedCount(), 0);
  QVERIFY(!hasPanel(QStringLiteral("synchro.panel.duckdb")));
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.terminal")));

  selection.click(noteRow);
  QCOMPARE(selection.selectedCount(), 1);
  QVERIFY(!hasPanel(QStringLiteral("synchro.panel.duckdb")));

  selection.click(dbRow);
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.duckdb")));
}

void PeekOverlayTest::fileGridFollowsProxySort() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("z.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("asc"));
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(proxy.data(proxy.index(0, 0), DirectoryModel::NameRole).toString(),
           QStringLiteral("a.png"));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"),
                                           &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] { return visualNamed(grid, QStringLiteral("gridCell")).size() == 2; },
      2000));

  QString name0;
  for (QQuickItem *cell : visualNamed(grid, QStringLiteral("gridCell"))) {
    if (cell->property("rowIndex").toInt() == 0)
      name0 = cell->property("name").toString();
  }
  QCOMPARE(name0, QStringLiteral("a.png"));

  proxy.selectRow(0);
  QCOMPARE(proxy.currentName(), QStringLiteral("a.png"));
  QCOMPARE(model.currentName(), QStringLiteral("a.png"));
}

void PeekOverlayTest::findInFilePastDefaultWindow() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("big.txt"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("head token\n") > 0);
    QByteArray pad(70 * 1024, 'x');
    QVERIFY(f.write(pad) == pad.size());
    QVERIFY(f.write("\nFINDME_TOKEN in the tail\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);

  const QUrl url = QUrl::fromLocalFile(path);
  const QVariantList hits = host.findInFile(url, QStringLiteral("FINDME_TOKEN"));
  QCOMPARE(hits.size(), 1);
  const QVariantMap hit = hits.at(0).toMap();
  QVERIFY(hit.value(QStringLiteral("offset")).toLongLong() > 65536);
  QCOMPARE(hit.value(QStringLiteral("line")).toInt(), 3);

  const QVariantMap head = host.readPreview(url, 65536, 0);
  QVERIFY(head.value(QStringLiteral("ok")).toBool());
  QVERIFY(!head.value(QStringLiteral("text")).toString().contains(
      QStringLiteral("FINDME_TOKEN")));

  const QVariantMap mid = host.readPreview(
      url, 65536, hit.value(QStringLiteral("offset")).toLongLong() - 64);
  QVERIFY(mid.value(QStringLiteral("ok")).toBool());
  QVERIFY(mid.value(QStringLiteral("text")).toString().contains(
      QStringLiteral("FINDME_TOKEN")));
  QVERIFY(mid.value(QStringLiteral("startByte")).toLongLong() > 0);

  const QVariantList both = host.findInFile(url, QStringLiteral("token"));
  QCOMPARE(both.size(), 2);
}

void PeekOverlayTest::textPeekFindCyclesHits() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("alpha zzhit\nbeta\nzzhit again\nnope\n") > 0);
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
  const int row = findProxy(proxy, QStringLiteral("notes.txt"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

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
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QVERIFY(hostApi.peekPreviewFocused());

  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findOpen").toBool(); }, 1000));
  preview->setProperty("findQuery", QStringLiteral("zzhit"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() >= 2; }, 1000));
  QCOMPARE(preview->property("findCount").toInt(), 2);
  QCOMPARE(preview->property("findIndex").toInt(), 0);

  QVariant consumed;
  QVERIFY(QMetaObject::invokeMethod(
      preview, "peekKey", Qt::DirectConnection, Q_RETURN_ARG(QVariant, consumed),
      Q_ARG(QVariant, int(Qt::Key_N)), Q_ARG(QVariant, int(Qt::NoModifier))));
  QVERIFY(consumed.toBool());
  QCOMPARE(preview->property("findIndex").toInt(), 1);

  QVERIFY(QMetaObject::invokeMethod(
      preview, "peekKey", Qt::DirectConnection, Q_RETURN_ARG(QVariant, consumed),
      Q_ARG(QVariant, int(Qt::Key_N)),
      Q_ARG(QVariant, int(Qt::ShiftModifier))));
  QVERIFY(consumed.toBool());
  QCOMPARE(preview->property("findIndex").toInt(), 0);

  QVERIFY(keys.handleListKey(Qt::Key_N, Qt::NoModifier, QStringLiteral("n")));
  QCOMPARE(preview->property("findIndex").toInt(), 1);

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(hostApi.isOpen());
  QVERIFY(!preview->property("findOpen").toBool());
  QCOMPARE(preview->property("findCount").toInt(), 0);
}

void PeekOverlayTest::textPeekFindJumpsPastWindow() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("log.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("start\n") > 0);
    QVERIFY(f.write(QByteArray(70 * 1024, 'x')) == 70 * 1024);
    QVERIFY(f.write("\nTAIL_UNIQUE_HIT here\n") > 0);
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
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("log.txt")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  const QVariantMap head = preview->property("preview").toMap();
  QVERIFY(!head.value(QStringLiteral("text")).toString().contains(
      QStringLiteral("TAIL_UNIQUE_HIT")));

  preview->setProperty("findQuery", QStringLiteral("TAIL_UNIQUE_HIT"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  QCOMPARE(preview->property("findIndex").toInt(), 0);
  QVERIFY(preview->property("viewStart").toInt() > 0);
  const QVariantMap mid = preview->property("preview").toMap();
  QVERIFY(mid.value(QStringLiteral("text")).toString().contains(
      QStringLiteral("TAIL_UNIQUE_HIT")));
}

void PeekOverlayTest::textPeekFindKeepsNewlines() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("lines.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("one\ntwo HIT\nthree\n") > 0);
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
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("lines.txt")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  preview->setProperty("findQuery", QStringLiteral("HIT"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  auto *body = preview->findChild<QQuickItem *>(QStringLiteral("textPeekBody"));
  QVERIFY(body);
  QVERIFY(QTest::qWaitFor(
      [&] { return body->implicitHeight() > 48; }, 1000));
  QVERIFY2(body->implicitHeight() > 48,
           qPrintable(QString::number(body->implicitHeight())));
}

void PeekOverlayTest::textPeekFindKeepsSyntaxColors() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("sample.py")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("def foo():\n    return \"synchro_hl_token\"\n") > 0);
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
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("sample.py")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  const QVariantMap head = preview->property("preview").toMap();
  if (!head.value(QStringLiteral("highlighted")).toBool())
    QSKIP("preview has no syntax HTML");

  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  preview->setProperty("findQuery", QStringLiteral("synchro_hl_token"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  auto *body = preview->findChild<QQuickItem *>(QStringLiteral("textPeekBody"));
  QVERIFY(body);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const QString t = body->property("text").toString();
        return t.contains(QStringLiteral("background-color:")) &&
               t.contains(QStringLiteral("color:"));
      },
      1000));
  const QString html = body->property("text").toString();
  QVERIFY2(html.contains(QStringLiteral("color:")), qPrintable(html.left(240)));
  QVERIFY2(html.contains(QStringLiteral("background-color:")),
           qPrintable(html.left(240)));
}

void PeekOverlayTest::contentSearchPeekOpensFind() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("alpha\nsynchro_deeplink_token here\nbeta\n") > 0);
  }

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
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

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("synchro_deeplink_token"), tmp.path(), false,
               true);
  QVERIFY(QTest::qWaitFor(
      [&] { return !search.listing() && search.count() == 1; }, 5000));
  QVERIFY(keys.statusMessage().contains(QStringLiteral("content matches")));

  // Search completion/status is contextual. Returning to a normal folder
  // must not leave the last result count in the browser chrome.
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(keys.statusMessage().isEmpty());

  model.setPath(QStringLiteral("search://"));
  QCOMPARE(model.isContentSearch(), true);
  proxy.setCurrentIndex(0);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QCOMPARE(hostApi.peekFindQuery(), QStringLiteral("synchro_deeplink_token"));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; },
                          3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return preview->property("findOpen").toBool() &&
               preview->property("findQuery").toString() ==
                   QStringLiteral("synchro_deeplink_token") &&
               preview->property("findCount").toInt() >= 1;
      },
      3000));
  QCOMPARE(preview->property("findIndex").toInt(), 0);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  PeekOverlayTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "peek_overlay_test.moc"
