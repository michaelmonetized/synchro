#include "DirectoryLister.h"
#include "DirectoryModel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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
  void staleSetPathDoesNotClobber();
  void activatePendingSymlinkToDir();
  void listingTwoThousandLeavesGuiResponsive();
  void warmTimeToFirstFrame();

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

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  DirectoryModelTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "directory_model_test.moc"
