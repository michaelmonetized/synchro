#include "Config.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerRegistry.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "TrashStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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
  void sortByNameAndSize();
  void configPersistsHiddenAndSort();
  void configUnknownVersionIsReadOnly();
  void currentStatHasSizeMtimePerm();

private:
  QTemporaryDir m_xdg;
};

void LocationAdaptersTest::initTestCase() {
  QVERIFY(m_xdg.isValid());
  qputenv("XDG_DATA_HOME", QFile::encodeName(m_xdg.path()));
}

void LocationAdaptersTest::cleanup() {
  QDir(TrashStore::filesDir()).removeRecursively();
  QDir(TrashStore::infoDir()).removeRecursively();
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
  QCOMPARE(list.size(), 3);
  QCOMPARE(list.at(0).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.home"));
  QCOMPARE(list.at(1).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.recent"));
  QCOMPARE(list.at(2).toMap().value(QStringLiteral("id")).toString(),
           QStringLiteral("synchro.location.trash"));
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
  QVERIFY(keys.handleListKey(Qt::Key_G, Qt::NoModifier, QStringLiteral("g")));
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
    cfg.setLastPath(QStringLiteral("/tmp"));
    QVERIFY(cfg.save());
  }
  Config loaded(path);
  QVERIFY(loaded.showHidden());
  QCOMPARE(loaded.sortRole(), QStringLiteral("mtime"));
  QCOMPARE(loaded.sortOrder(), QStringLiteral("desc"));
  QCOMPARE(loaded.view(), QStringLiteral("grid"));
  QCOMPARE(loaded.lastPath(), QStringLiteral("/tmp"));
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

QTEST_MAIN(LocationAdaptersTest)
#include "location_adapters_test.moc"
