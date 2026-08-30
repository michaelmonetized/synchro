#include "Config.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerRegistry.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "TrashStore.h"
#include "VolumeStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

bool writeFile(const QString &path, const QByteArray &data = QByteArray("x")) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.write(data);
  return true;
}

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

int findRow(const DirectoryModel &model, const QString &name) {
  for (int i = 0; i < model.rowCount(); ++i) {
    if (model.data(model.index(i, 0), DirectoryModel::NameRole).toString() ==
        name)
      return i;
  }
  return -1;
}

QString nameAt(const FilterProxy &proxy, int row) {
  return proxy.data(proxy.index(row, 0), DirectoryModel::NameRole).toString();
}

QString canon(const QString &path) {
  const QString c = QFileInfo(path).canonicalFilePath();
  return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

} // namespace

class LocationAdaptersTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();
  void expandHomePath();
  void chipsFromManifests();
  void chooserOffersRecentOmitsTrash();
  void activateHomeNavigates();
  void recentListsAndEnterActivates();
  void recentGRevealsParent();
  void trashListsAndEnterRestores();
  void recentNewestFirstUnderNameSort();
  void recentDuplicateBasenames();
  void recordWhileViewingRecentDoesNotDeadlock();
  void emptyRequiresConfirm();
  void sortDescKeepsDirsFirst();
  void sortByNameAndSize();
  void proxyRowMapFollowsSort();
  void configPersistsHiddenAndSort();
  void configPersistsPanelLook();
  void lastPathNeverPersistsSearch();
  void pinChipAfterHomeAndActivate();
  void pinPersistsInConfig();
  void sqlBookmarkPersistsAndUpdates();
  void sqlBookmarkIsALocation();
  void colonPinAndShiftPToggle();
  void configUnknownVersionIsReadOnly();
  void currentStatHasSizeMtimePerm();
  void volumeParseFiltersNoiseAndKeepsMedia();
  void volumesListingAndRootTreeUp();
  void extraVolumeChipAppears();
  void diskPlacesSplitAndVolumesActiveOnlyOnInventory();

private:
  QTemporaryDir m_xdg;
};

void LocationAdaptersTest::initTestCase() {
  QVERIFY(m_xdg.isValid());
  qputenv("XDG_DATA_HOME", QFile::encodeName(m_xdg.path()));
  VolumeStore::instance().setInventoryForTest({});
}

void LocationAdaptersTest::cleanup() {
  QDir(TrashStore::filesDir()).removeRecursively();
  QDir(TrashStore::infoDir()).removeRecursively();
  VolumeStore::instance().setScanHook({});
  VolumeStore::instance().setEjectHook({});
  VolumeStore::instance().setInventoryForTest({});
}

void LocationAdaptersTest::expandHomePath() {
  QCOMPARE(LocationChips::expandPath(QStringLiteral("$HOME")), QDir::homePath());
  QCOMPARE(LocationChips::expandPath(QStringLiteral("${HOME}/Projects")),
           QDir::homePath() + QStringLiteral("/Projects"));
}

void LocationAdaptersTest::chipsFromManifests() {
  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.location.home")));
  QVERIFY(reg.contains(QStringLiteral("synchro.location.recent")));
  QVERIFY(reg.contains(QStringLiteral("synchro.location.trash")));

  LocationChips chips;
  chips.setRegistry(&reg);
  const QVariantList list = chips.chips();
  QCOMPARE(list.size(), 4);
  QCOMPARE(list.at(0).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.home"));
  QCOMPARE(list.at(1).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.recent"));
  QCOMPARE(list.at(2).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.trash"));
  QCOMPARE(list.at(3).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.volumes"));
}

void LocationAdaptersTest::chooserOffersRecentOmitsTrash() {
  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();
  LocationChips chips;
  chips.setRegistry(&reg);
  chips.setChooserMode(true);
  const QVariantList list = chips.chips();
  QStringList ids;
  for (const QVariant &v : list)
    ids.append(v.toMap().value(QStringLiteral("id")).toString());
  QVERIFY(ids.contains(QStringLiteral("synchro.location.home")));
  QVERIFY(ids.contains(QStringLiteral("synchro.location.recent")));
  QVERIFY(ids.contains(QStringLiteral("synchro.location.volumes")));
  QVERIFY(!ids.contains(QStringLiteral("synchro.location.trash")));
}

void LocationAdaptersTest::activateHomeNavigates() {
  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();

  DirectoryModel model;
  NavStack nav(&model);
  LocationChips chips;
  chips.setRegistry(&reg);
  chips.setNav(&nav);
  chips.setDirectoryModel(&model);
  chips.activate(QStringLiteral("synchro.location.home"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(QDir::homePath()));
}

void LocationAdaptersTest::recentListsAndEnterActivates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString file = tmp.filePath(QStringLiteral("notes.md"));
  QVERIFY(writeFile(file));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("proj")));
  const QString dir = tmp.filePath(QStringLiteral("proj"));

  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(file, QStringLiteral("text/markdown"));
  recents.record(dir, QStringLiteral("inode/directory"));

  DirectoryModel model;
  model.setRecentStore(&recents);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setRecentStore(&recents);

  model.setPath(QStringLiteral("recent://"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.path(), QStringLiteral("recent://"));
  QVERIFY(model.isRecent());
  QCOMPARE(model.rowCount(), 2);
  QCOMPARE(model.currentName(), QStringLiteral("proj"));
  model.activateCurrent();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(dir));
}

void LocationAdaptersTest::recentGRevealsParent() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));
  const QString file = tmp.filePath(QStringLiteral("sub/read.txt"));
  QVERIFY(writeFile(file));

  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(file, QStringLiteral("text/plain"));

  DirectoryModel model;
  model.setRecentStore(&recents);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(QStringLiteral("recent://"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.currentName(), QStringLiteral("read.txt"));
  QVERIFY(keys.handleListKey(Qt::Key_G, Qt::ControlModifier, QString()));
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()),
           canon(tmp.filePath(QStringLiteral("sub"))));
  QCOMPARE(model.currentName(), QStringLiteral("read.txt"));
}

void LocationAdaptersTest::trashListsAndEnterRestores() {
  QTemporaryDir live;
  QVERIFY(live.isValid());
  const QString src = live.filePath(QStringLiteral("gone.txt"));
  QVERIFY(writeFile(src, QByteArray("hello")));

  QString err;
  QString trashFile;
  QVERIFY(TrashStore::canTrash(src, &err));
  QVERIFY2(TrashStore::prepareTrash(src, &trashFile, &err), qPrintable(err));
  QVERIFY(QFile::rename(src, trashFile));
  QVERIFY(!QFileInfo::exists(src));

  DirectoryModel model;
  model.setPath(QStringLiteral("trash://"));
  QVERIFY(waitListingDone(model));
  QVERIFY(model.isTrash());
  QCOMPARE(model.path(), QStringLiteral("trash://"));
  const int row = findRow(model, QFileInfo(trashFile).fileName());
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  QVERIFY(model.restoreCurrent());
  QVERIFY(waitListingDone(model));
  QVERIFY(QFileInfo::exists(src) ||
          QFileInfo::exists(live.filePath(QStringLiteral("gone (1).txt"))));
  QVERIFY(!QFileInfo::exists(trashFile));
}

void LocationAdaptersTest::recentNewestFirstUnderNameSort() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("z.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.txt"))));

  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(tmp.filePath(QStringLiteral("z.txt")),
                 QStringLiteral("text/plain"));
  recents.record(tmp.filePath(QStringLiteral("a.txt")),
                 QStringLiteral("text/plain"));

  DirectoryModel model;
  model.setRecentStore(&recents);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("asc"));
  model.setPath(QStringLiteral("recent://"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(proxy.rowCount(), 2);
  QCOMPARE(nameAt(proxy, 0), QStringLiteral("a.txt"));
  QCOMPARE(nameAt(proxy, 1), QStringLiteral("z.txt"));
}

void LocationAdaptersTest::recentDuplicateBasenames() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("a")));
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("b")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a/README.md"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b/README.md"))));

  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(tmp.filePath(QStringLiteral("a/README.md")),
                 QStringLiteral("text/markdown"));
  recents.record(tmp.filePath(QStringLiteral("b/README.md")),
                 QStringLiteral("text/markdown"));

  DirectoryModel model;
  model.setRecentStore(&recents);
  model.setPath(QStringLiteral("recent://"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), 2);
}

void LocationAdaptersTest::recordWhileViewingRecentDoesNotDeadlock() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString first = tmp.filePath(QStringLiteral("one.jpg"));
  const QString second = tmp.filePath(QStringLiteral("two.jpg"));
  QVERIFY(writeFile(first));
  QVERIFY(writeFile(second));

  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(first, QStringLiteral("image/jpeg"));

  DirectoryModel model;
  model.setRecentStore(&recents);
  model.setPath(QStringLiteral("recent://"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), 1);

  // Same path as Enter on Recents: record() used to emit entriesChanged
  // while still holding LOCK_EX; reload() then flock(LOCK_SH) deadlocked.
  recents.record(second, QStringLiteral("image/jpeg"));
  QVERIFY(QTest::qWaitFor([&] { return model.rowCount() == 2; }, 2000));
  QCOMPARE(model.rowCount(), 2);
}

void LocationAdaptersTest::emptyRequiresConfirm() {
  QTemporaryDir live;
  QVERIFY(live.isValid());
  const QString src = live.filePath(QStringLiteral("wipe-me.txt"));
  QVERIFY(writeFile(src));
  QString err;
  QString trashFile;
  QVERIFY2(TrashStore::prepareTrash(src, &trashFile, &err), qPrintable(err));
  QVERIFY(QFile::rename(src, trashFile));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  model.setPath(QStringLiteral("trash://"));
  QVERIFY(waitListingDone(model));
  QVERIFY(model.rowCount() >= 1);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":empty"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("confirm-dialog"));
  QCOMPARE(keys.promptKind(), QStringLiteral("empty-trash"));
  QVERIFY(keys.confirmOpen());
  QVERIFY(QFileInfo::exists(trashFile));
  QCOMPARE(model.rowCount(), 1);

  QVERIFY(keys.handleListKey(Qt::Key_Y, Qt::NoModifier, QStringLiteral("y")));
  QVERIFY(!keys.confirmOpen());
  QVERIFY(waitListingDone(model));
  QVERIFY(!QFileInfo::exists(trashFile));
  QCOMPARE(model.rowCount(), 0);

  model.setPath(live.path());
  QVERIFY(waitListingDone(model));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":empty"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("empty is only available in trash"));
}

void LocationAdaptersTest::sortDescKeepsDirsFirst() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("adir")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("c.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(QTest::qWaitFor([&] { return proxy.rowCount() == 3; }));
  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("desc"));
  QCOMPARE(proxy.data(proxy.index(0, 0), DirectoryModel::IsDirRole).toBool(),
           true);
  QCOMPARE(nameAt(proxy, 0), QStringLiteral("adir"));
}

void LocationAdaptersTest::sortByNameAndSize() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b.txt")), QByteArray("aa")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.txt")),
                    QByteArray("aaaaaaaa")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("c.txt")), QByteArray("a")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(QTest::qWaitFor([&] { return proxy.rowCount() == 3; }));

  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("asc"));
  QCOMPARE(nameAt(proxy, 0), QStringLiteral("a.txt"));
  QCOMPARE(nameAt(proxy, 1), QStringLiteral("b.txt"));
  QCOMPARE(nameAt(proxy, 2), QStringLiteral("c.txt"));

  proxy.setSortRoleName(QStringLiteral("size"));
  proxy.setSortOrder(QStringLiteral("asc"));
  QVERIFY(QTest::qWaitFor([&] {
    return nameAt(proxy, 0) == QStringLiteral("c.txt");
  }));
  QCOMPARE(nameAt(proxy, 2), QStringLiteral("a.txt"));
}

void LocationAdaptersTest::proxyRowMapFollowsSort() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("z.txt")), QByteArray("z")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.txt")), QByteArray("a")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("asc"));
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(QTest::qWaitFor([&] { return proxy.rowCount() == 2; }));

  QCOMPARE(nameAt(proxy, 0), QStringLiteral("a.txt"));
  QCOMPARE(nameAt(proxy, 1), QStringLiteral("z.txt"));
  QCOMPARE(proxy.rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("a.txt"));
  QCOMPARE(proxy.rowMap(1).value(QStringLiteral("name")).toString(),
           QStringLiteral("z.txt"));
  QCOMPARE(proxy.rowMap(0).value(QStringLiteral("path")).toString(),
           proxy.data(proxy.index(0, 0), DirectoryModel::PathRole).toString());
  // Source row 0 is whatever the lister emitted first — not the view order.
  QVERIFY(model.rowMap(0).value(QStringLiteral("name")).toString() ==
              QStringLiteral("z.txt") ||
          model.rowMap(0).value(QStringLiteral("name")).toString() ==
              QStringLiteral("a.txt"));
  if (model.rowMap(0).value(QStringLiteral("name")).toString() ==
      QStringLiteral("z.txt")) {
    QCOMPARE(proxy.rowMap(0).value(QStringLiteral("name")).toString(),
             QStringLiteral("a.txt"));
  }
}

void LocationAdaptersTest::configPersistsHiddenAndSort() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  {
    Config cfg(path);
    QVERIFY(cfg.writable());
    cfg.setShowHidden(true);
    cfg.setSortRole(QStringLiteral("mtime"));
    cfg.setSortOrder(QStringLiteral("desc"));
    cfg.setView(QStringLiteral("grid"));
    cfg.setGridSize(176);
    cfg.setLastPath(QStringLiteral("/tmp"));
    QVERIFY(cfg.save());
  }
  Config loaded(path);
  QVERIFY(loaded.showHidden());
  QCOMPARE(loaded.sortRole(), QStringLiteral("mtime"));
  QCOMPARE(loaded.sortOrder(), QStringLiteral("desc"));
  QCOMPARE(loaded.view(), QStringLiteral("grid"));
  QCOMPARE(loaded.gridSize(), 176);
  QCOMPARE(loaded.lastPath(), QStringLiteral("/tmp"));
}

void LocationAdaptersTest::configPersistsPanelLook() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  {
    Config cfg(path);
    QVERIFY(cfg.panelLookOpen());
    QCOMPARE(cfg.panelLookRatio(), 0.34);
    QCOMPARE(cfg.lookSize(), 360);
    QVERIFY(cfg.lookSide().isEmpty());
    cfg.setPanelLookOpen(false);
    cfg.setPanelLookRatio(0.43);
    cfg.setLookSize(412);
    cfg.setLookSide(QStringLiteral("left"));
    QVERIFY(cfg.save());
  }
  Config loaded(path);
  QVERIFY(!loaded.panelLookOpen());
  QCOMPARE(loaded.panelLookRatio(), 0.43);
  QCOMPARE(loaded.lookSize(), 412);
  QCOMPARE(loaded.lookSide(), QStringLiteral("left"));
  loaded.setPanelLookRatio(0.9);
  QCOMPARE(loaded.panelLookRatio(), 0.5);
  loaded.setLookSize(10);
  QCOMPARE(loaded.lookSize(), 240);
  loaded.setLookSide(QStringLiteral("diagonal"));
  QVERIFY(loaded.lookSide().isEmpty());
}

void LocationAdaptersTest::lastPathNeverPersistsSearch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  {
    Config cfg(path);
    cfg.setLastPath(QStringLiteral("/tmp"));
    QCOMPARE(cfg.lastPath(), QStringLiteral("/tmp"));
    cfg.setLastPath(QStringLiteral("search://"));
    QCOMPARE(cfg.lastPath(), QDir::homePath());
    cfg.setLastPath(QStringLiteral("trash://"));
    QCOMPARE(cfg.lastPath(), QStringLiteral("trash://"));
    cfg.setLastPath(QStringLiteral("recent://"));
    QCOMPARE(cfg.lastPath(), QStringLiteral("recent://"));
    QVERIFY(cfg.save());
  }

  QFile stale(path);
  QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Truncate));
  QVERIFY(stale.write("{\n  \"version\": 1,\n  \"lastPath\": \"search://\"\n}\n") >
          0);
  stale.close();

  Config loaded(path);
  QCOMPARE(loaded.lastPath(), QDir::homePath());
}

void LocationAdaptersTest::pinChipAfterHomeAndActivate() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("Work")));
  const QString work = QFileInfo(tmp.filePath(QStringLiteral("Work"))).absoluteFilePath();

  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();

  Config cfg(tmp.filePath(QStringLiteral("config.json")));
  DirectoryModel model;
  NavStack nav(&model);
  LocationChips chips;
  chips.setRegistry(&reg);
  chips.setConfig(&cfg);
  chips.setNav(&nav);
  chips.setDirectoryModel(&model);

  QVERIFY(chips.pin(work));
  QVERIFY(chips.isPinned(work));
  QStringList ids;
  QStringList labels;
  for (const QVariant &v : chips.chips()) {
    ids.append(v.toMap().value(QStringLiteral("id")).toString());
    labels.append(v.toMap().value(QStringLiteral("label")).toString());
  }
  QVERIFY(ids.contains(QStringLiteral("synchro.location.home")));
  QVERIFY(ids.contains(LocationChips::pinId(work)));
  QVERIFY(labels.contains(QStringLiteral("Work")));
  QCOMPARE(ids.indexOf(LocationChips::pinId(work)),
           ids.indexOf(QStringLiteral("synchro.location.home")) + 1);

  chips.activate(LocationChips::pinId(work));
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(work));

  QVERIFY(chips.unpin(work));
  QVERIFY(!chips.isPinned(work));
  for (const QVariant &v : chips.chips())
    QVERIFY(v.toMap().value(QStringLiteral("id")).toString() !=
            LocationChips::pinId(work));
}

void LocationAdaptersTest::pinPersistsInConfig() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("Pinned")));
  const QString pinned =
      QFileInfo(tmp.filePath(QStringLiteral("Pinned"))).absoluteFilePath();
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  {
    Config cfg(path);
    LocationChips chips;
    chips.setConfig(&cfg);
    QVERIFY(chips.pin(pinned));
  }
  Config loaded(path);
  QCOMPARE(loaded.pins().size(), 1);
  QCOMPARE(loaded.pins().first(), Config::normalizePin(pinned));
}

void LocationAdaptersTest::sqlBookmarkPersistsAndUpdates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  QString id;
  {
    Config cfg(path);
    id = cfg.saveSqlBookmark(
        QStringLiteral("big webp files"),
        QStringLiteral("select * from tree where extension = 'webp' and mb > 5"),
        tmp.path());
    QVERIFY(!id.isEmpty());
    QCOMPARE(cfg.saveSqlBookmark(
                 QStringLiteral("BIG WEBP FILES"),
                 QStringLiteral("select * from tree where extension = 'webp' and mb > 10"),
                 tmp.path()),
             id);
    QCOMPARE(cfg.sqlBookmarks().size(), 1);
    QVERIFY(cfg.save());
  }
  Config loaded(path);
  QCOMPARE(loaded.sqlBookmarks().size(), 1);
  const QVariantMap saved = loaded.sqlBookmarks().first().toMap();
  QCOMPARE(saved.value(QStringLiteral("id")).toString(), id);
  QCOMPARE(saved.value(QStringLiteral("name")).toString(),
           QStringLiteral("BIG WEBP FILES"));
  QVERIFY(saved.value(QStringLiteral("sql"))
              .toString()
              .contains(QStringLiteral("mb > 10")));
  QVERIFY(loaded.removeSqlBookmark(id));
  QVERIFY(loaded.sqlBookmarks().isEmpty());
}

void LocationAdaptersTest::sqlBookmarkIsALocation() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  Config cfg(tmp.filePath(QStringLiteral("config.json")));
  const QString id = cfg.saveSqlBookmark(
      QStringLiteral("big webp files"),
      QStringLiteral("select path from tree where extension = 'webp'"),
      tmp.path());
  QVERIFY(!id.isEmpty());

  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();
  LocationChips chips;
  chips.setRegistry(&reg);
  chips.setConfig(&cfg);

  const QString chipId = LocationChips::sqlBookmarkId(id);
  QStringList ids;
  for (const QVariant &value : chips.placeChips())
    ids.append(value.toMap().value(QStringLiteral("id")).toString());
  QCOMPARE(ids.indexOf(chipId),
           ids.indexOf(QStringLiteral("synchro.location.home")) + 1);

  QSignalSpy activated(&chips, &LocationChips::sqlBookmarkActivated);
  chips.activate(chipId);
  QCOMPARE(activated.size(), 1);
  const QList<QVariant> args = activated.takeFirst();
  QCOMPARE(args.at(0).toString(), QStringLiteral("big webp files"));
  QVERIFY(args.at(1).toString().contains(QStringLiteral("extension = 'webp'")));
  QCOMPARE(canon(args.at(2).toString()), canon(tmp.path()));
  QCOMPARE(args.at(3).toString(), id);

  chips.setChooserMode(true);
  for (const QVariant &value : chips.chips())
    QVERIFY(value.toMap().value(QStringLiteral("id")).toString() != chipId);
  chips.setChooserMode(false);
  QVERIFY(chips.removeSqlBookmark(chipId));
  for (const QVariant &value : chips.chips())
    QVERIFY(value.toMap().value(QStringLiteral("id")).toString() != chipId);
}

void LocationAdaptersTest::colonPinAndShiftPToggle() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("proj")));
  const QString proj =
      QFileInfo(tmp.filePath(QStringLiteral("proj"))).absoluteFilePath();

  Config cfg(tmp.filePath(QStringLiteral("config.json")));
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  LocationChips chips;
  chips.setConfig(&cfg);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setLocationChips(&chips);

  model.setPath(proj);
  QVERIFY(waitListingDone(model));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":pin"));
  keys.acceptField();
  QVERIFY(chips.isPinned(proj));
  QCOMPARE(keys.statusMessage(), QStringLiteral("pinned proj"));

  QVERIFY(keys.handleListKey(Qt::Key_B, Qt::ControlModifier, QString()));
  QVERIFY(!chips.isPinned(proj));

  // Keep the previous shortcut compatible for existing muscle memory.
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::ControlModifier, QString()));
  QVERIFY(chips.isPinned(proj));
  QVERIFY(keys.handleListKey(Qt::Key_B, Qt::ControlModifier, QString()));
  QVERIFY(!chips.isPinned(proj));
}

void LocationAdaptersTest::configUnknownVersionIsReadOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("config.json"));
  QVERIFY(writeFile(path, QByteArray("{\n  \"version\": 2,\n  "
                                     "\"showHidden\": true\n}\n")));
  Config cfg(path);
  QVERIFY(!cfg.writable());
  QVERIFY(cfg.showHidden());
  cfg.setShowHidden(false);
  QVERIFY(!cfg.save());
  Config again(path);
  QVERIFY(again.showHidden());
}

void LocationAdaptersTest::currentStatHasSizeMtimePerm() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("statme.txt"));
  QVERIFY(writeFile(path, QByteArray("hello-stat")));

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(QTest::qWaitFor([&] {
    const int row = findRow(model, QStringLiteral("statme.txt"));
    if (row < 0)
      return false;
    model.setCurrentIndex(row);
    const QVariantMap st = model.currentStat();
    return st.value(QStringLiteral("size")).toLongLong() > 0 &&
           st.value(QStringLiteral("mtime")).toLongLong() > 0 &&
           st.value(QStringLiteral("perm")).toString().size() == 10;
  }));
}

void LocationAdaptersTest::volumeParseFiltersNoiseAndKeepsMedia() {
  const QString mountinfo = QStringLiteral(
      "22 1 8:1 / / rw - ext4 /dev/sda1 rw\n"
      "23 22 0:21 / /proc rw - proc proc rw\n"
      "24 22 0:22 / /sys rw - sysfs sysfs rw\n"
      "25 22 0:23 / /dev rw - devtmpfs dev rw\n"
      "26 22 8:2 / /home rw - ext4 /dev/sda2 rw\n"
      "27 22 8:16 / /run/media/ryan/KINGSTON rw - vfat /dev/sdb1 rw\n"
      "28 22 0:47 / /snap/core/1 ro - squashfs /dev/loop0 ro\n"
      "29 22 0:48 / /run/user/1000/gvfs rw - fuse.gvfsd-fuse gvfsd-fuse rw\n"
      "30 22 253:1 /sub /bind rw - ext4 /dev/sda1 rw\n");
  const auto vols = VolumeStore::parseMountinfo(
      mountinfo, QStringLiteral("/home/ryan"));
  QStringList mounts;
  QString rootLabel;
  for (const auto &v : vols) {
    mounts.append(v.mountPoint);
    if (v.mountPoint == QLatin1String("/"))
      rootLabel = v.label;
  }
  QVERIFY(mounts.contains(QStringLiteral("/")));
  QCOMPARE(rootLabel, QStringLiteral("/"));
  QVERIFY(mounts.contains(QStringLiteral("/home")));
  QVERIFY(mounts.contains(QStringLiteral("/run/media/ryan/KINGSTON")));
  QVERIFY(!mounts.contains(QStringLiteral("/proc")));
  QVERIFY(!mounts.contains(QStringLiteral("/sys")));
  QVERIFY(!mounts.contains(QStringLiteral("/dev")));
  QVERIFY(!mounts.contains(QStringLiteral("/snap/core/1")));
  QVERIFY(!mounts.contains(QStringLiteral("/run/user/1000/gvfs")));
  QVERIFY(!mounts.contains(QStringLiteral("/bind")));
  VolumeStore::Volume usb;
  for (const auto &v : vols) {
    if (v.mountPoint.endsWith(QStringLiteral("KINGSTON")))
      usb = v;
  }
  QVERIFY(usb.extra);
  QVERIFY(usb.removable);
  QCOMPARE(usb.label, QStringLiteral("KINGSTON"));

  const QString btrfs = QStringLiteral(
      "36 1 0:26 /@ / rw,relatime - btrfs /dev/nvme0n1p2 rw\n"
      "37 36 0:26 /@home /home rw,relatime - btrfs /dev/nvme0n1p2 rw\n");
  const auto btrfsVols =
      VolumeStore::parseMountinfo(btrfs, QStringLiteral("/home/ryan"));
  QStringList btrfsMounts;
  for (const auto &v : btrfsVols)
    btrfsMounts.append(v.mountPoint);
  QVERIFY2(btrfsMounts.contains(QStringLiteral("/")),
           "btrfs /@ must still count as the / volume");
  QVERIFY(btrfsMounts.contains(QStringLiteral("/home")));
}

void LocationAdaptersTest::volumesListingAndRootTreeUp() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("usb")));
  const QString usb =
      QFileInfo(tmp.filePath(QStringLiteral("usb"))).absoluteFilePath();

  VolumeStore::Volume sys;
  sys.label = QStringLiteral("/");
  sys.mountPoint = QStringLiteral("/");
  sys.fstype = QStringLiteral("ext4");
  sys.total = 1000;
  sys.free = 400;
  VolumeStore::Volume stick;
  stick.label = QStringLiteral("KINGSTON");
  stick.mountPoint = usb;
  stick.fstype = QStringLiteral("vfat");
  stick.removable = true;
  stick.extra = true;
  stick.total = 64ll * 1024 * 1024 * 1024;
  stick.free = 18ll * 1024 * 1024 * 1024;
  stick.used = stick.total - stick.free;
  VolumeStore::instance().setInventoryForTest({sys, stick});

  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(QStringLiteral("volumes://"));
  QCOMPARE(model.path(), QStringLiteral("volumes://"));
  QVERIFY(model.isVolumes());
  QVERIFY(findRow(model, QStringLiteral("/")) >= 0);
  const int usbRow = findRow(model, QStringLiteral("KINGSTON"));
  QVERIFY(usbRow >= 0);
  QVERIFY(model.data(model.index(usbRow, 0), DirectoryModel::DetailRole)
              .toString()
              .contains(QStringLiteral("free")));
  QCOMPARE(model.data(model.index(usbRow, 0), DirectoryModel::UsedRole)
               .toLongLong(),
           stick.used);
  QCOMPARE(model.data(model.index(usbRow, 0), DirectoryModel::TotalRole)
               .toLongLong(),
           stick.total);
  QCOMPARE(model.data(model.index(usbRow, 0), DirectoryModel::PercentRole)
               .toInt(),
           71);

  model.setCurrentIndex(usbRow);
  model.activateCurrent();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(usb));
  QCOMPARE(model.volumeRoot(), usb);
  QVERIFY(model.volumeHint().contains(QStringLiteral("KINGSTON")));

  nav.goUp();
  QCOMPARE(model.path(), QStringLiteral("volumes://"));
}

void LocationAdaptersTest::extraVolumeChipAppears() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("usb")));
  const QString usb =
      QFileInfo(tmp.filePath(QStringLiteral("usb"))).absoluteFilePath();

  VolumeStore::Volume root;
  root.label = QStringLiteral("/");
  root.mountPoint = QStringLiteral("/");
  root.total = 5000;
  root.free = 1200;
  VolumeStore::Volume stick;
  stick.label = QStringLiteral("KINGSTON");
  stick.mountPoint = usb;
  stick.removable = true;
  stick.extra = true;
  stick.total = 1000;
  stick.free = 200;
  VolumeStore::instance().setInventoryForTest({root, stick});

  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();
  LocationChips chips;
  chips.setRegistry(&reg);
  QStringList ids;
  QStringList labels;
  for (const QVariant &v : chips.chips()) {
    ids.append(v.toMap().value(QStringLiteral("id")).toString());
    labels.append(v.toMap().value(QStringLiteral("label")).toString());
  }
  QVERIFY(ids.contains(QStringLiteral("synchro.location.volumes")));
  QVERIFY(ids.contains(QStringLiteral("volume:/")));
  QVERIFY(ids.contains(QStringLiteral("volume:") + usb));
  bool sawFree = false;
  for (const QString &label : labels) {
    if (label.contains(QStringLiteral("KINGSTON")))
      sawFree = true;
  }
  QVERIFY(sawFree);
}

void LocationAdaptersTest::diskPlacesSplitAndVolumesActiveOnlyOnInventory() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("usb")));
  const QString usb =
      QFileInfo(tmp.filePath(QStringLiteral("usb"))).absoluteFilePath();

  VolumeStore::Volume sys;
  sys.label = QStringLiteral("/");
  sys.mountPoint = QStringLiteral("/");
  sys.total = 5000;
  sys.free = 1200;
  VolumeStore::Volume stick;
  stick.label = QStringLiteral("KINGSTON");
  stick.mountPoint = usb;
  stick.removable = true;
  stick.extra = true;
  stick.total = 1000;
  stick.free = 200;
  VolumeStore::instance().setInventoryForTest({sys, stick});

  HandlerRegistry reg;
  reg.setScanEnv(false);
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(QStringLiteral("/tmp/synchro-no-user-handlers"));
  reg.scan();
  DirectoryModel model;
  LocationChips chips;
  chips.setRegistry(&reg);
  chips.setDirectoryModel(&model);

  QStringList places;
  for (const QVariant &v : chips.placeChips())
    places.append(v.toMap().value(QStringLiteral("id")).toString());
  QStringList disks;
  for (const QVariant &v : chips.diskChips())
    disks.append(v.toMap().value(QStringLiteral("id")).toString());
  QVERIFY(places.contains(QStringLiteral("synchro.location.home")));
  QVERIFY(!places.contains(QStringLiteral("synchro.location.volumes")));
  QVERIFY(!places.contains(QStringLiteral("volume:/")));
  QVERIFY(disks.contains(QStringLiteral("volume:/")));
  QVERIFY(disks.contains(QStringLiteral("volume:") + usb));
  QCOMPARE(chips.volumesChip().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.volumes"));

  model.setPath(usb);
  QVERIFY(waitListingDone(model));
  chips.refresh();
  QVERIFY(!chips.volumesChip().value(QStringLiteral("active")).toBool());
  bool usbOn = false;
  for (const QVariant &v : chips.diskChips()) {
    const QVariantMap m = v.toMap();
    if (m.value(QStringLiteral("id")).toString() ==
        QStringLiteral("volume:") + usb)
      usbOn = m.value(QStringLiteral("active")).toBool();
  }
  QVERIFY(usbOn);

  model.setPath(QStringLiteral("volumes://"));
  chips.refresh();
  QVERIFY(chips.volumesChip().value(QStringLiteral("active")).toBool());
}

QTEST_MAIN(LocationAdaptersTest)
#include "location_adapters_test.moc"
