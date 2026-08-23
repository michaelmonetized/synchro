#include "DirectoryModel.h"
#include "FileCatalog.h"

#include <QFile>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
  if (sqlite3_prepare_v2(db, "SELECT count(*) FROM files;", -1, &st,
                         nullptr) == SQLITE_OK &&
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
  void querySurfaceRejectsWrites();
  void scanBuildsTreeRelation();
  void cappedScanReportsIncomplete();
  void incrementalScanPublishesQueryableBatches();
  void completedScanStateSurvivesRelaunch();
  void legacyCatalogStateIsAdopted();
  void aggregateRowsExposeDrillQueries();
  void incrementalRefreshReconcilesExternalMove();
  void rescanPrunesMissingRows();
};

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
  catalog.query(QStringLiteral(
                    "select name, hidden, is_hidden, kb, mb, gb from tree "
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
  QVERIFY(result.value(QStringLiteral("error")).toString().contains(
      QStringLiteral("read-only")));
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
  QFile module(root.filePath(
      QStringLiteral("node_modules/package/indexed-module.js")));
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
  catalog.query(QStringLiteral(
                    "select name from tree where extension in ('json','js') "
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
    const QString dirPath =
        root.filePath(QStringLiteral("batch-%1").arg(dirIndex, 2, 10,
                                                       QLatin1Char('0')));
    QVERIFY(QDir().mkpath(dirPath));
    for (int fileIndex = 0; fileIndex < 80; ++fileIndex) {
      QFile file(QDir(dirPath).filePath(
          QStringLiteral("item-%1.txt").arg(fileIndex, 3, 10,
                                             QLatin1Char('0'))));
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

void FileCatalogTest::aggregateRowsExposeDrillQueries() {
  QTemporaryDir home;
  QTemporaryDir root;
  QVERIFY(home.isValid());
  QVERIFY(root.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  for (const QString &name : {QStringLiteral("one.txt"),
                              QStringLiteral("two.txt"),
                              QStringLiteral("three.md"),
                              QStringLiteral("LICENSE"),
                              QStringLiteral("one.rlib"),
                              QStringLiteral("two.rlib")}) {
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
  catalog.query(QStringLiteral("select extension, count(*) as files "
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
    QVERIFY(!row.value(QStringLiteral("_synchro_drill_sql")).toString().isEmpty());
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
  QCOMPARE(txtRow.value(QStringLiteral("_synchro_preview_paths"))
               .toList()
               .size(),
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
  QCOMPARE(opaqueRow.value(QStringLiteral("_synchro_preview_paths"))
               .toList()
               .size(),
           2);

  QSignalSpy drillSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(txtDrill, root.path());
  QTRY_COMPARE_WITH_TIMEOUT(drillSpy.size(), 1, 10000);
  const QVariantMap drill = drillSpy.takeFirst().at(1).toMap();
  QVERIFY2(drill.value(QStringLiteral("ok")).toBool(),
           qPrintable(drill.value(QStringLiteral("error")).toString()));
  QCOMPARE(drill.value(QStringLiteral("rows")).toList().size(), 2);

  QSignalSpy nullSpy(&catalog, &FileCatalog::queryFinished);
  catalog.query(QStringLiteral("select cast(NULL as varchar) as extension, "
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
