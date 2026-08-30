#include "AgentBridge.h"
#include "DirectoryModel.h"
#include "FileCatalog.h"
#include "FsnLayout.h"
#include "HotSetWatcher.h"
#include "SearchApi.h"
#include "ThumbCache.h"
#include "ThumbnailService.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <sqlite3.h>

namespace {

int catalogRows() {
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(QFile::encodeName(FileCatalog::dbPath()).constData(), &db,
                      SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return -1;
  }
  sqlite3_stmt *st = nullptr;
  int count = -1;
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM files;", -1, &st, nullptr) ==
          SQLITE_OK &&
      sqlite3_step(st) == SQLITE_ROW)
    count = sqlite3_column_int(st, 0);
  if (st)
    sqlite3_finalize(st);
  sqlite3_close(db);
  return count;
}

bool waitListing(DirectoryModel &model) {
  QElapsedTimer timer;
  timer.start();
  while (model.listing() && timer.elapsed() < 5000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return !model.listing();
}

} // namespace

class FileCatalogTest : public QObject {
  Q_OBJECT

private slots:
  void currentFolderIsQueryable();
  void sourceRelationIgnoresCommentsAndStrings();
  void derivedSizeAndHiddenFieldsAreQueryable();
  void derivedNavigationFieldsAreQueryable();
  void deterministicImageFactsAreQueryable();
  void repeatedImageFactsOnlyInvalidateActualChanges();
  void projectRelationClassifiesMarkers();
  void headlessQueryReportsScopeCoverage();
  void headlessQueryNeedsNoCatalogWriteAccess();
  void nativeShadowIsAtomicAndDeliberatelyLagged();
  void mcpListsAndCallsCatalogTools();
  void querySurfaceRejectsWrites();
  void scanBuildsTreeRelation();
  void catalogSceneBuildsEnrichedHierarchy();
  void cappedScanReportsIncomplete();
  void incrementalScanPublishesQueryableBatches();
  void completedScanStateSurvivesRelaunch();
  void legacyCatalogStateIsAdopted();
  void legacySchemaMigratesWithoutRowLoss();
  void trigramSearchTracksCatalogMutations();
  void launcherSearchUsesAndTerms();
  void launcherSearchPrioritizesSavedLocations();
  void launcherSearchExportsWarmThumbnail();
  void launcherContentSearchFindsLiteralText();
  void aggregateRowsExposeDrillQueries();
  void incrementalRefreshReconcilesExternalMove();
  void rescanPrunesMissingRows();
  void hotSetWatcherCoalescesAndReconciles();
  void operationalSearchApiIsAsyncFilteredAndVersioned();
};

void FileCatalogTest::operationalSearchApiIsAsyncFilteredAndVersioned() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  for (const QString &name : {QStringLiteral("alpha-one.pdf"),
                              QStringLiteral("alpha-two.pdf"),
                              QStringLiteral("alpha-note.txt")}) {
    QFile file(root.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("alpha payload\n");
  }
  QVERIFY(QDir(root.path()).mkpath(QStringLiteral("nested")));
  QFile nested(root.filePath(QStringLiteral("nested/alpha-inside.pdf")));
  QVERIFY(nested.open(QIODevice::WriteOnly));
  nested.write("alpha nested payload\n");
  nested.close();
  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 5,
                           10000);

  SearchApi api;
  const QVariantMap description = api.describe();
  QCOMPARE(description.value(QStringLiteral("apiVersion")).toInt(), 1);
  QVERIFY(description.value(QStringLiteral("cancellation")).toBool());
  QVERIFY(description.value(QStringLiteral("pagination")).toBool());
  QVERIFY(!description.value(QStringLiteral("contentIndexed")).toBool());

  if (QDBusConnection::sessionBus().isConnected()) {
    const QString service =
        QStringLiteral("org.omarchy.Synchro.Search1.test%1")
            .arg(QCoreApplication::applicationPid());
    QVERIFY2(api.start(QDBusConnection::sessionBus(), service),
             qPrintable(api.lastError()));
    QVERIFY(QDBusConnection::sessionBus()
                .interface()
                ->isServiceRegistered(service));
  }

  QSignalSpy ready(&api, &SearchApi::resultsReady);
  const QVariantMap options{{QStringLiteral("cwd"), root.path()},
                            {QStringLiteral("scope"), QStringLiteral("cwd")},
                            {QStringLiteral("kind"), QStringLiteral("file")},
                            {QStringLiteral("extension"), QStringLiteral("pdf")},
                            {QStringLiteral("limit"), 1}};
  const qulonglong request = api.startSearch(QStringLiteral("alpha"), options);
  QVERIFY(request > 0);
  QCOMPARE(api.result(request).value(QStringLiteral("state")).toString(),
           QStringLiteral("running"));
  QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
  const QVariantMap result = api.result(request);
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QCOMPARE(result.value(QStringLiteral("state")).toString(),
           QStringLiteral("ready"));
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 1);
  QVERIFY(result.value(QStringLiteral("hasMore")).toBool());
  QCOMPARE(result.value(QStringLiteral("nextOffset")).toInt(), 1);
  const QVariantMap row =
      result.value(QStringLiteral("rows")).toList().first().toMap();
  QCOMPARE(row.value(QStringLiteral("extension")).toString(),
           QStringLiteral("pdf"));
  QVERIFY(row.value(QStringLiteral("uri")).toString().startsWith(
      QLatin1String("file://")));
  const QVariantMap generation = result.value(QStringLiteral("catalog")).toMap();
  QVERIFY(generation.value(QStringLiteral("available")).toBool());
  QVERIFY(generation.value(QStringLiteral("coverageComplete")).toBool());
  QVERIFY(generation.value(QStringLiteral("revision")).toLongLong() > 0);

  QVariantMap scopedOptions = options;
  scopedOptions.insert(QStringLiteral("cwd"), root.filePath("nested"));
  scopedOptions.insert(QStringLiteral("limit"), 10);
  const QVariantMap scoped =
      SearchApi::executeSearch(QStringLiteral("alpha"), scopedOptions);
  QCOMPARE(scoped.value(QStringLiteral("count")).toInt(), 1);
  QCOMPARE(scoped.value(QStringLiteral("rows")).toList().first().toMap().value(
               QStringLiteral("path")),
           root.filePath(QStringLiteral("nested/alpha-inside.pdf")));

  const qulonglong queryRequest = api.startQuery(
      QStringLiteral("select name, path from tree order by name"),
      {{QStringLiteral("cwd"), root.path()}, {QStringLiteral("limit"), 2}});
  QVERIFY(queryRequest > 0);
  QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
  const QVariantMap queryResult = api.result(queryRequest);
  QVERIFY2(queryResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(queryResult.value(QStringLiteral("error")).toString()));
  QCOMPARE(queryResult.value(QStringLiteral("state")).toString(),
           QStringLiteral("ready"));
  QCOMPARE(queryResult.value(QStringLiteral("requestKind")).toString(),
           QStringLiteral("query"));
  QCOMPARE(queryResult.value(QStringLiteral("mode")).toString(),
           QStringLiteral("sql"));
  QCOMPARE(queryResult.value(QStringLiteral("count")).toInt(), 2);

  const qulonglong canceled =
      api.startSearch(QStringLiteral("alpha"), options);
  QVERIFY(api.cancel(canceled));
  QCOMPARE(api.result(canceled).value(QStringLiteral("state")).toString(),
           QStringLiteral("canceled"));
}

void FileCatalogTest::hotSetWatcherCoalescesAndReconciles() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);

  HotSetWatcher watcher(32, 80);
  QVERIFY(watcher.available());
  QVERIFY(watcher.addDirectory(root.path()));
  QSignalSpy changed(&watcher, &HotSetWatcher::directoriesChanged);
  connect(&watcher, &HotSetWatcher::directoriesChanged, &catalog,
          &FileCatalog::reconcileDirectories);

  const QString path = root.filePath(QStringLiteral("live.txt"));
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("one");
  file.write(" two");
  file.close();
  QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(catalogRows(), 1, 3000);

  QVERIFY(QFile::remove(path));
  QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 2, 3000);
  QTRY_COMPARE_WITH_TIMEOUT(catalogRows(), 0, 3000);
  QVERIFY(FileCatalog::markHotDirectory(root.path()));
  QCOMPARE(FileCatalog::hotDirectories(4).value(0), root.path());
  QVERIFY(FileCatalog::forgetHotDirectory(root.path()));
  QVERIFY(FileCatalog::hotDirectories(4).isEmpty());
}

void FileCatalogTest::sourceRelationIgnoresCommentsAndStrings() {
  DirectoryModel model;
  FileCatalog catalog(&model);
  QCOMPARE(catalog.sourceRelation(QStringLiteral("select * from tree")),
           QStringLiteral("tree"));
  QCOMPARE(catalog.sourceRelation(
               QStringLiteral("-- from tree\nselect * from selection")),
           QStringLiteral("selection"));
  QCOMPARE(catalog.sourceRelation(
               QStringLiteral("select 'from tree' as note from here")),
           QStringLiteral("here"));
  QCOMPARE(catalog.sourceRelation(QStringLiteral("select * from image_facts")),
           QStringLiteral("image_facts"));
  QCOMPARE(catalog.sourceRelation(QStringLiteral("select * from projects")),
           QStringLiteral("projects"));
}

void FileCatalogTest::nativeShadowIsAtomicAndDeliberatelyLagged() {
  if (QStandardPaths::findExecutable(QStringLiteral("duckdb")).isEmpty())
    QSKIP("duckdb is not installed");
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  QFile first(root.filePath(QStringLiteral("first.txt")));
  QVERIFY(first.open(QIODevice::WriteOnly));
  first.write("first\n");
  first.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  const QVariantMap built = FileCatalog::rebuildShadow(true);
  QVERIFY2(built.value(QStringLiteral("ok")).toBool(),
           qPrintable(built.value(QStringLiteral("error")).toString()));
  QVERIFY(built.value(QStringLiteral("rebuilt")).toBool());
  QVERIFY(built.contains(QStringLiteral("sourceFactsRevision")));
  QCOMPARE(built.value(QStringLiteral("sourceFactsRevision")).toLongLong(),
           built.value(QStringLiteral("currentFactsRevision")).toLongLong());
  QVERIFY(QFileInfo::exists(FileCatalog::shadowPath()));

  QVariantMap query = FileCatalog::querySync(
      QStringLiteral("select name,path from tree order by name"), root.path());
  QVERIFY2(query.value(QStringLiteral("ok")).toBool(),
           qPrintable(query.value(QStringLiteral("error")).toString()));
  QVERIFY2(query.value(QStringLiteral("engine")).toString() ==
               QLatin1String("duckdb-shadow"),
           qPrintable(
               query.value(QStringLiteral("shadowFallbackError")).toString()));
  QCOMPARE(query.value(QStringLiteral("rows")).toList().size(), 1);

  QFile second(root.filePath(QStringLiteral("second.txt")));
  QVERIFY(second.open(QIODevice::WriteOnly));
  second.write("second\n");
  second.close();
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 2,
                           10000);

  const QVariantMap stale = FileCatalog::shadowStatus();
  QVERIFY(stale.value(QStringLiteral("available")).toBool());
  QVERIFY(stale.value(QStringLiteral("stale")).toBool());
  QVERIFY(stale.value(QStringLiteral("lagRevisions")).toLongLong() > 0);

  // Until the next generation is promoted, all new queries intentionally use
  // the last complete snapshot rather than a partially rebuilt database.
  query = FileCatalog::querySync(
      QStringLiteral("select name,path from tree order by name"), root.path());
  QCOMPARE(query.value(QStringLiteral("rows")).toList().size(), 1);

  const QVariantMap refreshed = FileCatalog::rebuildShadow(false);
  QVERIFY2(refreshed.value(QStringLiteral("ok")).toBool(),
           qPrintable(refreshed.value(QStringLiteral("error")).toString()));
  QVERIFY(refreshed.value(QStringLiteral("refreshed")).toBool());
  QCOMPARE(refreshed.value(QStringLiteral("mode")).toString(),
           QStringLiteral("delta"));
  QCOMPARE(refreshed.value(QStringLiteral("changedFiles")).toLongLong(), 1);
  query = FileCatalog::querySync(
      QStringLiteral("select name,path from tree order by name"), root.path());
  QCOMPARE(query.value(QStringLiteral("rows")).toList().size(), 2);
  QVERIFY(!FileCatalog::shadowStatus().value(QStringLiteral("stale")).toBool());

  const qint64 factsRevision =
      FileCatalog::shadowStatus().value(QStringLiteral("currentFactsRevision"))
          .toLongLong();
  catalog.recordImageFacts(
      second.fileName(), QFileInfo(second).lastModified().toMSecsSinceEpoch(),
      QVariantMap{{QStringLiteral("width"), 640.0}});
  QTRY_VERIFY_WITH_TIMEOUT(
      FileCatalog::shadowStatus()
              .value(QStringLiteral("currentFactsRevision"))
              .toLongLong() > factsRevision,
      5000);
  const QVariantMap factsRefresh = FileCatalog::refreshShadow();
  QVERIFY2(factsRefresh.value(QStringLiteral("ok")).toBool(),
           qPrintable(factsRefresh.value(QStringLiteral("error")).toString()));
  QCOMPARE(factsRefresh.value(QStringLiteral("mode")).toString(),
           QStringLiteral("delta"));
  QCOMPARE(factsRefresh.value(QStringLiteral("changedFacts")).toLongLong(), 1);
  const QVariantMap factsQuery = FileCatalog::querySync(
      QStringLiteral("select key,numeric_value from facts where name="
                     "'second.txt' and key='width'"),
      root.path());
  const QVariantList factRows =
      factsQuery.value(QStringLiteral("rows")).toList();
  QCOMPARE(factRows.size(), 1);
  QCOMPARE(factRows.first().toMap().value(QStringLiteral("numeric_value"))
               .toDouble(),
           640.0);

  QVERIFY(QFile::remove(first.fileName()));
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);
  const QVariantMap pruned = FileCatalog::refreshShadow();
  QVERIFY2(pruned.value(QStringLiteral("ok")).toBool(),
           qPrintable(pruned.value(QStringLiteral("error")).toString()));
  QCOMPARE(pruned.value(QStringLiteral("mode")).toString(),
           QStringLiteral("delta"));
  query = FileCatalog::querySync(
      QStringLiteral("select name,path from tree order by name"), root.path());
  const QVariantList remaining = query.value(QStringLiteral("rows")).toList();
  QCOMPARE(remaining.size(), 1);
  QCOMPARE(remaining.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("second.txt"));
}

void FileCatalogTest::deterministicImageFactsAreQueryable() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  const QString path = root.filePath(QStringLiteral("ocean.png"));
  QImage image(240, 100, QImage::Format_RGB32);
  image.fill(qRgb(15, 80, 230));
  QVERIFY(image.save(path, "PNG"));

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);
  catalog.analyzeImages(root.path(), 50);
  QVERIFY(catalog.analyzing());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.analyzing(), 10000);
  QVERIFY(catalog.analysisStatus().contains(QStringLiteral("1 added")));

  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select name,width,height,orientation,color_family,"
                     "palette_0,palette_weight_0,blue_share,visual_hash,path,"
                     "is_dir from image_facts"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 1);
  const QVariantMap row = rows.first().toMap();
  QCOMPARE(row.value(QStringLiteral("width")).toInt(), 240);
  QCOMPARE(row.value(QStringLiteral("height")).toInt(), 100);
  QCOMPARE(row.value(QStringLiteral("orientation")).toString(),
           QStringLiteral("landscape"));
  QCOMPARE(row.value(QStringLiteral("color_family")).toString(),
           QStringLiteral("blue"));
  QVERIFY(row.value(QStringLiteral("blue_share")).toDouble() > 0.95);
  QVERIFY(row.value(QStringLiteral("palette_0")).toString().startsWith('#'));
  QVERIFY(row.value(QStringLiteral("palette_weight_0")).toDouble() > 0.99);
  QCOMPARE(row.value(QStringLiteral("visual_hash")).toString().size(), 16);
  const QVariantMap coverage =
      result.value(QStringLiteral("factCoverage")).toMap();
  QCOMPARE(coverage.value(QStringLiteral("analyzed")).toLongLong(), 1);
  QCOMPARE(coverage.value(QStringLiteral("total")).toLongLong(), 1);
  QVERIFY(coverage.value(QStringLiteral("complete")).toBool());
}

void FileCatalogTest::repeatedImageFactsOnlyInvalidateActualChanges() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  const QString path = root.filePath(QStringLiteral("palette.png"));
  QImage image(32, 32, QImage::Format_RGB32);
  image.fill(qRgb(20, 80, 220));
  QVERIFY(image.save(path, "PNG"));

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  const qint64 treeRevision = FileCatalog::shadowStatus()
          .value(QStringLiteral("currentRevision"))
          .toLongLong();
  const qint64 initialFactsRevision =
      FileCatalog::shadowStatus()
          .value(QStringLiteral("currentFactsRevision"))
          .toLongLong();
  const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
  const QVariantMap blue{
      {QStringLiteral("palette_0"), QStringLiteral("#1450dc")},
                         {QStringLiteral("palette_weight_0"), 0.75}};
  catalog.recordImageFacts(path, mtime, blue);
  QTRY_COMPARE_WITH_TIMEOUT(FileCatalog::shadowStatus()
          .value(QStringLiteral("currentFactsRevision"))
          .toLongLong(),
      initialFactsRevision + 1, 5000);

  // The writer pool serializes these. Waiting for the changed third write
  // proves the identical middle write has also completed.
  catalog.recordImageFacts(path, mtime, blue);
  QVariantMap red = blue;
  red.insert(QStringLiteral("palette_0"), QStringLiteral("#dc5030"));
  catalog.recordImageFacts(path, mtime, red);
  QTRY_COMPARE_WITH_TIMEOUT(FileCatalog::shadowStatus()
          .value(QStringLiteral("currentFactsRevision"))
          .toLongLong(),
      initialFactsRevision + 2, 5000);
  QCOMPARE(FileCatalog::shadowStatus()
               .value(QStringLiteral("currentRevision"))
               .toLongLong(),
           treeRevision);
}

void FileCatalogTest::catalogSceneBuildsEnrichedHierarchy() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  const QString project = root.filePath(QStringLiteral("project"));
  const QString src = QDir(project).filePath(QStringLiteral("src"));
  QVERIFY(QDir().mkpath(src));
  QFile code(QDir(src).filePath(QStringLiteral("main.cpp")));
  QVERIFY(code.open(QIODevice::WriteOnly));
  code.write("int main() { return 0; }\n");
  code.close();
  QFile image(QDir(project).filePath(QStringLiteral("cover.png")));
  QVERIFY(image.open(QIODevice::WriteOnly));
  image.write("not decoded by the scene builder");
  image.close();
  const qint64 recursiveProjectBytes =
      QFileInfo(code.fileName()).size() + QFileInfo(image.fileName()).size();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 4,
                           10000);
  QVERIFY(catalog.coversTree(root.path()));

  QSignalSpy spy(&catalog, &FileCatalog::sceneFinished);
  const quint64 request = catalog.sceneExpanded(
      root.path(), QStringLiteral("tree"), false, {project, src});
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QList<QVariant> args = spy.takeFirst();
  QCOMPARE(args.at(0).toULongLong(), request);
  const QVariantMap result = args.at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QCOMPARE(result.value(QStringLiteral("source")).toString(),
           QStringLiteral("catalog"));
  QCOMPARE(result.value(QStringLiteral("nodeCount")).toInt(), 4);
  QVERIFY(!result.value(QStringLiteral("truncated")).toBool());
  QCOMPARE(result.value(QStringLiteral("omittedCount")).toLongLong(), 0);
  QCOMPARE(result.value(QStringLiteral("rootChildCount")).toInt(), 1);

  const QVariantList boxes = result.value(QStringLiteral("boxes")).toList();
  QVERIFY(!boxes.isEmpty());
  QVariantMap codeBox;
  QVariantMap projectPlatform;
  QVariantMap srcPlatform;
  bool hasRootPlatform = false;
  int roads = 0;
  for (const QVariant &value : boxes) {
    const QVariantMap box = value.toMap();
    if (box.value(QStringLiteral("kind")).toInt() == FsnLayout::KindRoad)
      ++roads;
    if (box.value(QStringLiteral("name")).toString() ==
        QStringLiteral("main.cpp"))
      codeBox = box;
    if (box.value(QStringLiteral("name")).toString() ==
        QStringLiteral("project"))
      projectPlatform = box;
    if (box.value(QStringLiteral("name")).toString() == QStringLiteral("src"))
      srcPlatform = box;
    if (box.value(QStringLiteral("root")).toBool())
      hasRootPlatform = true;
  }
  // A one-district neighborhood needs no synthetic road back to a root
  // monument: StrataV deliberately omits the current directory's geometry.
  QCOMPARE(roads, 0);
  QVERIFY(!hasRootPlatform);
  QCOMPARE(projectPlatform.value(QStringLiteral("bytes")).toLongLong(),
           recursiveProjectBytes);
  QCOMPARE(result.value(QStringLiteral("rollupCacheMisses")).toInt(), 1);
  QCOMPARE(codeBox.value(QStringLiteral("category")).toString(),
           QStringLiteral("code"));
  QCOMPARE(codeBox.value(QStringLiteral("sourceParent")).toString(), src);
  QCOMPARE(codeBox.value(QStringLiteral("ownerPath")).toString(), project);
  QCOMPARE(srcPlatform.value(QStringLiteral("childCount")).toInt(), 1);
  QVERIFY(srcPlatform.value(QStringLiteral("aggregate")).toBool());

  const quint64 cachedRequest = catalog.sceneExpanded(
      root.path(), QStringLiteral("tree"), false, {project, src});
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QList<QVariant> cachedArgs = spy.takeFirst();
  QCOMPARE(cachedArgs.at(0).toULongLong(), cachedRequest);
  const QVariantMap cachedResult = cachedArgs.at(1).toMap();
  QVERIFY(cachedResult.value(QStringLiteral("ok")).toBool());
  QCOMPARE(cachedResult.value(QStringLiteral("rollupCacheHits")).toInt(), 1);
  QCOMPARE(cachedResult.value(QStringLiteral("rollupCacheMisses")).toInt(), 0);
}

void FileCatalogTest::projectRelationClassifiesMarkers() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  const QString project = root.filePath(QStringLiteral("pipeline"));
  QVERIFY(QDir().mkpath(project));
  QFile marker(QDir(project).filePath(QStringLiteral("pyproject.toml")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("[project]\nname='pipeline'\n");
  marker.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 2,
                           10000);
  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select name,project_type,markers,path,is_dir "
                               "from projects"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantMap row =
      result.value(QStringLiteral("rows")).toList().first().toMap();
  QCOMPARE(row.value(QStringLiteral("name")).toString(),
           QStringLiteral("pipeline"));
  QCOMPARE(row.value(QStringLiteral("project_type")).toString(),
           QStringLiteral("python"));
  QCOMPARE(row.value(QStringLiteral("path")).toString(), project);
  QVERIFY(row.value(QStringLiteral("is_dir")).toBool());
}

void FileCatalogTest::headlessQueryReportsScopeCoverage() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QFile file(root.filePath(QStringLiteral("agent.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("catalog\n");
  file.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);
  const QVariantMap result = FileCatalog::querySync(
      QStringLiteral("select name,path from tree"), root.path(), {}, 20);
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
  const QVariantMap scope = result.value(QStringLiteral("scope")).toMap();
  QCOMPARE(scope.value(QStringLiteral("cwd")).toString(), root.path());
  QCOMPARE(scope.value(QStringLiteral("relation")).toString(),
           QStringLiteral("tree"));
  const QVariantMap metadata = result.value(QStringLiteral("catalog")).toMap();
  QVERIFY(metadata.value(QStringLiteral("coverageComplete")).toBool());
  QCOMPARE(metadata.value(QStringLiteral("indexedRoot")).toString(),
           root.path());
  QCOMPARE(metadata.value(QStringLiteral("indexedRows")).toLongLong(), 1);
  QVERIFY(metadata.value(QStringLiteral("lastCompleteScanAt")).toLongLong() >
          0);
}

void FileCatalogTest::headlessQueryNeedsNoCatalogWriteAccess() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QFile file(root.filePath(QStringLiteral("sandbox.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("read only\n");
  file.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  const QString db = FileCatalog::dbPath();
  QVERIFY(QFile::setPermissions(db, QFileDevice::ReadOwner));
  const QVariantMap result = FileCatalog::querySync(
      QStringLiteral("select name,path from tree"), root.path(), {}, 20);
  QVERIFY(QFile::setPermissions(db, QFileDevice::ReadOwner |
                                        QFileDevice::WriteOwner));
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);
}

void FileCatalogTest::mcpListsAndCallsCatalogTools() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QFile file(root.filePath(QStringLiteral("alpha-notes.md")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("alpha\n");
  file.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  const QJsonObject listed = AgentBridge::handleRequest(
      {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
       {QStringLiteral("id"), 1},
       {QStringLiteral("method"), QStringLiteral("tools/list")}});
  const QJsonArray definitions = listed.value(QStringLiteral("result"))
                                     .toObject()
                                     .value(QStringLiteral("tools"))
                                     .toArray();
  QSet<QString> names;
  for (const QJsonValue &value : definitions)
    names.insert(value.toObject().value(QStringLiteral("name")).toString());
  for (const QString &name :
       {QStringLiteral("search_files"), QStringLiteral("query_files"),
        QStringLiteral("find_projects"), QStringLiteral("get_file_facts"),
        QStringLiteral("list_saved_queries"), QStringLiteral("run_saved_query"),
        QStringLiteral("show_in_synchro")})
    QVERIFY2(names.contains(name), qPrintable(name));

  const QJsonObject called = AgentBridge::handleRequest(
      {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
       {QStringLiteral("id"), 2},
       {QStringLiteral("method"), QStringLiteral("tools/call")},
       {QStringLiteral("params"),
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("search_files")},
            {QStringLiteral("arguments"),
             QJsonObject{{QStringLiteral("query"), QStringLiteral("alpha")},
                         {QStringLiteral("cwd"), root.path()},
                         {QStringLiteral("limit"), 20}}}}}});
  const QJsonObject structured = called.value(QStringLiteral("result"))
                                     .toObject()
                                     .value(QStringLiteral("structuredContent"))
                                     .toObject();
  QVERIFY(structured.value(QStringLiteral("ok")).toBool());
  const QJsonArray rows = structured.value(QStringLiteral("rows")).toArray();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("alpha-notes.md"));
  QVERIFY(structured.value(QStringLiteral("catalog"))
              .toObject()
              .value(QStringLiteral("coverageComplete"))
              .toBool());
}

void FileCatalogTest::currentFolderIsQueryable() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QFile a(root.filePath(QStringLiteral("alpha.txt")));
  QVERIFY(a.open(QIODevice::WriteOnly));
  a.write("alpha");
  a.close();
  QFile b(root.filePath(QStringLiteral("beta.md")));
  QVERIFY(b.open(QIODevice::WriteOnly));
  b.write("beta");
  b.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  model.setPath(root.path());
  QVERIFY(waitListing(model));
  catalog.refreshCurrent();
  QTRY_VERIFY_WITH_TIMEOUT(catalogRows() >= 2, 5000);

  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  const quint64 id = catalog.query(
      QStringLiteral("select name, size, path from here order by name"),
      root.path(), {}, 20);
  QVERIFY(id > 0);
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 2);
  const QVariantList columns = result.value(QStringLiteral("columns")).toList();
  QCOMPARE(columns.size(), 3);
  QCOMPARE(columns.at(0).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("name"));
  QCOMPARE(columns.at(1).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("size"));
  QCOMPARE(columns.at(2).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("path"));
  QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("alpha.txt"));
}

void FileCatalogTest::derivedSizeAndHiddenFieldsAreQueryable() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  QFile visible(root.filePath(QStringLiteral("payload.bin")));
  QVERIFY(visible.open(QIODevice::WriteOnly));
  QVERIFY(visible.resize(3333333));
  visible.close();
  QFile hidden(root.filePath(QStringLiteral(".secret")));
  QVERIFY(hidden.open(QIODevice::WriteOnly));
  QVERIFY(hidden.resize(512));
  hidden.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  for (const QString &field : {QStringLiteral("hidden"), QStringLiteral("kb"),
                               QStringLiteral("mb"), QStringLiteral("gb")})
    QVERIFY(catalog.fields().contains(field));
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 2,
                           10000);

  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select name, hidden, is_hidden, kb, mb, gb from tree "
                     "where hidden = false and mb >= 3.0 order by name"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 1);
  const QVariantMap row = rows.first().toMap();
  QCOMPARE(row.value(QStringLiteral("name")).toString(),
           QStringLiteral("payload.bin"));
  QCOMPARE(row.value(QStringLiteral("hidden")).toBool(), false);
  QCOMPARE(row.value(QStringLiteral("is_hidden")).toBool(), false);
  QCOMPARE(row.value(QStringLiteral("kb")).toDouble(), 3255.21);
  QCOMPARE(row.value(QStringLiteral("mb")).toDouble(), 3.18);
  QCOMPARE(row.value(QStringLiteral("gb")).toDouble(), 0.0);

  QSignalSpy sumSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select sum(kb) as total_kb, "
                               "sum(mb) as total_mb, sum(gb) as total_gb "
                               "from tree where hidden = false"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(sumSpy.size(), 1, 10000);
  const QVariantMap sumResult = sumSpy.takeFirst().at(1).toMap();
  QVERIFY2(sumResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(sumResult.value(QStringLiteral("error")).toString()));
  const QVariantMap sums =
      sumResult.value(QStringLiteral("rows")).toList().first().toMap();
  QCOMPARE(sums.value(QStringLiteral("total_kb")).toDouble(), 3255.21);
  QCOMPARE(sums.value(QStringLiteral("total_mb")).toDouble(), 3.18);
  QCOMPARE(sums.value(QStringLiteral("total_gb")).toDouble(), 0.0);

  QSignalSpy hiddenSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select name from tree where hidden"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(hiddenSpy.size(), 1, 10000);
  const QVariantMap hiddenResult = hiddenSpy.takeFirst().at(1).toMap();
  QVERIFY2(hiddenResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(hiddenResult.value(QStringLiteral("error")).toString()));
  const QVariantList hiddenRows =
      hiddenResult.value(QStringLiteral("rows")).toList();
  QCOMPARE(hiddenRows.size(), 1);
  QCOMPARE(hiddenRows.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral(".secret"));

  QSignalSpy groupSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select hidden, count(*) as files from tree "
                               "group by hidden order by hidden"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(groupSpy.size(), 1, 10000);
  const QVariantMap grouped = groupSpy.takeFirst().at(1).toMap();
  QVERIFY2(grouped.value(QStringLiteral("ok")).toBool(),
           qPrintable(grouped.value(QStringLiteral("error")).toString()));
  QCOMPARE(grouped.value(QStringLiteral("groupKeys")).toStringList(),
           QStringList{QStringLiteral("hidden")});
  const QVariantList groups = grouped.value(QStringLiteral("rows")).toList();
  QCOMPARE(groups.size(), 2);
  bool foundHiddenDrill = false;
  for (const QVariant &value : groups) {
    const QVariantMap group = value.toMap();
    if (group.value(QStringLiteral("hidden")).toBool()) {
      foundHiddenDrill = group.value(QStringLiteral("_synchro_drill_sql"))
                             .toString()
                             .contains(QStringLiteral("hidden = TRUE"));
    }
  }
  QVERIFY(foundHiddenDrill);
}

void FileCatalogTest::derivedNavigationFieldsAreQueryable() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  QFile image(root.filePath(QStringLiteral("photo.png")));
  QVERIFY(image.open(QIODevice::WriteOnly));
  QVERIFY(image.resize(2 * 1024 * 1024));
  image.close();
  QFile code(root.filePath(QStringLiteral("worker.py")));
  QVERIFY(code.open(QIODevice::WriteOnly));
  code.write("print('ready')\n");
  code.close();
  const QString projectPath = root.filePath(QStringLiteral("project"));
  QVERIFY(QDir().mkpath(projectPath));
  QFile marker(QDir(projectPath).filePath(QStringLiteral("package.json")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("{}\n");
  marker.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  for (const QString &field :
       {QStringLiteral("kind"), QStringLiteral("stem"), QStringLiteral("depth"),
        QStringLiteral("age_days"), QStringLiteral("modified_date"),
        QStringLiteral("modified_month"), QStringLiteral("size_bucket"),
        QStringLiteral("age_bucket"), QStringLiteral("root")})
    QVERIFY2(catalog.fields().contains(field), qPrintable(field));

  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 4,
                           10000);

  QSignalSpy fieldSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select kind,stem,depth,age_days,modified_date,"
                     "modified_month,size_bucket,age_bucket,root from tree "
                     "where name='photo.png'"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(fieldSpy.size(), 1, 10000);
  const QVariantMap fieldResult = fieldSpy.takeFirst().at(1).toMap();
  QVERIFY2(fieldResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(fieldResult.value(QStringLiteral("error")).toString()));
  const QVariantMap imageRow =
      fieldResult.value(QStringLiteral("rows")).toList().first().toMap();
  QCOMPARE(imageRow.value(QStringLiteral("kind")).toString(),
           QStringLiteral("image"));
  QCOMPARE(imageRow.value(QStringLiteral("stem")).toString(),
           QStringLiteral("photo"));
  QVERIFY(imageRow.value(QStringLiteral("depth")).toInt() > 0);
  QCOMPARE(imageRow.value(QStringLiteral("age_days")).toLongLong(), 0);
  QCOMPARE(imageRow.value(QStringLiteral("size_bucket")).toString(),
           QStringLiteral("1-100 MB"));
  QCOMPARE(imageRow.value(QStringLiteral("age_bucket")).toString(),
           QStringLiteral("today"));
  QCOMPARE(imageRow.value(QStringLiteral("root")).toString(), root.path());
  QCOMPARE(imageRow.value(QStringLiteral("modified_date")).toString().size(),
           10);
  QCOMPARE(imageRow.value(QStringLiteral("modified_month")).toString().size(),
           7);

  QSignalSpy groupSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select kind,count(*) as files from tree "
                               "where not is_dir group by kind order by kind"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(groupSpy.size(), 1, 10000);
  const QVariantMap grouped = groupSpy.takeFirst().at(1).toMap();
  QVERIFY2(grouped.value(QStringLiteral("ok")).toBool(),
           qPrintable(grouped.value(QStringLiteral("error")).toString()));
  QCOMPARE(grouped.value(QStringLiteral("groupKeys")).toStringList(),
           QStringList{QStringLiteral("kind")});
  QVariantMap imageGroup;
  for (const QVariant &value : grouped.value(QStringLiteral("rows")).toList()) {
    const QVariantMap row = value.toMap();
    if (row.value(QStringLiteral("kind")).toString() == QLatin1String("image"))
      imageGroup = row;
  }
  QVERIFY(!imageGroup.isEmpty());
  QCOMPARE(imageGroup.value(QStringLiteral("_synchro_preview_paths"))
               .toList()
               .size(),
           1);
  const QString drill =
      imageGroup.value(QStringLiteral("_synchro_drill_sql")).toString();
  QVERIFY(drill.contains(QStringLiteral("kind = 'image'")));

  QSignalSpy drillSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(drill, root.path());
  QTRY_COMPARE_WITH_TIMEOUT(drillSpy.size(), 1, 10000);
  const QVariantMap drillResult = drillSpy.takeFirst().at(1).toMap();
  QVERIFY2(drillResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(drillResult.value(QStringLiteral("error")).toString()));
  QCOMPARE(drillResult.value(QStringLiteral("rows")).toList().size(), 1);
  QCOMPARE(drillResult.value(QStringLiteral("rows"))
               .toList()
               .first()
               .toMap()
               .value(QStringLiteral("name"))
               .toString(),
           QStringLiteral("photo.png"));

  QSignalSpy projectSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral(
          "select parent as path,"
          "coalesce(nullif(regexp_extract(parent,'[^/]+$'),''),'/') as name,"
          "true as is_dir,0::BIGINT as size,max(mtime) as mtime "
          "from tree where not is_dir and lower(name)='package.json' "
          "group by parent"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(projectSpy.size(), 1, 10000);
  const QVariantMap projects = projectSpy.takeFirst().at(1).toMap();
  QVERIFY2(projects.value(QStringLiteral("ok")).toBool(),
           qPrintable(projects.value(QStringLiteral("error")).toString()));
  const QVariantMap project =
      projects.value(QStringLiteral("rows")).toList().first().toMap();
  QCOMPARE(project.value(QStringLiteral("path")).toString(), projectPath);
  QCOMPARE(project.value(QStringLiteral("name")).toString(),
           QStringLiteral("project"));
  QVERIFY(project.value(QStringLiteral("is_dir")).toBool());
}

void FileCatalogTest::querySurfaceRejectsWrites() {
  QTemporaryDir home;
  QVERIFY(home.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  DirectoryModel model;
  FileCatalog catalog(&model);
  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("delete from files"), QDir::homePath());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 5000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY(!result.value(QStringLiteral("ok")).toBool());
  QVERIFY(result.value(QStringLiteral("error"))
              .toString()
              .contains(QStringLiteral("read-only")));
  QString validationError;
  QVERIFY(!FileCatalog::validateReadOnlySql(QStringLiteral("delete from files"),
                                            &validationError));
  QVERIFY(validationError.contains(QStringLiteral("read-only")));
  QVERIFY(FileCatalog::validateReadOnlySql(
      QStringLiteral("select name,path from tree limit 20"), &validationError));
}

void FileCatalogTest::scanBuildsTreeRelation() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QDir().mkpath(root.filePath(QStringLiteral("nested/deeper")));
  QDir().mkpath(root.filePath(QStringLiteral(".git/objects")));
  QDir().mkpath(root.filePath(QStringLiteral("node_modules/package")));
  QFile f(root.filePath(QStringLiteral("nested/deeper/result.json")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("{}");
  f.close();
  QFile gitObject(root.filePath(QStringLiteral(".git/objects/indexed-object")));
  QVERIFY(gitObject.open(QIODevice::WriteOnly));
  gitObject.write("git");
  gitObject.close();
  QFile module(
      root.filePath(QStringLiteral("node_modules/package/indexed-module.js")));
  QVERIFY(module.open(QIODevice::WriteOnly));
  module.write("export default true");
  module.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  QSignalSpy statusSpy(&catalog, &FileCatalog::statusChanged);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() >= 3,
                           10000);
  QVERIFY(!statusSpy.isEmpty());

  QSignalSpy querySpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select name from tree where extension in ('json','js') "
                     "or name='indexed-object' order by name"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(querySpy.size(), 1, 10000);
  const QVariantMap result = querySpy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 3);
  QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("indexed-module.js"));
  QCOMPARE(rows.at(1).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("indexed-object"));
  QCOMPARE(rows.at(2).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("result.json"));
}

void FileCatalogTest::cappedScanReportsIncomplete() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  for (int i = 0; i < 5; ++i) {
    QFile file(root.filePath(QStringLiteral("item-%1.txt").arg(i)));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
  }

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 2);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  QCOMPARE(catalog.indexedCount(), 2);
  QVERIFY(catalog.statusText().contains(QStringLiteral("index incomplete")));
  QVERIFY(catalog.statusText().contains(QStringLiteral("limit reached")));
  QVERIFY(!catalog.statusText().contains(QStringLiteral("cap reached")));
}

void FileCatalogTest::incrementalScanPublishesQueryableBatches() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  for (int dirIndex = 0; dirIndex < 20; ++dirIndex) {
    const QString dirPath = root.filePath(
        QStringLiteral("batch-%1").arg(dirIndex, 2, 10, QLatin1Char('0')));
    QVERIFY(QDir().mkpath(dirPath));
    for (int fileIndex = 0; fileIndex < 80; ++fileIndex) {
      QFile file(
          QDir(dirPath).filePath(QStringLiteral("item-%1.txt")
                                     .arg(fileIndex, 3, 10, QLatin1Char('0'))));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("x");
    }
  }

  DirectoryModel model;
  FileCatalog catalog(&model);
  QSignalSpy querySpy(&catalog, &FileCatalog::queryFinished);
  bool queryStartedFromProgress = false;
  connect(&catalog, &FileCatalog::statusChanged, &catalog, [&] {
    if (queryStartedFromProgress || !catalog.indexing() ||
        catalog.indexedCount() < 512)
      return;
    queryStartedFromProgress = true;
    catalog.query(QStringLiteral("select count(*) as files from tree"),
                  root.path());
  });

  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(queryStartedFromProgress, 10000);
  QTRY_COMPARE_WITH_TIMEOUT(querySpy.size(), 1, 10000);
  const QVariantMap result = querySpy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantMap row =
      result.value(QStringLiteral("rows")).toList().first().toMap();
  QVERIFY2(row.value(QStringLiteral("files")).toLongLong() >= 512,
           "a tree query should see batches committed by an active scan");

  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  QCOMPARE(catalog.indexedCount(), 1620); // 20 folders + 1,600 files.
  QVERIFY(catalog.statusText().contains(QStringLiteral("Tree indexed")));
}

void FileCatalogTest::completedScanStateSurvivesRelaunch() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("nested"))));
  QFile file(root.filePath(QStringLiteral("nested/item.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("persisted");
  file.close();

  {
    DirectoryModel model;
    FileCatalog catalog(&model);
    catalog.scanTree(root.path());
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
    QCOMPARE(catalog.indexedCount(), 2);
  }

  DirectoryModel restoredModel;
  FileCatalog restored(&restoredModel);
  QCOMPARE(restored.indexedRoot(), QDir::cleanPath(root.path()));
  QCOMPARE(restored.indexedCount(), 2);
  QVERIFY(restored.coversTree(root.path()));
  QVERIFY(restored.coversTree(root.filePath(QStringLiteral("nested"))));
  QVERIFY(!restored.coversTree(home.path()));
  QVERIFY(restored.statusText().contains(QStringLiteral("persisted")));
}

void FileCatalogTest::legacyCatalogStateIsAdopted() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QFile file(root.filePath(QStringLiteral("legacy.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("legacy");
  file.close();

  {
    DirectoryModel model;
    FileCatalog catalog(&model);
    catalog.scanTree(root.path());
    QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  }
  sqlite3 *db = nullptr;
  QVERIFY(sqlite3_open_v2(QFile::encodeName(FileCatalog::dbPath()).constData(),
                          &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
  QVERIFY(sqlite3_exec(db, "DELETE FROM scan_state;", nullptr, nullptr,
                       nullptr) == SQLITE_OK);
  sqlite3_close(db);

  DirectoryModel restoredModel;
  FileCatalog restored(&restoredModel);
  QCOMPARE(restored.indexedRoot(), QDir::cleanPath(root.path()));
  QCOMPARE(restored.indexedCount(), 1);
  QVERIFY(restored.coversTree(root.path()));
}

void FileCatalogTest::legacySchemaMigratesWithoutRowLoss() {
  QTemporaryDir home;
  QVERIFY(home.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QVERIFY(QDir().mkpath(home.path()));
  sqlite3 *db = nullptr;
  QVERIFY(sqlite3_open_v2(QFile::encodeName(FileCatalog::dbPath()).constData(),
                          &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                          nullptr) == SQLITE_OK);
  const char *legacy =
      "CREATE TABLE files(path TEXT PRIMARY KEY,parent TEXT NOT NULL,"
      "name TEXT NOT NULL,extension TEXT NOT NULL DEFAULT '',"
      "is_dir INTEGER NOT NULL DEFAULT 0,size INTEGER NOT NULL DEFAULT 0,"
      "mtime INTEGER NOT NULL DEFAULT 0,mime TEXT NOT NULL DEFAULT '',"
      "is_hidden INTEGER NOT NULL DEFAULT 0,is_symlink INTEGER NOT NULL "
      "DEFAULT 0,"
      "seen_at INTEGER NOT NULL DEFAULT 0,scan_root TEXT NOT NULL DEFAULT '');"
      "INSERT INTO files(path,parent,name) "
      "VALUES('/old/a.txt','/old','a.txt');";
  QVERIFY(sqlite3_exec(db, legacy, nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(db);

  DirectoryModel model;
  FileCatalog catalog(&model, nullptr, true);
  // The low-priority legacy FTS backfill may briefly hold the first write
  // transaction after construction; ordinary readers retry through WAL.
  QTRY_COMPARE_WITH_TIMEOUT(catalogRows(), 1, 1000);
  QVERIFY(sqlite3_open_v2(QFile::encodeName(FileCatalog::dbPath()).constData(),
                          &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
  for (const char *column : {"file_id", "device", "inode"}) {
    sqlite3_stmt *st = nullptr;
    QVERIFY(sqlite3_prepare_v2(db, "PRAGMA table_info(files);", -1, &st,
                               nullptr) == SQLITE_OK);
    bool found = false;
    while (sqlite3_step(st) == SQLITE_ROW) {
      const auto *name = sqlite3_column_text(st, 1);
      if (name && QByteArray(reinterpret_cast<const char *>(name)) == column)
        found = true;
    }
    sqlite3_finalize(st);
    QVERIFY2(found, column);
  }
  sqlite3_stmt *facts = nullptr;
  QVERIFY(sqlite3_prepare_v2(db, "SELECT count(*) FROM file_facts;", -1, &facts,
                             nullptr) == SQLITE_OK);
  QVERIFY(sqlite3_step(facts) == SQLITE_ROW);
  QCOMPARE(sqlite3_column_int(facts, 0), 0);
  sqlite3_finalize(facts);
  sqlite3_close(db);

  QVariantMap migratedSearch;
  QTRY_VERIFY_WITH_TIMEOUT(
      ([&] {
        migratedSearch = FileCatalog::searchSync(QStringLiteral("a.txt"),
                                                 QStringLiteral("/old"));
        return migratedSearch.value(QStringLiteral("count")).toInt() == 1 &&
               migratedSearch.value(QStringLiteral("index"))
                   .toMap()
                   .value(QStringLiteral("complete"))
                   .toBool();
      })(),
      5000);
  QCOMPARE(migratedSearch.value(QStringLiteral("rows"))
               .toList()
               .first()
               .toMap()
               .value(QStringLiteral("path"))
               .toString(),
           QStringLiteral("/old/a.txt"));
}

void FileCatalogTest::trigramSearchTracksCatalogMutations() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  const QString alpha = root.filePath(QStringLiteral("AlphaWidget.txt"));
  QFile file(alpha);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("alpha");
  file.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  QVariantMap result =
      FileCatalog::searchSync(QStringLiteral("phaw"), root.path(), {}, {}, 8);
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QCOMPARE(result.value(QStringLiteral("index"))
               .toMap()
               .value(QStringLiteral("tokenizer"))
               .toString(),
           QStringLiteral("fts5-trigram"));
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 1);
  QCOMPARE(result.value(QStringLiteral("rows"))
               .toList()
               .first()
               .toMap()
               .value(QStringLiteral("path"))
               .toString(),
           alpha);

  const QString beta = root.filePath(QStringLiteral("BetaArchive.txt"));
  QVERIFY(QFile::rename(alpha, beta));
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  result = FileCatalog::searchSync(QStringLiteral("phaw"), root.path());
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 0);
  result = FileCatalog::searchSync(QStringLiteral("archi"), root.path());
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 1);
  QCOMPARE(result.value(QStringLiteral("rows"))
               .toList()
               .first()
               .toMap()
               .value(QStringLiteral("path"))
               .toString(),
           beta);

  QVERIFY(QFile::remove(beta));
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  result = FileCatalog::searchSync(QStringLiteral("archi"), root.path());
  QCOMPARE(result.value(QStringLiteral("count")).toInt(), 0);
}

void FileCatalogTest::launcherSearchUsesAndTerms() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  for (const QString &name :
       {QStringLiteral("TestReport.pdf"), QStringLiteral("TestNotes.md"),
        QStringLiteral("AnnualReport.pdf")}) {
    QFile file(root.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(name.toUtf8());
  }

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 3,
                           10000);

  const QVariantMap result = FileCatalog::searchSync(QStringLiteral("test pdf"),
                                                     root.path(), {}, {}, 8);
  QVERIFY(result.value(QStringLiteral("ok")).toBool());
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("TestReport.pdf"));
}

void FileCatalogTest::launcherSearchPrioritizesSavedLocations() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  const QString ordinary = root.filePath(QStringLiteral("ProjectOrdinary"));
  const QString pinned = root.filePath(QStringLiteral("ProjectPinned"));
  QVERIFY(QDir().mkpath(ordinary));
  QVERIFY(QDir().mkpath(pinned));

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 2,
                           10000);

  const QVariantList bookmarks{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("big-projects")},
                  {QStringLiteral("name"), QStringLiteral("Project giants")},
                  {QStringLiteral("sql"),
                   QStringLiteral("select * from tree where size > 1")},
                  {QStringLiteral("cwd"), root.path()}}};
  const QVariantMap result = FileCatalog::searchSync(
      QStringLiteral("project"), root.path(), {pinned}, bookmarks, 8);
  QVERIFY(result.value(QStringLiteral("ok")).toBool());
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 3);
  QCOMPARE(rows.at(0).toMap().value(QStringLiteral("kind")).toString(),
           QStringLiteral("saved-query"));
  QCOMPARE(rows.at(1).toMap().value(QStringLiteral("path")).toString(), pinned);
  QVERIFY(rows.at(1).toMap().value(QStringLiteral("bookmarked")).toBool());
  QCOMPARE(rows.at(2).toMap().value(QStringLiteral("path")).toString(),
           ordinary);
}

void FileCatalogTest::launcherSearchExportsWarmThumbnail() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  const QString path = root.filePath(QStringLiteral("PreviewImage.png"));
  QImage image(24, 18, QImage::Format_ARGB32_Premultiplied);
  image.fill(QColor(QStringLiteral("#33aaff")));
  QVERIFY(image.save(path));

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);

  QVariantMap result = FileCatalog::searchSync(QStringLiteral("preview"),
                                               root.path(), {}, {}, 8);
  QVariantMap row =
      result.value(QStringLiteral("rows")).toList().first().toMap();
  const qint64 mtime = row.value(QStringLiteral("mtime")).toLongLong();
  ThumbCache::instance().putImage(path, mtime, 128, image);

  result = FileCatalog::searchSync(QStringLiteral("preview"), root.path(), {},
                                   {}, 8);
  row = result.value(QStringLiteral("rows")).toList().first().toMap();
  const QString thumbnail = row.value(QStringLiteral("thumbnail")).toString();
  QVERIFY(thumbnail.startsWith(QLatin1String("file:")));
  QVERIFY(QFileInfo::exists(QUrl(thumbnail).toLocalFile()));
}

void FileCatalogTest::launcherContentSearchFindsLiteralText() {
  if (QStandardPaths::findExecutable(QStringLiteral("rg")).isEmpty())
    QSKIP("rg is not installed");
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));

  QFile match(root.filePath(QStringLiteral("matcher.py")));
  QVERIFY(match.open(QIODevice::WriteOnly));
  match.write("import re\npattern = re.compile('synchro')\n");
  match.close();
  QFile miss(root.filePath(QStringLiteral("other.py")));
  QVERIFY(miss.open(QIODevice::WriteOnly));
  miss.write("import os\n");
  miss.close();

  const QVariantMap result = FileCatalog::contentSearchSync(
      QStringLiteral("'import re'"), root.path(), 8, 3000);
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 1);
  QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("matcher.py"));
  QCOMPARE(rows.first().toMap().value(QStringLiteral("matchType")).toString(),
           QStringLiteral("content"));
}

void FileCatalogTest::aggregateRowsExposeDrillQueries() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  for (const QString &name :
       {QStringLiteral("one.txt"), QStringLiteral("two.txt"),
        QStringLiteral("three.md"), QStringLiteral("LICENSE"),
        QStringLiteral("one.rlib"), QStringLiteral("two.rlib")}) {
    QFile file(root.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(name.toUtf8());
  }

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 6,
                           10000);

  QSignalSpy aggregateSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select extension, count(*) as files "
                     "from tree group by extension order by files desc"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(aggregateSpy.size(), 1, 10000);
  const QVariantMap aggregate = aggregateSpy.takeFirst().at(1).toMap();
  QVERIFY2(aggregate.value(QStringLiteral("ok")).toBool(),
           qPrintable(aggregate.value(QStringLiteral("error")).toString()));
  QCOMPARE(aggregate.value(QStringLiteral("sourceRelation")).toString(),
           QStringLiteral("tree"));
  QCOMPARE(aggregate.value(QStringLiteral("groupKeys")).toStringList(),
           QStringList{QStringLiteral("extension")});

  const QVariantList rows = aggregate.value(QStringLiteral("rows")).toList();
  QCOMPARE(rows.size(), 4);
  QVariantMap txtRow;
  QVariantMap noExtensionRow;
  QVariantMap opaqueRow;
  for (const QVariant &value : rows) {
    const QVariantMap row = value.toMap();
    QVERIFY(!row.value(QStringLiteral("_synchro_label")).toString().isEmpty());
    QVERIFY(
        !row.value(QStringLiteral("_synchro_drill_sql")).toString().isEmpty());
    if (row.value(QStringLiteral("extension")).toString() ==
        QLatin1String("txt"))
      txtRow = row;
    if (row.value(QStringLiteral("extension")).toString().isEmpty())
      noExtensionRow = row;
    if (row.value(QStringLiteral("extension")).toString() ==
        QLatin1String("rlib"))
      opaqueRow = row;
  }
  QVERIFY(!txtRow.isEmpty());
  QCOMPARE(
      txtRow.value(QStringLiteral("_synchro_preview_paths")).toList().size(),
      2);
  const QString txtDrill =
      txtRow.value(QStringLiteral("_synchro_drill_sql")).toString();
  QVERIFY(txtDrill.contains(QStringLiteral("extension = 'txt'")));
  QVERIFY(!txtDrill.contains(QStringLiteral("IS NOT DISTINCT FROM")));
  QVERIFY(!noExtensionRow.isEmpty());
  QCOMPARE(noExtensionRow.value(QStringLiteral("_synchro_preview_paths"))
               .toList()
               .size(),
           1);
  QVERIFY(noExtensionRow.value(QStringLiteral("_synchro_drill_sql"))
              .toString()
              .contains(QStringLiteral("extension = ''")));
  QVERIFY(!opaqueRow.isEmpty());
  QCOMPARE(
      opaqueRow.value(QStringLiteral("_synchro_preview_paths")).toList().size(),
      2);

  QSignalSpy drillSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(txtDrill, root.path());
  QTRY_COMPARE_WITH_TIMEOUT(drillSpy.size(), 1, 10000);
  const QVariantMap drill = drillSpy.takeFirst().at(1).toMap();
  QVERIFY2(drill.value(QStringLiteral("ok")).toBool(),
           qPrintable(drill.value(QStringLiteral("error")).toString()));
  QCOMPARE(drill.value(QStringLiteral("rows")).toList().size(), 2);

  QSignalSpy nullSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(
      QStringLiteral("select cast(NULL as varchar) as extension, "
                     "count(*) as files from tree group by extension"),
      root.path());
  QTRY_COMPARE_WITH_TIMEOUT(nullSpy.size(), 1, 10000);
  const QVariantMap nullResult = nullSpy.takeFirst().at(1).toMap();
  QVERIFY2(nullResult.value(QStringLiteral("ok")).toBool(),
           qPrintable(nullResult.value(QStringLiteral("error")).toString()));
  const QVariantMap nullRow =
      nullResult.value(QStringLiteral("rows")).toList().first().toMap();
  QVERIFY(nullRow.value(QStringLiteral("_synchro_drill_sql"))
              .toString()
              .contains(QStringLiteral("extension IS NULL")));
}

void FileCatalogTest::rescanPrunesMissingRows() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  const QString doomed = root.filePath(QStringLiteral("gone.txt"));
  QFile f(doomed);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("temporary");
  f.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);
  QVERIFY(QFile::remove(doomed));
  catalog.scanTree(root.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);

  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select path from tree"), root.path());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  QVERIFY(result.value(QStringLiteral("rows")).toList().isEmpty());
}

void FileCatalogTest::incrementalRefreshReconcilesExternalMove() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  const QString fromDir = root.filePath(QStringLiteral("from"));
  const QString toDir = root.filePath(QStringLiteral("to"));
  QVERIFY(QDir().mkpath(fromDir));
  QVERIFY(QDir().mkpath(toDir));
  const QString oldPath = QDir(fromDir).filePath(QStringLiteral("moved.bin"));
  const QString newPath = QDir(toDir).filePath(QStringLiteral("moved.bin"));
  QFile file(oldPath);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("move me");
  file.close();

  DirectoryModel model;
  FileCatalog catalog(&model);
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 3,
                           10000);

  QTest::qWait(5);
  QVERIFY(QFile::rename(oldPath, newPath));
  catalog.scanTree(root.path());
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing(), 10000);
  QVERIFY(catalog.statusText().startsWith(QStringLiteral("Tree refreshed")));

  QSignalSpy spy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select path from tree order by path"),
                root.path());
  QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 10000);
  const QVariantMap result = spy.takeFirst().at(1).toMap();
  QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
           qPrintable(result.value(QStringLiteral("error")).toString()));
  const QVariantList rows = result.value(QStringLiteral("rows")).toList();
  QStringList paths;
  for (const QVariant &value : rows)
    paths.append(value.toMap().value(QStringLiteral("path")).toString());
  QVERIFY(!paths.contains(oldPath));
  QVERIFY(paths.contains(newPath));
}

QTEST_MAIN(FileCatalogTest)
#include "file_catalog_test.moc"
