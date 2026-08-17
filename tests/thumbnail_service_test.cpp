#include "ThumbCache.h"
#include "ThumbTheme.h"
#include "ThumbnailService.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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

// 2x2 VP8 WebP — Qt on this box has no libqwebp, so Image/QImage cannot read it.
bool writeTinyWebp(const QString &path) {
  static const unsigned char kWebp[] = {
      0x52, 0x49, 0x46, 0x46, 0x40, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
      0x56, 0x50, 0x38, 0x20, 0x34, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00, 0x9d,
      0x01, 0x2a, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x34, 0x25, 0x98, 0x02,
      0x74, 0x01, 0x0e, 0xfe, 0x03, 0xc8, 0x00, 0x00, 0xfe, 0xe2, 0x9f, 0x61,
      0x0f, 0x3c, 0xf5, 0xbf, 0xc8, 0x2e, 0x6e, 0xa4, 0xa1, 0x1b, 0x3d, 0x0c,
      0x01, 0x32, 0x3f, 0xf0, 0x1f, 0xc4, 0xbf, 0xb0, 0x22, 0xb2, 0xb0, 0x00};
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write(reinterpret_cast<const char *>(kWebp), sizeof(kWebp)) ==
         qint64(sizeof(kWebp));
}

bool writeColorPng(const QString &path, QRgb color) {
  QImage img(16, 16, QImage::Format_RGB32);
  img.fill(color);
  QDir().mkpath(QFileInfo(path).absolutePath());
  return img.save(path, "PNG");
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
  void textCardGeneratedWhenNoThumbnailer();
  void webpDecodesWithoutQtPlugin();
  void folderMosaicFromChildImages();
  void folderMosaicFindsImagesPastEarlyFiles();
  void folderMosaicFollowsImageSymlink();
  void folderMosaicMarkdownIsReadable();
  void folderMosaicFromChildFolders();
  void folderMosaicNestedChildImage();
  void emptyFolderGetsFolderCard();
  void unknownFileGetsFallbackCard();
  void packedCacheRoundTrip();

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
  qputenv("SYNCHRO_HOME", QFile::encodeName(m_cache.filePath("synchro-home")));
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
  QCOMPARE(spy.at(0).at(1).toString(),
           ThumbnailService::packedUrl(src, mtime, 128));
  QVERIFY(ThumbCache::instance().contains(src, mtime, 128));
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
  QCOMPARE(spy.at(0).at(1).toString(),
           ThumbnailService::packedUrl(src, mtime, 128));
  QVERIFY(ThumbCache::instance().contains(src, mtime, 128));
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
  QCOMPARE(url, ThumbnailService::packedUrl(src, mtime, 128));
  const QString noteKey =
      src + QStringLiteral("#card-v1-") + ThumbTheme::current().cacheId();
  QVERIFY(ThumbCache::instance().contains(noteKey, mtime, 128));
  QVERIFY(!QFileInfo::exists(
      ThumbnailService::synchroThumbPath(src, mtime, 128)));
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
  QCOMPARE(spy.at(0).at(1).toString(),
           ThumbnailService::packedUrl(src, mtime, 128));
  const QString docKey =
      src + QStringLiteral("#card-v1-") + ThumbTheme::current().cacheId();
  QVERIFY(ThumbCache::instance().contains(docKey, mtime, 128));
  QVERIFY(!QFileInfo::exists(
      ThumbnailService::synchroThumbPath(src, mtime, 128)));
  QCOMPARE(QDir(xdgDir).entryList(QDir::Files), before);
}

void ThumbnailServiceTest::tryExecMissingBinaryIsSkipped() {
  const QString src = m_files.filePath(QStringLiteral("skip.bin"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(16, '\0'));
  }
  const qint64 mtime = mtimeMsOf(src);
  {
    QFile desk(m_thumbs.filePath(QStringLiteral("missing.thumbnailer")));
    QVERIFY(desk.open(QIODevice::WriteOnly | QIODevice::Text));
    desk.write("[Thumbnailer Entry]\n");
    desk.write("TryExec=synchro-not-a-real-thumbnailer\n");
    desk.write("Exec=/usr/bin/true %o\n");
    desk.write("MimeType=application/octet-stream;\n");
  }

  ThumbnailService svc;
  svc.setThumbnailerDirectories({m_thumbs.path()});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
  const QString url = spy.at(0).at(1).toString();
  QVERIFY2(!url.isEmpty(), "unknown files get a fallback card, not an empty box");
  QCOMPARE(url, ThumbnailService::packedUrl(src, mtime, 128));
}

void ThumbnailServiceTest::textCardGeneratedWhenNoThumbnailer() {
  const QString src = m_files.filePath(QStringLiteral("notes.md"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# hello\nworld\n");
  }
  const qint64 mtime = mtimeMsOf(src);
  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
  const QString url = spy.at(0).at(1).toString();
  QVERIFY(!url.isEmpty());
  QCOMPARE(url, ThumbnailService::packedUrl(src, mtime, 128));
  const QString packedKey =
      src + QStringLiteral("#card-v1-") + ThumbTheme::current().cacheId();
  QVERIFY(ThumbCache::instance().contains(packedKey, mtime, 128));
  QVERIFY(!ThumbCache::instance().getImage(packedKey, mtime, 128).isNull());
}

void ThumbnailServiceTest::webpDecodesWithoutQtPlugin() {
  const QString src = m_files.filePath(QStringLiteral("tile.webp"));
  QVERIFY(writeTinyWebp(src));
  QVERIFY(QImage(src).isNull());

  const QImage decoded = ThumbnailService::decodeRaster(src, 128);
  if (decoded.isNull())
    QSKIP("neither libwebp nor ffmpeg could decode the fixture WebP");
  QVERIFY(decoded.width() >= 2);
  QVERIFY(decoded.height() >= 2);

  const qint64 mtime = mtimeMsOf(src);
  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 4000));
  const QString url = spy.at(0).at(1).toString();
  QVERIFY2(!url.isEmpty(), "WebP must produce a PNG thumb without Qt's plugin");
  QCOMPARE(url, ThumbnailService::packedUrl(src, mtime, 128));
  QVERIFY(!ThumbCache::instance().getImage(src, mtime, 128).isNull());
}

void ThumbnailServiceTest::folderMosaicFromChildImages() {
  const QString album = m_files.filePath(QStringLiteral("album"));
  QVERIFY(QDir().mkpath(album));
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("a.png")),
                        qRgb(220, 40, 40)));
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("b.png")),
                        qRgb(40, 200, 40)));
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("c.png")),
                        qRgb(40, 40, 220)));
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("d.png")),
                        qRgb(220, 200, 40)));

  const QString dest = m_cache.filePath(QStringLiteral("mosaic.png"));
  QVERIFY(ThumbnailService::renderFolderMosaic(album, dest, 128));
  QImage mosaic(dest);
  QVERIFY(!mosaic.isNull());
  QCOMPARE(mosaic.width(), 128);
  QCOMPARE(mosaic.height(), 128);

  const int samples[][2] = {{20, 20}, {100, 20}, {20, 100}, {100, 100}};
  int painted = 0;
  for (const auto &xy : samples) {
    if (qAlpha(mosaic.pixel(xy[0], xy[1])) > 0)
      ++painted;
  }
  QVERIFY2(painted >= 3, "2x2 mosaic should paint child tiles, not a blank icon");

  const qint64 mtime = mtimeMsOf(album);
  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(album, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 4000));
  QVERIFY(!spy.at(0).at(1).toString().isEmpty());
}

void ThumbnailServiceTest::folderMosaicFindsImagesPastEarlyFiles() {
  const QString album = m_files.filePath(QStringLiteral("mixed"));
  QVERIFY(QDir().mkpath(album));
  for (int i = 0; i < 60; ++i) {
    QFile f(QDir(album).filePath(QStringLiteral("z%1.txt").arg(i, 2, 10,
                                                              QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("note\n");
  }
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("shot-a.png")),
                        qRgb(200, 20, 20)));
  QVERIFY(writeColorPng(QDir(album).filePath(QStringLiteral("shot-b.png")),
                        qRgb(20, 200, 20)));

  const QString dest = m_cache.filePath(QStringLiteral("mixed-mosaic.png"));
  QVERIFY(ThumbnailService::renderFolderMosaic(album, dest, 128));
  QImage mosaic(dest);
  QVERIFY(!mosaic.isNull());
  int painted = 0;
  const int samples[][2] = {{20, 20}, {100, 20}, {20, 100}, {100, 100}};
  for (const auto &xy : samples) {
    if (qAlpha(mosaic.pixel(xy[0], xy[1])) > 0)
      ++painted;
  }
  QVERIFY(painted >= 1);
}

void ThumbnailServiceTest::folderMosaicMarkdownIsReadable() {
  const QString docs = m_files.filePath(QStringLiteral("md-only"));
  QVERIFY(QDir().mkpath(docs));
  for (const char *name : {"ALPHA.md", "BETA.md", "GAMMA.md", "DELTA.md"}) {
    QFile f(QDir(docs).filePath(QLatin1String(name)));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("# heading\n\nSome markdown body for the card.\n");
  }
  const QString dest = m_cache.filePath(QStringLiteral("md-mosaic.png"));
  QVERIFY(ThumbnailService::renderFolderMosaic(docs, dest, 128));
  const QImage mosaic(dest);
  QVERIFY(!mosaic.isNull());
  const QColor paper(0xe8, 0xe4, 0xdc);
  const QColor themeBg = ThumbTheme::current().background;
  int themed = 0;
  const int samples[][2] = {{20, 20}, {100, 20}, {20, 100}, {100, 100}};
  for (const auto &xy : samples) {
    const QColor c = mosaic.pixelColor(xy[0], xy[1]);
    const int toTheme = qAbs(c.lightness() - themeBg.lightness());
    const int toPaper = qAbs(c.lightness() - paper.lightness());
    if (toTheme < toPaper)
      ++themed;
  }
  QVERIFY2(themed >= 3,
           "markdown mosaic tiles should follow Omarchy colors, not light paper");
}

void ThumbnailServiceTest::folderMosaicFollowsImageSymlink() {
  const QString album = m_files.filePath(QStringLiteral("linked"));
  QVERIFY(QDir().mkpath(album));
  const QString real = m_files.filePath(QStringLiteral("real-shot.png"));
  QVERIFY(writeColorPng(real, qRgb(30, 30, 200)));
  QVERIFY(QFile::link(real, QDir(album).filePath(QStringLiteral("alias.png"))));

  const QString dest = m_cache.filePath(QStringLiteral("link-mosaic.png"));
  QVERIFY(ThumbnailService::renderFolderMosaic(album, dest, 128));
  QVERIFY(!QImage(dest).isNull());
}

void ThumbnailServiceTest::folderMosaicFromChildFolders() {
  const QString root = m_files.filePath(QStringLiteral("only-dirs"));
  QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("alpha"))));
  QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("beta"))));
  QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("gamma"))));
  QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("delta"))));

  const QString dest = m_cache.filePath(QStringLiteral("dir-mosaic.png"));
  QVERIFY2(ThumbnailService::renderFolderMosaic(root, dest, 128),
           "a folder of folders must still produce a mosaic");
  const QImage mosaic(dest);
  QVERIFY(!mosaic.isNull());
  int painted = 0;
  int themed = 0;
  const QColor oldBlue(0xd2, 0xda, 0xe4);
  const QColor themeBg = ThumbTheme::current().background;
  const int samples[][2] = {{20, 20}, {100, 20}, {20, 100}, {100, 100}};
  for (const auto &xy : samples) {
    const QColor c = mosaic.pixelColor(xy[0], xy[1]);
    if (c.alpha() > 0)
      ++painted;
    if (qAbs(c.lightness() - themeBg.lightness()) <
        qAbs(c.lightness() - oldBlue.lightness()))
      ++themed;
  }
  QVERIFY2(painted >= 4, "all four folder tiles should be painted");
  QVERIFY2(themed >= 3, "folder cards should use Omarchy colors, not light blue");
}

void ThumbnailServiceTest::folderMosaicNestedChildImage() {
  const QString root = m_files.filePath(QStringLiteral("parent-of-photos"));
  const QString nested = QDir(root).filePath(QStringLiteral("photos"));
  QVERIFY(QDir().mkpath(nested));
  QVERIFY(writeColorPng(QDir(nested).filePath(QStringLiteral("shot.png")),
                        qRgb(220, 30, 30)));

  const QString dest = m_cache.filePath(QStringLiteral("nested-mosaic.png"));
  QVERIFY(ThumbnailService::renderFolderMosaic(root, dest, 128));
  const QImage mosaic(dest);
  QVERIFY(!mosaic.isNull());
  const QColor c = mosaic.pixelColor(28, 28);
  QVERIFY2(c.red() > 150 && c.red() > c.blue(),
           "child-folder tile should show a nested image, not a blank card");
}

void ThumbnailServiceTest::emptyFolderGetsFolderCard() {
  const QString empty = m_files.filePath(QStringLiteral("empty-dir"));
  QVERIFY(QDir().mkpath(empty));
  const QString dest = m_cache.filePath(QStringLiteral("empty-card.png"));
  QVERIFY2(ThumbnailService::renderFolderMosaic(empty, dest, 128),
           "empty folders should still get the mosaic folder card");
  const QImage card(dest);
  QVERIFY(!card.isNull());
  QCOMPARE(card.width(), 128);
  const QColor oldBlue(0xd2, 0xda, 0xe4);
  const QColor themeBg = ThumbTheme::current().background;
  int themed = 0;
  const int samples[][2] = {{20, 40}, {64, 64}, {100, 90}};
  for (const auto &xy : samples) {
    const QColor c = card.pixelColor(xy[0], xy[1]);
    if (qAbs(c.lightness() - themeBg.lightness()) <=
        qAbs(c.lightness() - oldBlue.lightness()))
      ++themed;
  }
  QVERIFY2(themed >= 2, "folder card should use Omarchy colors, not light blue");

  const qint64 mtime = mtimeMsOf(empty);
  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(empty, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 4000));
  QVERIFY(!spy.at(0).at(1).toString().isEmpty());
}

void ThumbnailServiceTest::unknownFileGetsFallbackCard() {
  const QString src = m_files.filePath(QStringLiteral("mystery.7z"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("7z\xbc\xaf not-a-real-archive", 24);
  }
  const qint64 mtime = mtimeMsOf(src);
  ThumbnailService svc;
  svc.setThumbnailerDirectories({});
  QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
  svc.request(src, mtime, 128);
  QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 4000));
  const QString url = spy.at(0).at(1).toString();
  QVERIFY2(!url.isEmpty(), "unread types should still get a file glyph");
  QCOMPARE(url, ThumbnailService::packedUrl(src, mtime, 128));
  const QString key =
      src + QStringLiteral("#card-v1-") + ThumbTheme::current().cacheId();
  const QImage card = ThumbCache::instance().getImage(key, mtime, 128);
  QVERIFY(!card.isNull());
  QVERIFY(card.width() >= 64);
}

void ThumbnailServiceTest::packedCacheRoundTrip() {
  const QString src = m_files.filePath(QStringLiteral("persist.md"));
  {
    QFile f(src);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("cached\n");
  }
  const qint64 mtime = mtimeMsOf(src);
  {
    ThumbnailService svc;
    svc.setThumbnailerDirectories({});
    QSignalSpy spy(&svc, &ThumbnailService::thumbnailReady);
    svc.request(src, mtime, 128);
    QVERIFY(QTest::qWaitFor([&] { return spy.count() >= 1; }, 2000));
    QCOMPARE(spy.at(0).at(1).toString(),
             ThumbnailService::packedUrl(src, mtime, 128));
  }
  QVERIFY(QFileInfo::exists(ThumbCache::dbPath()));
  const QString packedKey =
      src + QStringLiteral("#card-v1-") + ThumbTheme::current().cacheId();
  QVERIFY(ThumbCache::instance().contains(packedKey, mtime, 128));
  QVERIFY(!QFileInfo::exists(
      ThumbnailService::synchroThumbPath(src, mtime, 128)));

  ThumbnailService again;
  again.setThumbnailerDirectories({});
  QSignalSpy spy(&again, &ThumbnailService::thumbnailReady);
  again.request(src, mtime, 128);
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(1).toString(),
           ThumbnailService::packedUrl(src, mtime, 128));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  ThumbnailServiceTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "thumbnail_service_test.moc"
