#include "ThumbnailService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool writePng(const QString &path, const QString &uri, const QString &mtime) {
  QImage img(8, 8, QImage::Format_RGB32);
  img.fill(0xff336699);
  if (!uri.isNull())
    img.setText(QStringLiteral("Thumb::URI"), uri);
  if (!mtime.isNull())
    img.setText(QStringLiteral("Thumb::MTime"), mtime);
  QDir().mkpath(QFileInfo(path).absolutePath());
  return img.save(path, "PNG");
}

bool writePlainPng(const QString &path) {
  QImage img(4, 4, QImage::Format_RGB32);
  img.fill(0xffff0000);
  QDir().mkpath(QFileInfo(path).absolutePath());
  return img.save(path, "PNG");
}

qint64 mtimeMsOf(const QString &path) {
  return QFileInfo(path).lastModified().toMSecsSinceEpoch();
}

} // namespace

class ThumbnailServiceTest : public QObject {
  Q_OBJECT

private slots:
  void init();
  void parseExecSubstitutesKnownCodes();
  void parseExecDropsUnknownPercent();
  void xdgUriIsGlibCompatible();
  void validXdgPngAccepted();
  void rawPngWithoutTextRejected();
  void xdgMtimeMismatchRejected();
  void requestUsesValidXdgCache();
  void requestUsesSynchroCache();
  void invalidXdgFallsThroughToGenerate();
  void generateWritesOnlySynchroCache();
  void tryExecMissingBinaryIsSkipped();

private:
  QTemporaryDir m_cache;
  QTemporaryDir m_files;
  QTemporaryDir m_thumbs;
};

void ThumbnailServiceTest::init() {
  QVERIFY(m_cache.isValid());
  QVERIFY(m_files.isValid());
  QVERIFY(m_thumbs.isValid());
  qputenv("XDG_CACHE_HOME", QFile::encodeName(m_cache.path()));
  const QDir thumbDir(m_thumbs.path());
  const QStringList leftover =
      thumbDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
  for (const QString &name : leftover)
    QFile::remove(thumbDir.filePath(name));
}

void ThumbnailServiceTest::parseExecSubstitutesKnownCodes() {
  const QStringList argv = ThumbnailService::parseExec(
      QStringLiteral("ffmpegthumbnailer -i %i -o %o -s %s -f"),
      QStringLiteral("/tmp/in.mp4"), QStringLiteral("/tmp/out.png"), 128);
  QCOMPARE(argv.size(), 8);
  QCOMPARE(argv.at(0), QStringLiteral("ffmpegthumbnailer"));
  QCOMPARE(argv.at(1), QStringLiteral("-i"));
  QCOMPARE(argv.at(2), QStringLiteral("/tmp/in.mp4"));
  QCOMPARE(argv.at(3), QStringLiteral("-o"));
  QCOMPARE(argv.at(4), QStringLiteral("/tmp/out.png"));
  QCOMPARE(argv.at(5), QStringLiteral("-s"));
  QCOMPARE(argv.at(6), QStringLiteral("128"));
  QCOMPARE(argv.at(7), QStringLiteral("-f"));

  const QStringList glycin = ThumbnailService::parseExec(
      QStringLiteral(
          "/usr/bin/glycin-thumbnailer --input %u --output %o --size %s"),
      QStringLiteral("/tmp/a.png"), QStringLiteral("/tmp/t.png"), 256);
  QCOMPARE(glycin.at(0), QStringLiteral("/usr/bin/glycin-thumbnailer"));
  QCOMPARE(glycin.at(2),
           ThumbnailService::canonicalFileUri(QStringLiteral("/tmp/a.png")));
}

void ThumbnailServiceTest::parseExecDropsUnknownPercent() {
  const QStringList argv = ThumbnailService::parseExec(
      QStringLiteral("xournalpp-thumbnailer %i %o %z"),
      QStringLiteral("/tmp/n.xopp"), QStringLiteral("/tmp/o.png"), 128);
  QCOMPARE(argv, QStringList({QStringLiteral("xournalpp-thumbnailer"),
                              QStringLiteral("/tmp/n.xopp"),
                              QStringLiteral("/tmp/o.png")}));
}

void ThumbnailServiceTest::xdgUriIsGlibCompatible() {
  const QString path = QStringLiteral(
      "/home/ryanr/Downloads/ChatGPT Image Nov 12, 2025, 06_35_48 AM.png");
  QCOMPARE(ThumbnailService::canonicalFileUri(path),
           QStringLiteral(
               "file:///home/ryanr/Downloads/"
               "ChatGPT%20Image%20Nov%2012,%202025,%2006_35_48%20AM.png"));
  QCOMPARE(
      ThumbnailService::canonicalFileUri(QStringLiteral("/tmp/foo;bar.png")),
      QStringLiteral("file:///tmp/foo%3Bbar.png"));
}

void ThumbnailServiceTest::validXdgPngAccepted() {
  const QString src = m_files.filePath(QStringLiteral("pic.png"));
  QVERIFY(writePlainPng(src));
  const QString uri = ThumbnailService::canonicalFileUri(src);
  const qint64 sec = mtimeMsOf(src) / 1000;
  const QString thumb = m_cache.filePath(QStringLiteral("t.png"));
  QVERIFY(writePng(thumb, uri, QString::number(sec)));
  QVERIFY(ThumbnailService::isValidXdgThumbnail(thumb, uri, sec));
}

void ThumbnailServiceTest::rawPngWithoutTextRejected() {
  const QString src = m_files.filePath(QStringLiteral("raw.png"));
  QVERIFY(writePlainPng(src));
  const QString uri = ThumbnailService::canonicalFileUri(src);
  const QString thumb = m_cache.filePath(QStringLiteral("raw-thumb.png"));
  QVERIFY(writePlainPng(thumb));
  QVERIFY(!ThumbnailService::isValidXdgThumbnail(thumb, uri,
                                                 mtimeMsOf(src) / 1000));
}

void ThumbnailServiceTest::xdgMtimeMismatchRejected() {
  const QString src = m_files.filePath(QStringLiteral("old.png"));
  QVERIFY(writePlainPng(src));
  const QString uri = ThumbnailService::canonicalFileUri(src);
  const QString thumb = m_cache.filePath(QStringLiteral("old-thumb.png"));
  QVERIFY(writePng(thumb, uri, QStringLiteral("1")));
  QVERIFY(!ThumbnailService::isValidXdgThumbnail(thumb, uri,
                                                 mtimeMsOf(src) / 1000));
}

void ThumbnailServiceTest::requestUsesValidXdgCache() {
  const QString src = m_files.filePath(QStringLiteral("hit.jpg"));
  QVERIFY(writePlainPng(src));
  const qint64 mtime = mtimeMsOf(src);
  const QString uri = ThumbnailService::canonicalFileUri(src);
  const QString xdg = ThumbnailService::xdgThumbPath(src, 128);
  QVERIFY(writePng(xdg, uri, QString::number(mtime / 1000)));

  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
  QCOMPARE(spy.at(0).at(0).toString(), src);
  QCOMPARE(spy.at(0).at(1).toString(), ThumbnailService::fileUrl(xdg));
  QVERIFY(
      !QFileInfo::exists(ThumbnailService::synchroThumbPath(src, mtime, 128)));
}

void ThumbnailServiceTest::requestUsesSynchroCache() {
  const QString src = m_files.filePath(QStringLiteral("warm.bin"));
  QVERIFY(writePlainPng(src));
  const qint64 mtime = mtimeMsOf(src);
  const QString syn = ThumbnailService::synchroThumbPath(src, mtime, 128);
  QVERIFY(writePlainPng(syn));

  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
  QCOMPARE(spy.at(0).at(1).toString(), ThumbnailService::fileUrl(syn));
}

void ThumbnailServiceTest::invalidXdgFallsThroughToGenerate() {
  const QString src = m_files.filePath(QStringLiteral("note.txt"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello", 5);
  }
  const qint64 mtime = mtimeMsOf(src);
  const QString xdg = ThumbnailService::xdgThumbPath(src, 128);
  QVERIFY(writePlainPng(xdg));

  const QString fixture = m_thumbs.filePath(QStringLiteral("fixture.png"));
  QVERIFY(writePlainPng(fixture));
  {
    QFile desk(m_thumbs.filePath(QStringLiteral("fake.thumbnailer")));
    QVERIFY(desk.open(QIODevice::WriteOnly | QIODevice::Text));
    desk.write("[Thumbnailer Entry]\n");
    desk.write("Exec=/usr/bin/cp ");
    desk.write(QFile::encodeName(fixture));
    desk.write(" %o\n");
    desk.write("MimeType=text/plain;\n");
  }

  ThumbnailService svc;
  svc.setThumbnailerDirectories({m_thumbs.path()});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 3000));
  const QString url = spy.at(0).at(1).toString();
  QVERIFY(!url.isEmpty());
  const QString syn = ThumbnailService::synchroThumbPath(src, mtime, 128);
  QCOMPARE(url, ThumbnailService::fileUrl(syn));
  QVERIFY(QFileInfo::exists(syn));
}

void ThumbnailServiceTest::generateWritesOnlySynchroCache() {
  const QString src = m_files.filePath(QStringLiteral("doc.txt"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x", 1);
  }
  const qint64 mtime = mtimeMsOf(src);
  const QString fixture = m_thumbs.filePath(QStringLiteral("out.png"));
  QVERIFY(writePlainPng(fixture));
  {
    QFile desk(m_thumbs.filePath(QStringLiteral("plain.thumbnailer")));
    QVERIFY(desk.open(QIODevice::WriteOnly | QIODevice::Text));
    desk.write("[Thumbnailer Entry]\n");
    desk.write("Exec=/usr/bin/cp ");
    desk.write(QFile::encodeName(fixture));
    desk.write(" %o\n");
    desk.write("MimeType=text/plain;\n");
  }

  const QString xdgDir = m_cache.filePath(QStringLiteral("thumbnails/normal"));
  QVERIFY(QDir().mkpath(xdgDir));
  const QStringList before = QDir(xdgDir).entryList(QDir::Files);

  ThumbnailService svc;
  svc.setThumbnailerDirectories({m_thumbs.path()});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 3000));
  QVERIFY(
      QFileInfo::exists(ThumbnailService::synchroThumbPath(src, mtime, 128)));
  QCOMPARE(QDir(xdgDir).entryList(QDir::Files), before);
}

void ThumbnailServiceTest::tryExecMissingBinaryIsSkipped() {
  const QString src = m_files.filePath(QStringLiteral("skip.txt"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("z", 1);
  }
  const qint64 mtime = mtimeMsOf(src);
  {
    QFile desk(m_thumbs.filePath(QStringLiteral("missing.thumbnailer")));
    QVERIFY(desk.open(QIODevice::WriteOnly | QIODevice::Text));
    desk.write("[Thumbnailer Entry]\n");
    desk.write("TryExec=synchro-not-a-real-thumbnailer\n");
    desk.write("Exec=/usr/bin/true %o\n");
    desk.write("MimeType=text/plain;\n");
  }

  ThumbnailService svc;
  svc.setThumbnailerDirectories({m_thumbs.path()});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
  QCOMPARE(spy.at(0).at(1).toString(), QString());
  QVERIFY(
      !QFileInfo::exists(ThumbnailService::synchroThumbPath(src, mtime, 128)));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ThumbnailServiceTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "thumbnail_service_test.moc"
