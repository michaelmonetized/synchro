#include "DirectoryLister.h"
#include "DirectoryModel.h"
#include "NavStack.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

namespace {

constexpr int kFixtureCount = 2000;
constexpr int kFileCount = 1770;
constexpr int kDirCount = 200;
constexpr int kLinkFileCount = 15;
constexpr int kLinkDirCount = 5;
constexpr int kHiddenFileCount = 8;
constexpr int kHiddenDirCount = 2;

static_assert(kFileCount + kDirCount + kLinkFileCount + kLinkDirCount +
                      kHiddenFileCount + kHiddenDirCount ==
                  kFixtureCount,
              "fixture counts must sum to 2000");

QString pad(int n, int width) {
  return QStringLiteral("%1").arg(n, width, 10, QLatin1Char('0'));
}

int makeMixedFixture(const QString &root) {
  QDir dir(root);
  for (int i = 0; i < kFileCount; ++i) {
    QFile f(dir.filePath(QStringLiteral("file_%1.txt").arg(pad(i, 4))));
    if (!f.open(QIODevice::WriteOnly))
      return -1;
    f.write("x", 1);
  }
  for (int i = 0; i < kDirCount; ++i) {
    if (!dir.mkdir(QStringLiteral("dir_%1").arg(pad(i, 3))))
      return -1;
  }
  const QString fileTarget = QStringLiteral("file_0000.txt");
  const QString dirTarget = QStringLiteral("dir_000");
  for (int i = 0; i < kLinkFileCount; ++i) {
    if (!QFile::link(
            fileTarget,
            dir.filePath(QStringLiteral("linkfile_%1").arg(pad(i, 2)))))
      return -1;
  }
  for (int i = 0; i < kLinkDirCount; ++i) {
    if (!QFile::link(dirTarget,
                     dir.filePath(QStringLiteral("linkdir_%1").arg(pad(i, 2)))))
      return -1;
  }
  for (int i = 0; i < kHiddenFileCount; ++i) {
    QFile f(dir.filePath(QStringLiteral(".hidden_%1").arg(pad(i, 2))));
    if (!f.open(QIODevice::WriteOnly))
      return -1;
    f.write("h", 1);
  }
  for (int i = 0; i < kHiddenDirCount; ++i) {
    if (!dir.mkdir(QStringLiteral(".hiddendir_%1").arg(pad(i, 2))))
      return -1;
  }
  return kFixtureCount;
}

int visibleExpected() {
  return kFileCount + kDirCount + kLinkFileCount + kLinkDirCount;
}

bool waitRows(DirectoryModel &model, int minRows, int timeoutMs = 3000) {
  return QTest::qWaitFor([&] { return model.rowCount() >= minRows; },
                         timeoutMs);
}

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

QVariant roleAt(const DirectoryModel &model, int row, int role) {
  return model.data(model.index(row, 0), role);
}

int findRow(const DirectoryModel &model, const QString &name) {
  for (int i = 0; i < model.rowCount(); ++i) {
    if (roleAt(model, i, DirectoryModel::NameRole).toString() == name)
      return i;
  }
  return -1;
}

} // namespace

class DirectoryModelTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void fixtureHas2000Names();
  void setPathDoesNotBlockGuiThread();
  void warmFirstRowsUnder80ms();
  void direntDirIsDirOnFirstPaint();
  void symlinkToDirClassifiesAfterPending();
  void pendingStatedBeforeBulk();
  void hiddenFilteredByDefault();
  void showHiddenRevealsDotfiles();
  void rolesArePopulated();
  void cursorMoves();
  void activateDirChangesPath();
  void activateFileEmitsAndStays();
  void activatePendingSymlinkToFile();
  void pendingActivateDroppedIfNameDeleted();
  void staleSetPathDoesNotClobber();
  void activatePendingSymlinkToDir();
  void listingTwoThousandLeavesGuiResponsive();
  void warmTimeToFirstFrame();
  void watcherCreateDeleteUpdatesModel();
  void watcherRenameUpdatesModel();
  void watchedDirGoneNavigatesUp();
  void watcherDoesNotAdoptSubdirCreates();
  void navStackBackForward();
  void navStackGoUpSelectsChild();
  void activateRecordsHistory();
  void goHomeJumpsToHome();
  void pathSegmentsTildeAndRoot();
  void pathSegmentsWhenHomeIsSymlink();
  void samePathSetPathIsNoop();
  void staleWatchCreateDoesNotClobberNewPath();
  void staleDeleteSelfDoesNotKickNewPath();
  void deleteDuringListingIsNotResurrected();
  void visibleThumbsFillPngAndFolderMosaic();
  void createdFileGetsThumbnail();
  void refreshThumbsUpdatesFolderMosaicOnly();

private:
  QTemporaryDir m_fixture;
  QTemporaryDir m_scratch;
};

void DirectoryModelTest::initTestCase() {
  QVERIFY(m_fixture.isValid());
  QVERIFY(m_scratch.isValid());
  QCOMPARE(makeMixedFixture(m_fixture.path()), kFixtureCount);
  QFile marker(m_scratch.filePath(QStringLiteral("scratch.txt")));
  QVERIFY(marker.open(QIODevice::WriteOnly));
  marker.write("s", 1);
}

void DirectoryModelTest::fixtureHas2000Names() {
  const QStringList names =
      QDir(m_fixture.path())
          .entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
  QCOMPARE(names.size(), kFixtureCount);
}

void DirectoryModelTest::setPathDoesNotBlockGuiThread() {
  DirectoryModel model;
  bool inserted = false;
  connect(&model, &QAbstractItemModel::rowsInserted, this,
          [&] { inserted = true; });

  QElapsedTimer t;
  t.start();
  model.setPath(m_fixture.path());
  const qint64 blocked = t.elapsed();
  QVERIFY2(!inserted,
           "rowsInserted must be queued, not synchronous in setPath");
  QVERIFY2(blocked < 20,
           qPrintable(QStringLiteral("setPath blocked %1ms").arg(blocked)));
  QVERIFY(waitRows(model, 1));
}

void DirectoryModelTest::warmFirstRowsUnder80ms() {
  DirectoryModel model;
  model.setPath(m_fixture.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), visibleExpected());

  model.setPath(m_scratch.path());
  QVERIFY(waitListingDone(model));

  QElapsedTimer t;
  qint64 firstMs = -1;
  int firstRows = 0;
  connect(&model, &DirectoryModel::firstRowsInserted, this,
          [&](qint64 ms, int rows) {
            firstMs = ms;
            firstRows = rows;
          });
  t.start();
  model.setPath(m_fixture.path());
  QVERIFY(QTest::qWaitFor([&] { return firstMs >= 0; }, 3000));
  const qint64 wall = t.elapsed();

  qInfo("warm readdir->first_rowsInserted: model=%lldms wall=%lldms rows=%d",
        static_cast<long long>(firstMs), static_cast<long long>(wall),
        firstRows);
  QVERIFY2(firstRows > 0, "first batch must insert visible rows");
  QVERIFY2(firstMs < 80,
           qPrintable(QStringLiteral("warm first rowsInserted %1ms (gate 80)")
                          .arg(firstMs)));
}

void DirectoryModelTest::direntDirIsDirOnFirstPaint() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("plaindir")));
  QFile f(tmp.filePath(QStringLiteral("plain.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();

  DirectoryModel model;
  QSignalSpy first(&model, &DirectoryModel::firstRowsInserted);
  model.setPath(tmp.path());
  QVERIFY(first.wait(2000));

  const int dirRow = findRow(model, QStringLiteral("plaindir"));
  const int fileRow = findRow(model, QStringLiteral("plain.txt"));
  QVERIFY(dirRow >= 0);
  QVERIFY(fileRow >= 0);
  QCOMPARE(roleAt(model, dirRow, DirectoryModel::IsDirRole).toBool(), true);
  QCOMPARE(roleAt(model, dirRow, DirectoryModel::DirKindRole).toString(),
           QStringLiteral("posix"));
  QCOMPARE(roleAt(model, fileRow, DirectoryModel::IsDirRole).toBool(), false);
}

void DirectoryModelTest::symlinkToDirClassifiesAfterPending() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("targetdir")));
  QVERIFY(QFile::link(QStringLiteral("targetdir"),
                      tmp.filePath(QStringLiteral("thelink"))));
  QFile f(tmp.filePath(QStringLiteral("targetfile")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();
  QVERIFY(QFile::link(QStringLiteral("targetfile"),
                      tmp.filePath(QStringLiteral("filelink"))));

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  const int linkDir = findRow(model, QStringLiteral("thelink"));
  const int linkFile = findRow(model, QStringLiteral("filelink"));
  QVERIFY(linkDir >= 0);
  QVERIFY(linkFile >= 0);
  QCOMPARE(roleAt(model, linkDir, DirectoryModel::IsSymlinkRole).toBool(),
           true);
  QCOMPARE(roleAt(model, linkDir, DirectoryModel::IsDirRole).toBool(), true);
  QCOMPARE(roleAt(model, linkDir, DirectoryModel::DirKindRole).toString(),
           QStringLiteral("posix"));
  QCOMPARE(roleAt(model, linkFile, DirectoryModel::IsSymlinkRole).toBool(),
           true);
  QCOMPARE(roleAt(model, linkFile, DirectoryModel::IsDirRole).toBool(), false);
}

void DirectoryModelTest::pendingStatedBeforeBulk() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 80; ++i) {
    QFile f(tmp.filePath(QStringLiteral("f_%1").arg(i)));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x", 1);
  }
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("d0")));
  QVERIFY(QFile::link(QStringLiteral("d0"),
                      tmp.filePath(QStringLiteral("pend_link"))));

  QThread worker;
  DirectoryLister lister;
  lister.moveToThread(&worker);
  worker.start();

  QStringList firstPriorityNames;
  bool sawPriority = false;
  bool sawBulk = false;
  bool bulkBeforePriority = false;
  connect(&lister, &DirectoryLister::statsReady, this,
          [&](quint64, const QVector<DirectoryEntry> &batch, bool priority) {
            if (priority) {
              if (!sawPriority) {
                for (const DirectoryEntry &e : batch)
                  firstPriorityNames.append(e.name);
              }
              sawPriority = true;
            } else {
              if (!sawPriority)
                bulkBeforePriority = true;
              sawBulk = true;
            }
          });

  QMetaObject::invokeMethod(&lister, "requestList", Qt::QueuedConnection,
                            Q_ARG(quint64, 1), Q_ARG(QString, tmp.path()));
  QVERIFY(QTest::qWaitFor([&] { return sawPriority && sawBulk; }, 3000));
  worker.quit();
  worker.wait();

  QVERIFY2(!bulkBeforePriority, "pending fstatat must run before bulk stats");
  QVERIFY(firstPriorityNames.contains(QStringLiteral("pend_link")));
}

void DirectoryModelTest::hiddenFilteredByDefault() {
  DirectoryModel model;
  QCOMPARE(model.showHidden(), false);
  model.setPath(m_fixture.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), visibleExpected());
  QVERIFY(findRow(model, QStringLiteral(".hidden_00")) < 0);
  QVERIFY(findRow(model, QStringLiteral("file_0000.txt")) >= 0);
}

void DirectoryModelTest::showHiddenRevealsDotfiles() {
  DirectoryModel model;
  model.setPath(m_fixture.path());
  QVERIFY(waitListingDone(model));
  model.setShowHidden(true);
  QCOMPARE(model.rowCount(), kFixtureCount);
  QVERIFY(findRow(model, QStringLiteral(".hidden_00")) >= 0);
  QVERIFY(findRow(model, QStringLiteral(".hiddendir_00")) >= 0);
  QCOMPARE(roleAt(model, findRow(model, QStringLiteral(".hidden_00")),
                  DirectoryModel::IsHiddenRole)
               .toBool(),
           true);
}

void DirectoryModelTest::rolesArePopulated() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath(QStringLiteral("readme.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("hello", 5);
  f.close();

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findRow(model, QStringLiteral("readme.txt"));
  QVERIFY(row >= 0);
  QCOMPARE(roleAt(model, row, DirectoryModel::NameRole).toString(),
           QStringLiteral("readme.txt"));
  QVERIFY(roleAt(model, row, DirectoryModel::PathRole)
              .toString()
              .endsWith(QStringLiteral("readme.txt")));
  QVERIFY(roleAt(model, row, DirectoryModel::UriRole).toUrl().isLocalFile());
  QCOMPARE(roleAt(model, row, DirectoryModel::IsDirRole).toBool(), false);
  QCOMPARE(roleAt(model, row, DirectoryModel::SizeRole).toLongLong(), 5);
  QVERIFY(roleAt(model, row, DirectoryModel::MtimeRole).toLongLong() > 0);
  QVERIFY(!roleAt(model, row, DirectoryModel::MimeRole).toString().isEmpty());
  QVERIFY(
      !roleAt(model, row, DirectoryModel::IconNameRole).toString().isEmpty());
  QCOMPARE(roleAt(model, row, DirectoryModel::ThumbnailRole).toString(),
           QString());
  QCOMPARE(roleAt(model, row, DirectoryModel::IsHiddenRole).toBool(), false);
  QCOMPARE(roleAt(model, row, DirectoryModel::IsSymlinkRole).toBool(), false);
  QCOMPARE(roleAt(model, row, DirectoryModel::DirKindRole).toString(),
           QStringLiteral("posix"));
}

void DirectoryModelTest::cursorMoves() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 5; ++i) {
    QFile f(tmp.filePath(QStringLiteral("n%1").arg(i)));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x", 1);
  }
  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(model.rowCount() >= 5);
  QCOMPARE(model.currentIndex(), 0);
  model.moveCursor(1);
  QCOMPARE(model.currentIndex(), 1);
  model.moveCursor(-1);
  QCOMPARE(model.currentIndex(), 0);
  model.moveCursor(-10);
  QCOMPARE(model.currentIndex(), 0);
  model.moveCursor(100);
  QCOMPARE(model.currentIndex(), model.rowCount() - 1);
}

void DirectoryModelTest::activateDirChangesPath() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));
  QFile f(tmp.filePath(QStringLiteral("child/inside.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findRow(model, QStringLiteral("child"));
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  model.activateCurrent();
  QVERIFY(waitListingDone(model));
  QVERIFY(model.path().endsWith(QStringLiteral("child")));
  QVERIFY(findRow(model, QStringLiteral("inside.txt")) >= 0);
}

void DirectoryModelTest::activateFileEmitsAndStays() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath(QStringLiteral("readme.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();

  DirectoryModel model;
  QSignalSpy spy(&model, &DirectoryModel::fileActivated);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findRow(model, QStringLiteral("readme.txt"));
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  const QString before = model.path();
  model.activateCurrent();
  QCOMPARE(model.path(), before);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(QFileInfo(spy.at(0).at(0).toString()).fileName(),
           QStringLiteral("readme.txt"));
}

void DirectoryModelTest::activatePendingSymlinkToFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile target(tmp.filePath(QStringLiteral("targetfile")));
  QVERIFY(target.open(QIODevice::WriteOnly));
  target.write("x", 1);
  target.close();
  QVERIFY(QFile::link(QStringLiteral("targetfile"),
                      tmp.filePath(QStringLiteral("filelink"))));

  DirectoryModel model;
  QSignalSpy spy(&model, &DirectoryModel::fileActivated);
  QString kindAtFirstPaint;
  connect(&model, &DirectoryModel::firstRowsInserted, this, [&] {
    const int row = findRow(model, QStringLiteral("filelink"));
    if (row < 0)
      return;
    kindAtFirstPaint =
        roleAt(model, row, DirectoryModel::DirKindRole).toString();
    model.setCurrentIndex(row);
    model.activateCurrent();
  });
  model.setPath(tmp.path());
  QVERIFY(QTest::qWaitFor([&] { return spy.count() == 1; }, 3000));
  QCOMPARE(kindAtFirstPaint, QStringLiteral("pending"));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  QVERIFY(spy.at(0).at(0).toString().endsWith(QStringLiteral("filelink")));
}

void DirectoryModelTest::pendingActivateDroppedIfNameDeleted() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile target(tmp.filePath(QStringLiteral("targetfile")));
  QVERIFY(target.open(QIODevice::WriteOnly));
  target.write("x", 1);
  target.close();
  QVERIFY(QFile::link(QStringLiteral("targetfile"),
                      tmp.filePath(QStringLiteral("filelink"))));

  DirectoryModel model;
  QSignalSpy spy(&model, &DirectoryModel::fileActivated);
  bool hadPending = false;
  bool cleared = false;
  connect(&model, &DirectoryModel::firstRowsInserted, this, [&] {
    const int row = findRow(model, QStringLiteral("filelink"));
    if (row < 0)
      return;
    model.setCurrentIndex(row);
    model.activateCurrent();
    hadPending = !model.m_pendingActivate.isEmpty();
    QFile::remove(tmp.filePath(QStringLiteral("filelink")));
    model.removeByName(QStringLiteral("filelink"));
    cleared = model.m_pendingActivate.isEmpty();
  });
  model.setPath(tmp.path());
  QVERIFY(QTest::qWaitFor([&] { return hadPending && !model.listing(); }, 3000));
  QVERIFY(cleared);
  QCOMPARE(spy.count(), 0);

  QVERIFY(QFile::link(QStringLiteral("targetfile"),
                      tmp.filePath(QStringLiteral("filelink"))));
  QVERIFY(QTest::qWaitFor(
      [&] { return findRow(model, QStringLiteral("filelink")) >= 0; }, 3000));
  QVERIFY(QTest::qWaitFor(
      [&] {
        const int row = findRow(model, QStringLiteral("filelink"));
        return row >= 0 && roleAt(model, row, DirectoryModel::DirKindRole)
                                   .toString() != QStringLiteral("pending");
      },
      3000));
  QCOMPARE(spy.count(), 0);
}

void DirectoryModelTest::staleSetPathDoesNotClobber() {
  DirectoryModel model;
  model.setPath(m_fixture.path());
  model.setPath(m_scratch.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(m_scratch.path()).canonicalFilePath());
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(roleAt(model, 0, DirectoryModel::NameRole).toString(),
           QStringLiteral("scratch.txt"));
  QVERIFY(findRow(model, QStringLiteral("file_0000.txt")) < 0);
}

void DirectoryModelTest::activatePendingSymlinkToDir() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("targetdir")));
  QFile inside(tmp.filePath(QStringLiteral("targetdir/inside.txt")));
  QVERIFY(inside.open(QIODevice::WriteOnly));
  inside.write("x", 1);
  inside.close();
  QVERIFY(QFile::link(QStringLiteral("targetdir"),
                      tmp.filePath(QStringLiteral("thelink"))));

  DirectoryModel model;
  QString kindAtFirstPaint;
  connect(&model, &DirectoryModel::firstRowsInserted, this, [&] {
    const int row = findRow(model, QStringLiteral("thelink"));
    if (row < 0)
      return;
    kindAtFirstPaint =
        roleAt(model, row, DirectoryModel::DirKindRole).toString();
    model.setCurrentIndex(row);
    model.activateCurrent();
  });
  model.setPath(tmp.path());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(model.path()).fileName() ==
               QStringLiteral("targetdir");
      },
      3000));
  QCOMPARE(kindAtFirstPaint, QStringLiteral("pending"));
  QVERIFY(waitListingDone(model));
  QVERIFY(findRow(model, QStringLiteral("inside.txt")) >= 0);
}

void DirectoryModelTest::listingTwoThousandLeavesGuiResponsive() {
  DirectoryModel model;
  int timerFires = 0;
  QTimer ticker;
  ticker.setInterval(0);
  connect(&ticker, &QTimer::timeout, this, [&] { ++timerFires; });
  ticker.start();

  model.setPath(m_fixture.path());
  QVERIFY(waitListingDone(model));
  QVERIFY2(timerFires > 0,
           "0ms timer must fire while/around 2k listing (GUI not blocked)");
  QCOMPARE(model.rowCount(), visibleExpected());
}

void DirectoryModelTest::warmTimeToFirstFrame() {
  QTemporaryDir empty;
  QVERIFY(empty.isValid());

  DirectoryModel model;
  model.setPath(m_fixture.path());
  QVERIFY(waitListingDone(model));
  model.setPath(empty.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), 0);

  static const char kQml[] = R"QML(
import QtQuick
ListView {
    id: root
    objectName: "benchList"
    anchors.fill: parent
    model: fileModel
    reuseItems: true
    cacheBuffer: 160
    delegate: Text {
        required property string name
        required property bool isDir
        width: ListView.view ? ListView.view.width : 800
        height: 18
        text: (isDir ? "d " : "  ") + name
    }
}
)QML";

  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("fileModel"), &model);
  QQmlComponent component(&engine);
  component.setData(QByteArray(kQml), QUrl(QStringLiteral("qrc:/bench.qml")));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));

  QQuickWindow window;
  window.resize(800, 600);
  QObject *created = component.create();
  QVERIFY(created);
  auto *item = qobject_cast<QQuickItem *>(created);
  QVERIFY(item);
  item->setParentItem(window.contentItem());
  item->setSize(QSizeF(800, 600));

  bool sawEmptyFrame = false;
  bool sawFirstRows = false;
  qint64 ttf = -1;
  QElapsedTimer t;
  connect(&model, &DirectoryModel::firstRowsInserted, this,
          [&] { sawFirstRows = true; });
  connect(&window, &QQuickWindow::frameSwapped, this, [&] {
    if (!t.isValid()) {
      if (model.rowCount() == 0)
        sawEmptyFrame = true;
      return;
    }
    if (sawFirstRows && model.rowCount() > 0 && ttf < 0)
      ttf = t.elapsed();
  });

  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  if (!QTest::qWaitFor([&] { return sawEmptyFrame; }, 2000)) {
    QSKIP("warm TTF: no empty frameSwapped before setPath "
          "(offscreen/software produced no frame)");
  }
  t.start();
  model.setPath(m_fixture.path());
  const bool gotFrame = QTest::qWaitFor([&] { return ttf >= 0; }, 3000);
  QVERIFY(waitListingDone(model));

  if (!gotFrame || ttf < 0) {
    QSKIP("warm TTF: no frameSwapped after first rows "
          "(offscreen/software produced no frame)");
  }

  qInfo("warm time-to-first-frame: %lldms (gate 80ms); first_rows=%lldms",
        static_cast<long long>(ttf),
        static_cast<long long>(model.lastFirstRowsMs()));
  QVERIFY2(ttf < 80,
           qPrintable(QStringLiteral("warm TTF %1ms exceeds 80ms").arg(ttf)));
}

void DirectoryModelTest::watcherCreateDeleteUpdatesModel() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), 0);

  QFile f(tmp.filePath(QStringLiteral("appeared.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();
  QVERIFY(QTest::qWaitFor(
      [&] { return findRow(model, QStringLiteral("appeared.txt")) >= 0; },
      2000));
  const int row = findRow(model, QStringLiteral("appeared.txt"));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return roleAt(model, row, DirectoryModel::DirKindRole).toString() !=
               QStringLiteral("pending");
      },
      2000));
  QCOMPARE(roleAt(model, row, DirectoryModel::IsDirRole).toBool(), false);

  QVERIFY(f.remove());
  QVERIFY(QTest::qWaitFor(
      [&] { return findRow(model, QStringLiteral("appeared.txt")) < 0; },
      2000));
}

void DirectoryModelTest::watcherRenameUpdatesModel() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QFile f(tmp.filePath(QStringLiteral("oldname.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(findRow(model, QStringLiteral("oldname.txt")) >= 0);

  QVERIFY(QFile::rename(tmp.filePath(QStringLiteral("oldname.txt")),
                        tmp.filePath(QStringLiteral("newname.txt"))));
  QVERIFY(QTest::qWaitFor(
      [&] { return findRow(model, QStringLiteral("newname.txt")) >= 0; },
      2000));
  QVERIFY(findRow(model, QStringLiteral("oldname.txt")) < 0);
}

void DirectoryModelTest::watchedDirGoneNavigatesUp() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));
  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  model.setPath(tmp.filePath(QStringLiteral("child")));
  QVERIFY(waitListingDone(model));
  QVERIFY(model.path().endsWith(QStringLiteral("child")));
  QVERIFY(nav.canGoBack());

  QVERIFY(QDir(tmp.path()).rmdir(QStringLiteral("child")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(model.path()).canonicalFilePath() ==
               QFileInfo(tmp.path()).canonicalFilePath();
      },
      2000));
  while (nav.canGoBack()) {
    nav.goBack();
    QVERIFY2(QFileInfo(model.path()).isDir(),
             qPrintable(QStringLiteral("history restored missing path %1")
                            .arg(model.path())));
  }
}

void DirectoryModelTest::watcherDoesNotAdoptSubdirCreates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));
  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int before = model.rowCount();

  QFile f(tmp.filePath(QStringLiteral("sub/inside.txt")));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("x", 1);
  f.close();
  QTest::qWait(120);
  QCOMPARE(model.rowCount(), before);
  QVERIFY(findRow(model, QStringLiteral("inside.txt")) < 0);
  QVERIFY(findRow(model, QStringLiteral("sub")) >= 0);
}

void DirectoryModelTest::navStackBackForward() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("a/b")));
  QFile leaf(tmp.filePath(QStringLiteral("a/b/leaf.txt")));
  QVERIFY(leaf.open(QIODevice::WriteOnly));
  leaf.write("x", 1);
  leaf.close();

  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(!nav.canGoBack());
  QVERIFY(!nav.canGoForward());

  nav.navigate(tmp.filePath(QStringLiteral("a")));
  QVERIFY(waitListingDone(model));
  QVERIFY(nav.canGoBack());
  QVERIFY(!nav.canGoForward());

  nav.navigate(tmp.filePath(QStringLiteral("a/b")));
  QVERIFY(waitListingDone(model));
  QVERIFY(findRow(model, QStringLiteral("leaf.txt")) >= 0);

  nav.goBack();
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).fileName(), QStringLiteral("a"));
  QVERIFY(nav.canGoForward());

  nav.goForward();
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).fileName(), QStringLiteral("b"));
  QVERIFY(findRow(model, QStringLiteral("leaf.txt")) >= 0);
}

void DirectoryModelTest::navStackGoUpSelectsChild() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));
  QFile inside(tmp.filePath(QStringLiteral("child/inside.txt")));
  QVERIFY(inside.open(QIODevice::WriteOnly));
  inside.write("x", 1);
  inside.close();

  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(tmp.filePath(QStringLiteral("child")));
  QVERIFY(waitListingDone(model));
  nav.goUp();
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return model.currentIndex() >= 0 &&
               roleAt(model, model.currentIndex(), DirectoryModel::NameRole)
                       .toString() == QStringLiteral("child");
      },
      2000));
}

void DirectoryModelTest::activateRecordsHistory() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));

  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(!nav.canGoBack());
  const int row = findRow(model, QStringLiteral("child"));
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  model.activateCurrent();
  QVERIFY(waitListingDone(model));
  QVERIFY(model.path().endsWith(QStringLiteral("child")));
  QVERIFY(nav.canGoBack());

  nav.goBack();
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return model.currentIndex() >= 0 &&
               roleAt(model, model.currentIndex(), DirectoryModel::NameRole)
                       .toString() == QStringLiteral("child");
      },
      2000));
}

void DirectoryModelTest::goHomeJumpsToHome() {
  DirectoryModel model;
  NavStack nav(&model);
  model.setPath(m_scratch.path());
  QVERIFY(waitListingDone(model));
  nav.goHome();
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(QDir::homePath()).canonicalFilePath());
  QVERIFY(nav.canGoBack());
  nav.goBack();
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(m_scratch.path()).canonicalFilePath());
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.rowCount(), 1);
}

void DirectoryModelTest::pathSegmentsTildeAndRoot() {
  DirectoryModel model;
  NavStack nav(&model);
  const QString home = nav.homePath();

  const QVariantList homeSegs = nav.segmentsFor(home);
  QCOMPARE(homeSegs.size(), 1);
  QCOMPARE(homeSegs.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("~"));
  QCOMPARE(homeSegs.at(0).toMap().value(QStringLiteral("path")).toString(),
           home);

  const QVariantList child =
      nav.segmentsFor(home + QStringLiteral("/Projects/foo"));
  QCOMPARE(child.size(), 3);
  QCOMPARE(child.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("~"));
  QCOMPARE(child.at(1).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("Projects"));
  QCOMPARE(child.at(2).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("foo"));
  QCOMPARE(child.at(2).toMap().value(QStringLiteral("path")).toString(),
           home + QStringLiteral("/Projects/foo"));

  const QVariantList root = nav.segmentsFor(QStringLiteral("/"));
  QCOMPARE(root.size(), 1);
  QCOMPARE(root.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("/"));

  const QVariantList abs = nav.segmentsFor(QStringLiteral("/usr/bin"));
  QCOMPARE(abs.size(), 3);
  QCOMPARE(abs.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("/"));
  QCOMPARE(abs.at(1).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("usr"));
  QCOMPARE(abs.at(2).toMap().value(QStringLiteral("path")).toString(),
           QStringLiteral("/usr/bin"));
}

void DirectoryModelTest::pathSegmentsWhenHomeIsSymlink() {
  QTemporaryDir real;
  QVERIFY(real.isValid());
  QTemporaryDir linkParent;
  QVERIFY(linkParent.isValid());
  const QString link = linkParent.filePath(QStringLiteral("home_link"));
  QVERIFY(QFile::link(real.path(), link));
  const QString canon = QFileInfo(link).canonicalFilePath();
  QVERIFY(!canon.isEmpty());
  QVERIFY(canon != link);

  const QByteArray oldHome = qgetenv("HOME");
  qputenv("HOME", QFile::encodeName(link));
  struct RestoreHome {
    QByteArray old;
    ~RestoreHome() { qputenv("HOME", old); }
  } restoreHome{oldHome};

  DirectoryModel model;
  NavStack nav(&model);
  QCOMPARE(nav.homePath(), canon);
  const QVariantList homeSegs = nav.segmentsFor(nav.homePath());
  QCOMPARE(homeSegs.size(), 1);
  QCOMPARE(homeSegs.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("~"));
  QCOMPARE(homeSegs.at(0).toMap().value(QStringLiteral("path")).toString(),
           canon);

  const QVariantList child =
      nav.segmentsFor(canon + QStringLiteral("/Projects"));
  QCOMPARE(child.size(), 2);
  QCOMPARE(child.at(0).toMap().value(QStringLiteral("label")).toString(),
           QStringLiteral("~"));
  QCOMPARE(child.at(1).toMap().value(QStringLiteral("path")).toString(),
           canon + QStringLiteral("/Projects"));
}

void DirectoryModelTest::staleWatchCreateDoesNotClobberNewPath() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("b")));
  QFile keep(tmp.filePath(QStringLiteral("b/keep.txt")));
  QVERIFY(keep.open(QIODevice::WriteOnly));
  keep.write("k", 1);
  keep.close();

  DirectoryModel model;
  model.setPath(tmp.filePath(QStringLiteral("a")));
  QVERIFY(waitListingDone(model));

  QFile ghost(tmp.filePath(QStringLiteral("a/ghost")));
  QVERIFY(ghost.open(QIODevice::WriteOnly));
  ghost.write("g", 1);
  ghost.close();
  model.setPath(tmp.filePath(QStringLiteral("b")));
  QVERIFY(waitListingDone(model));
  QTest::qWait(80);
  QVERIFY(findRow(model, QStringLiteral("ghost")) < 0);
  QVERIFY(findRow(model, QStringLiteral("keep.txt")) >= 0);
  QCOMPARE(QFileInfo(model.path()).fileName(), QStringLiteral("b"));
}

void DirectoryModelTest::staleDeleteSelfDoesNotKickNewPath() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("b")));
  QFile keep(tmp.filePath(QStringLiteral("b/keep.txt")));
  QVERIFY(keep.open(QIODevice::WriteOnly));
  keep.write("k", 1);
  keep.close();

  DirectoryModel model;
  model.setPath(tmp.filePath(QStringLiteral("a")));
  QVERIFY(waitListingDone(model));
  QVERIFY(QDir(tmp.path()).rmdir(QStringLiteral("a")));
  model.setPath(tmp.filePath(QStringLiteral("b")));
  QVERIFY(waitListingDone(model));
  QTest::qWait(80);
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.filePath(QStringLiteral("b"))).canonicalFilePath());
  QVERIFY(findRow(model, QStringLiteral("keep.txt")) >= 0);
}

void DirectoryModelTest::deleteDuringListingIsNotResurrected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 400; ++i) {
    QFile f(
        tmp.filePath(QStringLiteral("f_%1").arg(i, 3, 10, QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x", 1);
  }
  QFile victim(tmp.filePath(QStringLiteral("zzz_victim")));
  QVERIFY(victim.open(QIODevice::WriteOnly));
  victim.write("v", 1);
  victim.close();

  DirectoryModel model;
  model.setPath(tmp.path());
  DirectoryWatchEvent ev;
  ev.kind = DirectoryWatchEvent::Deleted;
  ev.name = QStringLiteral("zzz_victim");
  ev.serial = model.m_watchSerial;
  model.onWatchEvents({ev});
  QVERIFY(waitListingDone(model));
  QVERIFY(findRow(model, QStringLiteral("zzz_victim")) < 0);
  QVERIFY(findRow(model, QStringLiteral("f_000")) >= 0);
}

void DirectoryModelTest::samePathSetPathIsNoop() {
  DirectoryModel model;
  model.setPath(m_scratch.path());
  QVERIFY(waitListingDone(model));
  const quint64 before = static_cast<quint64>(model.rowCount());
  QSignalSpy pathSpy(&model, &DirectoryModel::pathChanged);
  model.setPath(m_scratch.path());
  QCOMPARE(pathSpy.count(), 0);
  QCOMPARE(model.rowCount(), static_cast<int>(before));
  QVERIFY(!model.listing());
}

void DirectoryModelTest::visibleThumbsFillPngAndFolderMosaic() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("album")));
  auto writeColor = [](const QString &path, QRgb color) {
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
  };
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("shot.png")), qRgb(200, 30, 30)));
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("album/a.png")),
                     qRgb(30, 200, 30)));
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("album/b.png")),
                     qRgb(30, 30, 200)));

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  model.requestVisibleThumbs(0, model.rowCount() - 1, 128);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const int shot = findRow(model, QStringLiteral("shot.png"));
        const int album = findRow(model, QStringLiteral("album"));
        if (shot < 0 || album < 0)
          return false;
        return !roleAt(model, shot, DirectoryModel::ThumbnailRole)
                    .toString()
                    .isEmpty() &&
               !roleAt(model, album, DirectoryModel::ThumbnailRole)
                    .toString()
                    .isEmpty();
      },
      4000));
}

void DirectoryModelTest::createdFileGetsThumbnail() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QImage seed(24, 24, QImage::Format_RGB32);
  seed.fill(qRgb(200, 40, 40));
  QVERIFY(seed.save(tmp.filePath(QStringLiteral("keep.png")), "PNG"));

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  model.requestVisibleThumbs(0, model.rowCount() - 1, 128);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const int row = findRow(model, QStringLiteral("keep.png"));
        return row >= 0 && !roleAt(model, row, DirectoryModel::ThumbnailRole)
                                .toString()
                                .isEmpty();
      },
      4000));

  QImage extra(24, 24, QImage::Format_RGB32);
  extra.fill(qRgb(40, 200, 40));
  QVERIFY(extra.save(tmp.filePath(QStringLiteral("new.png")), "PNG"));
  DirectoryWatchEvent ev;
  ev.kind = DirectoryWatchEvent::Created;
  ev.name = QStringLiteral("new.png");
  ev.isDir = false;
  ev.serial = model.m_watchSerial;
  model.onWatchEvents({ev});
  QVERIFY(findRow(model, QStringLiteral("new.png")) >= 0);
  model.refreshThumbs({tmp.filePath(QStringLiteral("new.png"))});

  QVERIFY(QTest::qWaitFor(
      [&] {
        const int row = findRow(model, QStringLiteral("new.png"));
        return row >= 0 && !roleAt(model, row, DirectoryModel::ThumbnailRole)
                                .toString()
                                .isEmpty();
      },
      4000));
}

void DirectoryModelTest::refreshThumbsUpdatesFolderMosaicOnly() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("album")));
  auto writeColor = [](const QString &path, QRgb color) {
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
  };
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("shot.png")), qRgb(200, 30, 30)));
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("album/a.png")),
                     qRgb(30, 200, 30)));

  DirectoryModel model;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  model.requestVisibleThumbs(0, model.rowCount() - 1, 128);
  QString shotUrl;
  QString albumUrl;
  QVERIFY(QTest::qWaitFor(
      [&] {
        const int shot = findRow(model, QStringLiteral("shot.png"));
        const int album = findRow(model, QStringLiteral("album"));
        if (shot < 0 || album < 0)
          return false;
        shotUrl = roleAt(model, shot, DirectoryModel::ThumbnailRole).toString();
        albumUrl =
            roleAt(model, album, DirectoryModel::ThumbnailRole).toString();
        return !shotUrl.isEmpty() && !albumUrl.isEmpty();
      },
      4000));

  QVERIFY(writeColor(tmp.filePath(QStringLiteral("album/b.png")),
                     qRgb(30, 30, 200)));
  model.refreshThumbs({tmp.filePath(QStringLiteral("album"))});
  QVERIFY(QTest::qWaitFor(
      [&] {
        const int album = findRow(model, QStringLiteral("album"));
        if (album < 0)
          return false;
        return roleAt(model, album, DirectoryModel::ThumbnailRole).toString() !=
               albumUrl;
      },
      4000));
  const int shot = findRow(model, QStringLiteral("shot.png"));
  QVERIFY(shot >= 0);
  QCOMPARE(roleAt(model, shot, DirectoryModel::ThumbnailRole).toString(),
           shotUrl);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  DirectoryModelTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "directory_model_test.moc"
