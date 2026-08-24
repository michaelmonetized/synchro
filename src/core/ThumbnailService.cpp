#include "ThumbnailService.h"
#include "ArchiveMeta.h"
#include "MimeMap.h"
#include "ParquetMeta.h"
#include "ThumbCache.h"
#include "ThumbTheme.h"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QPainter>
#include <QPolygon>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#ifdef SYNCHRO_HAVE_WEBP
#include <webp/decode.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <limits>
#include <utility>

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10
#endif

namespace {

constexpr int kMaxWorkers = 2;
constexpr int kTimeoutMs = 8000;

const struct {
  const char *dir;
  int px;
} kXdgSizes[] = {
    {"normal", 128},
    {"large", 256},
    {"x-large", 512},
    {"xx-large", 1024},
};

bool debugOn() {
  static const bool on = qEnvironmentVariableIntValue("SYNCHRO_DEBUG") != 0 ||
                         qEnvironmentVariableIsSet("SYNCHRO_DEBUG");
  return on;
}

void debugLog(const char *fmt, ...) {
  if (!debugOn())
    return;
  std::fputs("synchro: ", stderr);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

// g_filename_to_uri leaves path extras unescaped except ';', which is %3B.
bool uriPathAllowed(unsigned char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9'))
    return true;
  switch (c) {
  case '-':
  case '.':
  case '_':
  case '~':
  case '!':
  case '$':
  case '&':
  case '\'':
  case '(':
  case ')':
  case '*':
  case '+':
  case ',':
  case '=':
  case ':':
  case '@':
  case '/':
    return true;
  default:
    return false;
  }
}

// Qt (and some thumbnailers) store longer Thumb::* strings as zlib zTXt.
QByteArray inflateZlib(const QByteArray &src) {
  if (src.isEmpty())
    return {};
  const quint32 guesses[] = {quint32(src.size() * 16 + 64), 1u << 16, 1u << 20};
  for (quint32 guess : guesses) {
    QByteArray wrapped(4, Qt::Uninitialized);
    qToBigEndian(guess, reinterpret_cast<uchar *>(wrapped.data()));
    wrapped.append(src);
    const QByteArray out = qUncompress(wrapped);
    if (!out.isEmpty())
      return out;
  }
  return {};
}

QString jobKey(const QString &path, qint64 mtime, int sizePx) {
  return path + QLatin1Char('\n') + QString::number(mtime) + QLatin1Char('\n') +
         QString::number(sizePx);
}

bool isThumbCachePath(const QString &path) {
  const QString cache = ThumbnailService::xdgCacheHome();
  const QString home = ThumbCache::homeDir();
  return path.startsWith(cache + QStringLiteral("/thumbnails/")) ||
         path.startsWith(cache + QStringLiteral("/synchro/thumbs/")) ||
         path.startsWith(home + QLatin1Char('/'));
}

QByteArray pngBytes(const QImage &img) {
  if (img.isNull())
    return {};
  QByteArray out;
  QBuffer buf(&out);
  if (!buf.open(QIODevice::WriteOnly))
    return {};
  if (!img.save(&buf, "PNG"))
    return {};
  return out;
}

QString cachePathFor(const QString &path);

struct Thumbnailer {
  QString fileName;
  QString tryExec;
  QString exec;
  QStringList mimes;
};

bool tryExecOk(const QString &tryExec) {
  if (tryExec.isEmpty())
    return true;
  if (tryExec.contains(QLatin1Char('/')))
    return QFileInfo(tryExec).isExecutable();
  return !QStandardPaths::findExecutable(tryExec).isEmpty();
}

QString resolveProgram(const QString &cmd) {
  if (cmd.contains(QLatin1Char('/')))
    return cmd;
  return QStandardPaths::findExecutable(cmd);
}

QVector<Thumbnailer> loadThumbnailers(const QStringList &dirs) {
  QVector<Thumbnailer> out;
  for (const QString &dirPath : dirs) {
    const QDir dir(dirPath);
    const QStringList files =
        dir.entryList(QStringList() << QStringLiteral("*.thumbnailer"),
                      QDir::Files, QDir::Name);
    for (const QString &name : files) {
      QFile f(dir.filePath(name));
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        continue;
      Thumbnailer t;
      t.fileName = name;
      while (!f.atEnd()) {
        const QByteArray raw = f.readLine();
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) ||
            line.startsWith(QLatin1Char('[')))
          continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
          continue;
        const QString key = line.left(eq);
        const QString value = line.mid(eq + 1);
        if (key == QLatin1String("TryExec"))
          t.tryExec = value;
        else if (key == QLatin1String("Exec"))
          t.exec = value;
        else if (key == QLatin1String("MimeType")) {
          const QStringList parts =
              value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
          for (QString m : parts)
            t.mimes.append(m.trimmed());
        }
      }
      if (t.exec.isEmpty() || !tryExecOk(t.tryExec))
        continue;
      out.append(std::move(t));
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Thumbnailer &a, const Thumbnailer &b) {
              const bool ga = a.fileName.contains(QLatin1String("glycin"));
              const bool gb = b.fileName.contains(QLatin1String("glycin"));
              if (ga != gb)
                return ga;
              return a.fileName < b.fileName;
            });
  return out;
}

bool looksLikeText(const QString &path, const QString &mimeHint) {
  return MimeMap::isProbablyText(path, mimeHint);
}

bool looksLikeParquet(const QString &path, const QString &mimeHint) {
  return ParquetMeta::looksLike(path, mimeHint);
}

bool looksLikeArchive(const QString &path, const QString &mimeHint) {
  return ArchiveMeta::looksLike(path, mimeHint);
}

bool renderParquetCard(const QString &src, const QString &dest, int sizePx) {
  const ParquetInfo info = ParquetMeta::parse(src);
  const int px = qBound(64, sizePx, 512);
  QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
  const ThumbTheme th = ThumbTheme::current();
  img.fill(th.background);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing);
  QFont head(th.fontFamily, qMax(8, px / 14));
  head.setBold(true);
  p.setFont(head);
  p.setPen(th.foreground);
  const int rail = qMax(3, px / 28);
  p.fillRect(0, 0, rail, px, th.accent);
  const QRect headRect(rail + 6, 3, px - rail - 11, qMax(16, px / 5));
  p.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
             QStringLiteral("parquet"));
  if (px >= 72) {
    QFont body(th.fontFamily, qMax(6, px / 22));
    p.setFont(body);
    p.setPen(th.muted);
    QString text = QFileInfo(src).fileName();
    if (info.ok) {
      text += QLatin1Char('\n');
      text += QString::number(info.columns.size()) + QStringLiteral(" cols");
      text += QLatin1Char('\n');
      text += QString::number(info.numRows) + QStringLiteral(" rows");
      if (!info.codec.isEmpty())
        text += QLatin1Char('\n') + info.codec;
    }
    p.drawText(QRect(5, headRect.bottom() + 3, px - 10,
                     px - headRect.bottom() - 8),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
  }
  p.end();
  QDir().mkpath(QFileInfo(dest).absolutePath());
  return img.save(dest, "PNG");
}

bool renderArchiveCard(const QString &src, const QString &dest, int sizePx) {
  const ArchiveInfo info = ArchiveMeta::inspect(src, 8);
  const int px = qBound(64, sizePx, 512);
  QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
  const ThumbTheme th = ThumbTheme::current();
  img.fill(th.background);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing);
  QFont head(th.fontFamily, qMax(8, px / 14));
  head.setBold(true);
  p.setFont(head);
  p.setPen(th.foreground);
  const int rail = qMax(3, px / 28);
  p.fillRect(0, 0, rail, px, th.accent);
  const QRect headRect(rail + 6, 3, px - rail - 11, qMax(16, px / 5));
  const QString kind = info.format.isEmpty() ? QStringLiteral("archive")
                                             : info.format;
  p.drawText(headRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
             kind);
  if (px >= 72) {
    QFont body(th.fontFamily, qMax(6, px / 22));
    p.setFont(body);
    p.setPen(th.muted);
    QString text = QFileInfo(src).fileName();
    if (info.ok) {
      text += QLatin1Char('\n');
      if (info.files)
        text += QString::number(info.files) + QStringLiteral(" files");
      if (info.dirs)
        text += QLatin1Char('\n') + QString::number(info.dirs) +
                QStringLiteral(" dirs");
      if (info.uncompressed > 0)
        text += QLatin1Char('\n') + QString::number(info.uncompressed) +
                QStringLiteral(" B");
    }
    p.drawText(QRect(5, headRect.bottom() + 3, px - 10,
                     px - headRect.bottom() - 8),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
  }
  p.end();
  QDir().mkpath(QFileInfo(dest).absolutePath());
  return img.save(dest, "PNG");
}

bool renderTextCard(const QString &src, const QString &dest, int sizePx);

enum class SniffKind { None, Image, WebP, Video };

SniffKind sniffKind(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return SniffKind::None;
  const QByteArray h = f.read(16);
  if (h.size() >= 3 && uchar(h.at(0)) == 0xff && uchar(h.at(1)) == 0xd8 &&
      uchar(h.at(2)) == 0xff)
    return SniffKind::Image;
  static const char kTiffLe[] = {'I', 'I', '*', '\0'};
  static const char kTiffBe[] = {'M', 'M', '\0', '*'};
  if (h.startsWith("\x89PNG") || h.startsWith("GIF8") || h.startsWith("BM") ||
      h.startsWith(QByteArray::fromRawData(kTiffLe, 4)) ||
      h.startsWith(QByteArray::fromRawData(kTiffBe, 4)) ||
      h.startsWith("qoif"))
    return SniffKind::Image;
  if (h.size() >= 12 && h.startsWith("RIFF") && h.mid(8, 4) == "WEBP")
    return SniffKind::WebP;
  if (h.size() >= 12 && h.mid(4, 4) == "ftyp") {
    const QByteArray brand = h.mid(8, 4);
    if (brand.startsWith("heic") || brand.startsWith("heif") ||
        brand.startsWith("mif1") || brand.startsWith("avif") ||
        brand.startsWith("msf1"))
      return SniffKind::Image;
    return SniffKind::Video;
  }
  return SniffKind::None;
}

bool looksLikeImage(const QString &path, const QString &mimeHint) {
  const QString mime = mimeHint.toLower();
  if (mime.startsWith(QLatin1String("image/")))
    return true;
  static const char *const kSuf[] = {
      ".jpg", ".jpeg", ".jpe",  ".jfif", ".png",  ".gif",  ".webp",
      ".bmp", ".tif",  ".tiff", ".svg",  ".jxl",  ".heic", ".heif",
      ".avif", ".ico", ".qoi",  ".pbm",  ".pgm",  ".ppm",  ".pnm"};
  for (const char *s : kSuf) {
    if (path.endsWith(QLatin1String(s), Qt::CaseInsensitive))
      return true;
  }
  const SniffKind sniff = sniffKind(path);
  return sniff == SniffKind::Image || sniff == SniffKind::WebP;
}

bool looksLikeVideo(const QString &path, const QString &mimeHint) {
  const QString mime = mimeHint.toLower();
  if (mime.startsWith(QLatin1String("video/")))
    return true;
  static const char *const kSuf[] = {".mp4",  ".m4v", ".mkv", ".mov",
                                     ".webm", ".avi", ".mpeg", ".mpg",
                                     ".ogv",  ".wmv", ".3gp"};
  for (const char *s : kSuf) {
    if (path.endsWith(QLatin1String(s), Qt::CaseInsensitive))
      return true;
  }
  return false;
}

QString cachePathFor(const QString &path) {
  const QString theme = ThumbTheme::current().cacheId();
  if (QFileInfo(path).isDir())
    return path + QLatin1String("#mosaic-v8-") + theme;
  if (looksLikeImage(path, QString()) || looksLikeVideo(path, QString()))
    return path;
  return path + QLatin1String("#card-v5-") + theme;
}

bool looksLikeWebP(const QString &path, const QString &mimeHint) {
  const QString mime = mimeHint.toLower();
  if (mime == QLatin1String("image/webp") ||
      mime == QLatin1String("image/x-webp"))
    return true;
  return path.endsWith(QLatin1String(".webp"), Qt::CaseInsensitive);
}

QImage scaleToFit(const QImage &src, int maxEdge) {
  if (src.isNull() || maxEdge <= 0)
    return src;
  if (src.width() <= maxEdge && src.height() <= maxEdge)
    return src;
  return src.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                    Qt::SmoothTransformation);
}

QImage cropToSquare(const QImage &src, int edge) {
  if (src.isNull() || edge <= 0)
    return {};
  QImage s = src;
  if (s.width() != edge || s.height() != edge)
    s = s.scaled(edge, edge, Qt::KeepAspectRatioByExpanding,
                 Qt::SmoothTransformation);
  if (s.width() == edge && s.height() == edge)
    return s;
  const int x = qMax(0, (s.width() - edge) / 2);
  const int y = qMax(0, (s.height() - edge) / 2);
  return s.copy(x, y, edge, edge);
}

QImage cropToAspect(const QImage &src, const QSize &size) {
  if (src.isNull() || size.width() <= 0 || size.height() <= 0)
    return {};
  QImage scaled = src.scaled(size, Qt::KeepAspectRatioByExpanding,
                             Qt::SmoothTransformation);
  const int x = qMax(0, (scaled.width() - size.width()) / 2);
  const int y = qMax(0, (scaled.height() - size.height()) / 2);
  return scaled.copy(x, y, size.width(), size.height());
}

#ifdef SYNCHRO_HAVE_WEBP
QImage decodeWebPBytes(const QByteArray &bytes, int maxEdge) {
  if (bytes.isEmpty())
    return {};
  const auto *data = reinterpret_cast<const uint8_t *>(bytes.constData());
  WebPDecoderConfig cfg;
  if (!WebPInitDecoderConfig(&cfg))
    return {};
  if (WebPGetFeatures(data, size_t(bytes.size()), &cfg.input) != VP8_STATUS_OK)
    return {};
  const int w = cfg.input.width;
  const int h = cfg.input.height;
  if (w <= 0 || h <= 0)
    return {};
  if (maxEdge > 0 && (w > maxEdge || h > maxEdge)) {
    const double s = double(maxEdge) / double(qMax(w, h));
    cfg.options.use_scaling = 1;
    cfg.options.scaled_width = qMax(1, int(w * s));
    cfg.options.scaled_height = qMax(1, int(h * s));
  }
  cfg.output.colorspace = MODE_RGBA;
  if (WebPDecode(data, size_t(bytes.size()), &cfg) != VP8_STATUS_OK)
    return {};
  QImage img(cfg.output.u.RGBA.rgba, cfg.output.width, cfg.output.height,
             cfg.output.u.RGBA.stride, QImage::Format_RGBA8888);
  const QImage copy = img.copy();
  WebPFreeDecBuffer(&cfg.output);
  return copy;
}
#endif

QImage decodeViaFfmpeg(const QString &path, int maxEdge) {
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty())
    return {};
  QDir().mkpath(ThumbnailService::synchroThumbsDir());
  const QString tmp =
      ThumbnailService::synchroThumbsDir() + QLatin1Char('/') +
      QStringLiteral("ff-") +
      QString::number(qHash(path), 16) + QStringLiteral(".png");
  QFile::remove(tmp);
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  proc.setStandardOutputFile(QProcess::nullDevice());
  proc.setStandardErrorFile(QProcess::nullDevice());
  proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-y"),
                      QStringLiteral("-i"), path, QStringLiteral("-frames:v"),
                      QStringLiteral("1"), tmp});
  if (!proc.waitForFinished(kTimeoutMs) ||
      proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (proc.state() != QProcess::NotRunning)
      proc.kill();
    QFile::remove(tmp);
    return {};
  }
  const QImage img(tmp);
  QFile::remove(tmp);
  return scaleToFit(img, maxEdge);
}

QImage decodeChildCached(const QString &path, qint64 mtimeMs, int tilePx) {
  QImage packed = ThumbCache::instance().getImage(path, mtimeMs, tilePx);
  if (!packed.isNull())
    return packed;
  const QString uri = ThumbnailService::canonicalFileUri(path);
  const qint64 mtimeSec = mtimeMs / 1000;
  const int sizes[] = {tilePx, 128, 256, 512};
  for (int px : sizes) {
    const QString xdg = ThumbnailService::xdgThumbPath(path, px);
    if (ThumbnailService::isValidXdgThumbnail(xdg, uri, mtimeSec)) {
      QImage img(xdg);
      if (!img.isNull()) {
        ThumbCache::instance().ingestFile(path, mtimeMs, tilePx, xdg);
        return img;
      }
    }
  }
  const QString syn =
      ThumbnailService::synchroThumbPath(path, mtimeMs, tilePx);
  if (QFileInfo::exists(syn)) {
    QImage img(syn);
    if (!img.isNull()) {
      ThumbCache::instance().ingestFile(path, mtimeMs, tilePx, syn);
      return img;
    }
  }
  return {};
}

QImage decodeVideoFrame(const QString &path, int tilePx, const QString &scratch) {
  const QString bin =
      QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer"));
  if (bin.isEmpty())
    return decodeViaFfmpeg(path, tilePx);
  QFile::remove(scratch);
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  proc.setStandardOutputFile(QProcess::nullDevice());
  proc.setStandardErrorFile(QProcess::nullDevice());
  proc.start(bin, {QStringLiteral("-i"), path, QStringLiteral("-o"), scratch,
                   QStringLiteral("-s"), QString::number(qMax(64, tilePx)),
                   QStringLiteral("-f")});
  if (!proc.waitForFinished(kTimeoutMs) ||
      proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0 ||
      !QFileInfo::exists(scratch)) {
    if (proc.state() != QProcess::NotRunning)
      proc.kill();
    QFile::remove(scratch);
    return decodeViaFfmpeg(path, tilePx);
  }
  const QImage img(scratch);
  QFile::remove(scratch);
  return img;
}

struct MosaicPick {
  QString path;
  QString name;
  QString mime;
  bool isDir = false;
};

constexpr int kMaxMosaicTiles = 10;

void sortMosaicPicks(QVector<MosaicPick> &picks) {
  std::sort(picks.begin(), picks.end(),
            [](const MosaicPick &a, const MosaicPick &b) {
              return QString::localeAwareCompare(a.name.toLower(),
                                                 b.name.toLower()) < 0;
            });
}

QVector<MosaicPick> scanMosaicEntries(const QString &dirPath, int cap,
                                      bool includeDirs) {
  QVector<MosaicPick> picks;
  QMimeDatabase db;
  DIR *d = opendir(QFile::encodeName(dirPath).constData());
  if (!d)
    return {};
  int scanned = 0;
  while (scanned < cap) {
    const struct dirent *ent = readdir(d);
    if (!ent)
      break;
    if (ent->d_name[0] == '.')
      continue;
    const QString name = QFile::decodeName(ent->d_name);
    const QString path = QDir(dirPath).filePath(name);
    const QFileInfo fi(path);
    const bool listedDir = ent->d_type == DT_DIR;
    const bool maybeLink =
        ent->d_type == DT_LNK || ent->d_type == DT_UNKNOWN || fi.isSymLink();
    if (listedDir || (maybeLink && fi.isDir())) {
      if (!includeDirs || !fi.isDir() || !fi.isReadable())
        continue;
      ++scanned;
      MosaicPick pick;
      pick.path = path;
      pick.name = name;
      pick.mime = QStringLiteral("inode/directory");
      pick.isDir = true;
      picks.append(pick);
      continue;
    }
    QString resolved = path;
    if (maybeLink) {
      const QString canon = fi.canonicalFilePath();
      const QFileInfo tgt(canon);
      if (canon.isEmpty() || !tgt.isFile() || !tgt.isReadable() || tgt.isDir())
        continue;
      resolved = tgt.absoluteFilePath();
    } else if (ent->d_type == DT_UNKNOWN || ent->d_type == DT_REG) {
      if (!fi.isFile() || !fi.isReadable())
        continue;
    } else if (ent->d_type != DT_REG) {
      continue;
    }
    ++scanned;
    const QString mime =
        db.mimeTypeForFile(resolved, QMimeDatabase::MatchExtension).name();
    MosaicPick pick;
    pick.path = resolved;
    pick.mime = mime;
    pick.name = name;
    picks.append(pick);
  }
  closedir(d);
  return picks;
}

QVector<MosaicPick> pickMosaicChildren(const QString &dirPath) {
  QVector<MosaicPick> picks = scanMosaicEntries(dirPath, 512, true);
  sortMosaicPicks(picks);
  if (picks.size() > kMaxMosaicTiles)
    picks.resize(kMaxMosaicTiles);
  return picks;
}

MosaicPick firstVisualInDir(const QString &dirPath) {
  QVector<MosaicPick> files = scanMosaicEntries(dirPath, 96, false);
  sortMosaicPicks(files);
  MosaicPick best;
  for (const MosaicPick &p : files) {
    if (looksLikeImage(p.path, p.mime))
      return p;
    if (best.path.isEmpty())
      best = p;
  }
  return best;
}

QImage renderFolderCard(const QString &name, const QImage &face, int px) {
  const ThumbTheme th = ThumbTheme::current();
  QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setRenderHint(QPainter::TextAntialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const int margin = qMax(2, px / 32);
  const QRect body(margin, margin, px - margin * 2, px - margin * 2);
  p.fillRect(body, th.surfaceRaised);
  if (!face.isNull()) {
    const int inset = qMax(3, px / 24);
    const QRect cover = body.adjusted(inset, inset, -inset, -inset);
    p.drawImage(cover, cropToSquare(face, qMax(cover.width(), cover.height())));
    const int barH = qMax(12, px / 4);
    QColor bar = th.background;
    bar.setAlpha(218);
    p.fillRect(body.left(), body.bottom() - barH + 1, body.width(), barH, bar);
    QFont f(th.fontFamily, qMax(6, px / 9));
    p.setFont(f);
    p.setPen(th.foreground);
    p.drawText(QRect(body.left() + 3, body.bottom() - barH + 1,
                     body.width() - 6, barH),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, name);
  } else {
    QFont f(th.fontFamily, qMax(6, px / 8));
    f.setBold(true);
    p.setFont(f);
    p.setPen(th.foreground);
    p.drawText(body.adjusted(5, body.height() / 4, -5, -5),
               Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name);
  }
  p.setBrush(Qt::NoBrush);
  p.setPen(th.border);
  p.drawRect(body.adjusted(0, 0, -1, -1));
  p.end();
  return img;
}

QImage renderUnknownFileImage(const QString &path, int px) {
  const ThumbTheme th = ThumbTheme::current();
  QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::transparent);
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing, false);
  p.setRenderHint(QPainter::TextAntialiasing);
  const int m = qMax(2, px / 14);
  const int fold = qBound(6, px / 5, px / 3);
  const QRect page(m, m, px - 2 * m, px - 2 * m);
  QPolygon body;
  body << QPoint(page.left(), page.top())
       << QPoint(page.right() - fold, page.top())
       << QPoint(page.right(), page.top() + fold)
       << QPoint(page.right(), page.bottom())
       << QPoint(page.left(), page.bottom());
  p.setPen(th.border);
  p.setBrush(th.background);
  p.drawPolygon(body);
  QPolygon ear;
  ear << QPoint(page.right() - fold, page.top())
      << QPoint(page.right(), page.top() + fold)
      << QPoint(page.right() - fold, page.top() + fold);
  QColor earFill = th.muted;
  earFill.setAlpha(80);
  p.setBrush(earFill);
  p.drawPolygon(ear);
  QString ext = QFileInfo(path).suffix().toUpper();
  if (ext.size() > 8)
    ext = ext.left(8);
  if (!ext.isEmpty() && px >= 28) {
    QFont f(th.fontFamily, qMax(6, px / 12));
    f.setBold(true);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int padX = qMax(3, px / 32);
    const int padY = qMax(2, px / 48);
    const int chipW = qMin(page.width() - 4, fm.horizontalAdvance(ext) + padX * 2);
    const int chipH = fm.height() + padY * 2;
    const QRect chip(page.right() - chipW - 3, page.bottom() - chipH - 3,
                     chipW, chipH);
    QColor chipFill = th.background;
    chipFill.setAlpha(220);
    p.fillRect(chip, chipFill);
    p.setPen(th.accent);
    p.drawText(chip.adjusted(padX, padY, -padX, -padY),
               Qt::AlignCenter | Qt::TextSingleLine, ext);
  }
  p.end();
  return img;
}

bool renderUnknownCard(const QString &src, const QString &dest, int sizePx) {
  const int px = qBound(64, sizePx, 512);
  const QImage img = renderUnknownFileImage(src, px);
  if (img.isNull())
    return false;
  QDir().mkpath(QFileInfo(dest).absolutePath());
  return img.save(dest, "PNG");
}

QImage fileTileImage(const MosaicPick &pick, int tilePx,
                     const QString &scratch) {
  const QFileInfo fi(pick.path);
  const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
  QImage img = decodeChildCached(pick.path, mtime, tilePx);
  if (img.isNull())
    img = ThumbnailService::decodeRaster(pick.path, tilePx * 2);
  if (img.isNull() && looksLikeVideo(pick.path, pick.mime))
    img = decodeVideoFrame(pick.path, tilePx, scratch);
  if (img.isNull() && looksLikeText(pick.path, pick.mime) &&
      renderTextCard(pick.path, scratch, tilePx)) {
    img = QImage(scratch);
    QFile::remove(scratch);
  }
  if (img.isNull() && looksLikeParquet(pick.path, pick.mime) &&
      renderParquetCard(pick.path, scratch, tilePx)) {
    img = QImage(scratch);
    QFile::remove(scratch);
  }
  if (img.isNull() && looksLikeArchive(pick.path, pick.mime) &&
      renderArchiveCard(pick.path, scratch, tilePx)) {
    img = QImage(scratch);
    QFile::remove(scratch);
  }
  if (img.isNull() && renderUnknownCard(pick.path, scratch, tilePx)) {
    img = QImage(scratch);
    QFile::remove(scratch);
  }
  return img;
}

QImage tileForChild(const MosaicPick &pick, int tilePx, const QString &scratch) {
  if (pick.isDir) {
    QImage face;
    const MosaicPick nested = firstVisualInDir(pick.path);
    if (!nested.path.isEmpty())
      face = fileTileImage(nested, tilePx, scratch);
    return renderFolderCard(pick.name, face, tilePx);
  }
  return cropToSquare(fileTileImage(pick, tilePx, scratch), tilePx);
}

bool renderTextCard(const QString &src, const QString &dest, int sizePx) {
  QFile f(src);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  QByteArray raw = f.read(4096);
  if (raw.left(qMin(raw.size(), 256)).contains('\0'))
    return false;
  const QString text = QString::fromUtf8(raw);
  const int px = qBound(64, sizePx, 512);
  const ThumbTheme th = ThumbTheme::current();
  QImage img(px, px, QImage::Format_ARGB32_Premultiplied);
  img.fill(th.background);
  QPainter p(&img);
  p.setRenderHint(QPainter::TextAntialiasing);
  const int rail = qMax(3, px / 28);
  p.fillRect(0, 0, rail, px, th.accent);
  // The filename and extension already sit directly below the tile. Use the
  // image surface for content instead of repeating that metadata over it.
  QStringList previewLines;
  const QStringList rawLines = text.split(QLatin1Char('\n'));
  for (QString line : rawLines) {
    line = line.simplified();
    if (line.isEmpty())
      continue;
    previewLines.append(line);
    if (previewLines.size() >= 12)
      break;
  }
  QFont body(th.fontFamily);
  body.setPixelSize(qMax(7, px / 17));
  p.setFont(body);
  p.setPen(th.subtleForeground);
  const QFontMetrics fm(body);
  const int left = rail + qMax(5, px / 28);
  const int right = qMax(5, px / 28);
  const int top = qMax(5, px / 28);
  const int lineH = qMax(fm.height(), px / 14);
  const int maxLines = qMax(1, (px - top * 2) / lineH);
  for (int i = 0; i < qMin(maxLines, previewLines.size()); ++i) {
    const QString line = fm.elidedText(previewLines.at(i), Qt::ElideRight,
                                       px - left - right);
    p.drawText(QRect(left, top + i * lineH, px - left - right, lineH),
               Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, line);
  }
  p.end();
  QDir().mkpath(QFileInfo(dest).absolutePath());
  return img.save(dest, "PNG");
}

const Thumbnailer *matchThumbnailer(const QVector<Thumbnailer> &list,
                                    const QString &path,
                                    const QString &mimeHint,
                                    QMimeDatabase &db) {
  QStringList candidates;
  if (!mimeHint.isEmpty())
    candidates.append(mimeHint);
  if (mimeHint == QLatin1String("image/heic") ||
      path.endsWith(QLatin1String(".heic"), Qt::CaseInsensitive))
    candidates.append(QStringLiteral("image/heif"));
  const QMimeType mt = db.mimeTypeForFile(path);
  if (!mt.isDefault() && !candidates.contains(mt.name()))
    candidates.append(mt.name());
  for (const QString &alias : mt.aliases()) {
    if (!candidates.contains(alias))
      candidates.append(alias);
  }
  for (const QString &cand : candidates) {
    for (const Thumbnailer &t : list) {
      if (t.mimes.contains(cand))
        return &t;
    }
  }
  return nullptr;
}

} // namespace

class ThumbnailEngine : public QObject {
public:
  explicit ThumbnailEngine(QObject *parent = nullptr) : QObject(parent) {
    m_thumbnailerDirs.append(QStringLiteral("/usr/share/thumbnailers"));
  }

  std::function<void(const QString &, const QString &)> notify;
  std::function<void(const QVector<ThumbnailResult> &)> notifyBatch;
  std::function<void(const QString &, qint64, const QVariantMap &)> notifyFacts;

  void setThumbnailerDirectories(const QStringList &dirs) {
    m_thumbnailerDirs = dirs;
    m_thumbnailers.clear();
    m_loaded = false;
  }

  void setHandlerThumbnailers(const QVector<ExecThumbnailer> &list) {
    m_handlerSpecs = list;
    m_handlers.clear();
    m_handlersLoaded = false;
  }

  void cancelAll() {
    m_pending.clear();
    while (!m_active.isEmpty())
      killActive(0, false);
  }

  void invalidate(const QString &path) {
    if (path.isEmpty())
      return;
    ThumbCache::instance().removePath(cachePathFor(path));
    const QString prefix = path + QLatin1Char('\n');
    for (auto it = m_pending.begin(); it != m_pending.end();) {
      if (it.value().path == path)
        it = m_pending.erase(it);
      else
        ++it;
    }
    for (auto it = m_ready.begin(); it != m_ready.end();) {
      if (it.key().startsWith(prefix))
        it = m_ready.erase(it);
      else
        ++it;
    }
    for (auto it = m_failed.begin(); it != m_failed.end();) {
      if (it->startsWith(prefix))
        it = m_failed.erase(it);
      else
        ++it;
    }
    for (int i = m_active.size() - 1; i >= 0; --i) {
      if (m_active.at(i).path == path)
        killActive(i, false);
    }
  }

  void submit(const QVector<ThumbnailJob> &jobs, bool exclusive) {
    QElapsedTimer elapsed;
    elapsed.start();
    QSet<QString> keep;
    keep.reserve(jobs.size());
    QVector<ThumbnailResult> hits;
    hits.reserve(jobs.size());
    QVector<Job> cold;
    cold.reserve(jobs.size());
    for (const ThumbnailJob &in : jobs) {
      if (in.path.isEmpty() || in.mtime <= 0 || in.sizePx <= 0)
        continue;
      if (isThumbCachePath(in.path))
        continue;
      Job job;
      job.path = in.path;
      job.mime = in.mime;
      job.mtime = in.mtime;
      job.sizePx = ThumbCache::canonicalSize(in.sizePx);
      job.key = jobKey(job.path, job.mtime, job.sizePx);
      job.priority = in.priority;
      job.mosaicPaths = in.mosaicPaths;
      job.mosaicLabel = in.mosaicLabel;
      keep.insert(job.key);

      if (const auto it = m_ready.constFind(job.key); it != m_ready.cend()) {
        const QString ready = ThumbCache::instance().lookupUrl(
            cachePath(job), job.mtime, job.sizePx);
        if (!ready.isEmpty()) {
          hits.append({job.path, ready});
          continue;
        }
        m_ready.erase(it);
      }
      if (m_failed.contains(job.key)) {
        if (notify)
          notify(job.path, QString());
        continue;
      }
      if (isActive(job.key)) {
        continue;
      }
      const QString hit = lookupPacked(job);
      if (!hit.isEmpty()) {
        m_ready.insert(job.key, hit);
        hits.append({job.path, hit});
        continue;
      }
      cold.append(job);
    }

    const int packedHits = hits.size();
    const qint64 packedMs = elapsed.elapsed();
    if (!hits.isEmpty() && notifyBatch) {
      notifyBatch(hits);
      hits.clear();
    }

    int legacyHits = 0;
    for (const Job &job : std::as_const(cold)) {
      const QString hit = lookupLegacyCache(job);
      if (!hit.isEmpty()) {
        ++legacyHits;
        m_ready.insert(job.key, hit);
        hits.append({job.path, hit});
        // Do not make the first legacy hits wait behind every remaining file.
        if (hits.size() >= 8 && notifyBatch) {
          notifyBatch(hits);
          hits.clear();
        }
        continue;
      }
      if (m_pending.contains(job.key))
        m_pending[job.key].priority = job.priority;
      else
        m_pending.insert(job.key, job);
    }

    if (!hits.isEmpty() && notifyBatch)
      notifyBatch(hits);

    if (exclusive) {
      for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (!keep.contains(it.key()))
          it = m_pending.erase(it);
        else
          ++it;
      }
      for (int i = m_active.size() - 1; i >= 0; --i) {
        if (!keep.contains(m_active.at(i).key))
          killActive(i, false);
      }
    }

    debugLog("thumb batch jobs=%d packed=%d legacy=%d pending=%d "
             "active=%d packed_lookup=%lldms total=%lldms",
             jobs.size(), packedHits, legacyHits, m_pending.size(),
             m_active.size(), static_cast<long long>(packedMs),
             static_cast<long long>(elapsed.elapsed()));
    kick();
  }

private:
  struct Job {
    QString key;
    QString path;
    QString mime;
    qint64 mtime = 0;
    int sizePx = 128;
    int priority = 0;
    QStringList mosaicPaths;
    QString mosaicLabel;
  };

  struct Active {
    QString key;
    QString path;
    QString dest;
    QString temp;
    qint64 mtime = 0;
    int sizePx = 128;
    QProcess *proc = nullptr;
  };

  bool isActive(const QString &key) const {
    for (const Active &a : m_active) {
      if (a.key == key)
        return true;
    }
    return false;
  }

  QString cachePath(const Job &job) const {
    if (!job.mosaicLabel.isEmpty())
      return job.path + QStringLiteral("#query-mosaic-v3-") +
             ThumbTheme::current().cacheId();
    return cachePathFor(job.path);
  }

  QString packedUrl(const Job &job) const {
    return ThumbCache::imageUrl(cachePath(job), job.mtime, job.sizePx);
  }

  bool storePacked(const Job &job, const QByteArray &png) {
    if (png.isEmpty())
      return false;
    ThumbCache::instance().putPng(cachePath(job), job.mtime, job.sizePx, png);
    return true;
  }

  bool storePackedImage(const Job &job, const QImage &img) {
    return storePacked(job, pngBytes(img));
  }

  QString lookupPacked(const Job &job) const {
    const QString packed = cachePath(job);
    return ThumbCache::instance().lookupUrl(packed, job.mtime, job.sizePx);
  }

  QString lookupLegacyCache(const Job &job) const {
    const QString packed = cachePath(job);
    const QString uri = ThumbnailService::canonicalFileUri(job.path);
    const qint64 mtimeSec = job.mtime / 1000;
    const QString preferred = ThumbnailService::xdgSizeDir(job.sizePx);
    QStringList dirs;
    dirs.append(preferred);
    for (const auto &s : kXdgSizes) {
      const QString name = QLatin1String(s.dir);
      if (name != preferred)
        dirs.append(name);
    }
    const QString hash = ThumbnailService::md5Hex(uri.toUtf8());
    const QString root =
        ThumbnailService::xdgCacheHome() + QStringLiteral("/thumbnails/");
    for (const QString &dir : dirs) {
      const QString png =
          root + dir + QLatin1Char('/') + hash + QStringLiteral(".png");
      if (ThumbnailService::isValidXdgThumbnail(png, uri, mtimeSec)) {
        ThumbCache::instance().ingestFile(packed, job.mtime, job.sizePx, png);
        return ThumbCache::imageUrl(packed, job.mtime, job.sizePx);
      }
    }
    const QString syn = destFor(job);
    if (QFileInfo::exists(syn)) {
      ThumbCache::instance().ingestFile(packed, job.mtime, job.sizePx, syn);
      return ThumbCache::imageUrl(packed, job.mtime, job.sizePx);
    }
    return {};
  }

  QString lookupCache(const Job &job) const {
    const QString packed = lookupPacked(job);
    return packed.isEmpty() ? lookupLegacyCache(job) : packed;
  }

  QString destFor(const Job &job) const {
    return ThumbnailService::synchroThumbPath(cachePath(job), job.mtime,
                                              job.sizePx);
  }

  QString scratchPath(const Job &job) const {
    const QString dir = ThumbCache::homeDir() + QStringLiteral("/tmp");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') +
           ThumbCache::makeKey(cachePath(job), job.mtime, job.sizePx) +
           QStringLiteral(".png");
  }

  void ensureThumbnailers() {
    if (!m_loaded) {
      m_thumbnailers = loadThumbnailers(m_thumbnailerDirs);
      m_loaded = true;
    }
    if (!m_handlersLoaded) {
      m_handlers.clear();
      for (const ExecThumbnailer &spec : m_handlerSpecs) {
        if (spec.exec.isEmpty() || !tryExecOk(spec.tryExec))
          continue;
        Thumbnailer t;
        t.exec = spec.exec;
        t.tryExec = spec.tryExec;
        t.mimes = spec.mimes;
        m_handlers.append(t);
      }
      m_handlersLoaded = true;
    }
  }

  void kick() {
    while (m_active.size() < kMaxWorkers && !m_pending.isEmpty()) {
      auto best = m_pending.end();
      int bestPri = std::numeric_limits<int>::max();
      for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it.value().priority < bestPri) {
          bestPri = it.value().priority;
          best = it;
        }
      }
      if (best == m_pending.end())
        break;
      const Job job = best.value();
      m_pending.erase(best);
      startGenerate(job);
    }
  }

  void startGenerate(const Job &job) {
    const QString hit = lookupCache(job);
    if (!hit.isEmpty()) {
      m_ready.insert(job.key, hit);
      if (notify)
        notify(job.path, hit);
      kick();
      return;
    }

    if (!job.mosaicPaths.isEmpty() || !job.mosaicLabel.isEmpty()) {
      const QImage mosaic = ThumbnailService::renderPathMosaicImage(
          job.mosaicPaths, job.mosaicLabel, job.sizePx);
      if (!mosaic.isNull() && storePackedImage(job, mosaic)) {
        const QString url = packedUrl(job);
        m_ready.insert(job.key, url);
        if (notify)
          notify(job.path, url);
      } else {
        fail(job.key, job.path);
      }
      kick();
      return;
    }

    const QFileInfo fi(job.path);
    if (!fi.exists()) {
      fail(job.key, job.path);
      kick();
      return;
    }

    if (fi.isDir()) {
      const QImage mosaic =
          ThumbnailService::renderFolderMosaicImage(job.path, job.sizePx);
      if (!mosaic.isNull() && storePackedImage(job, mosaic)) {
        const QString url = packedUrl(job);
        m_ready.insert(job.key, url);
        if (notify)
          notify(job.path, url);
      } else {
        fail(job.key, job.path);
      }
      kick();
      return;
    }

    if (!fi.isReadable()) {
      fail(job.key, job.path);
      kick();
      return;
    }

    if (looksLikeImage(job.path, job.mime)) {
      QImage direct = ThumbnailService::decodeRaster(job.path, job.sizePx);
      if (!direct.isNull()) {
        if (notifyFacts)
          notifyFacts(job.path, job.mtime,
                      ThumbnailService::deterministicImageFacts(job.path,
                                                                direct));
        direct = scaleToFit(direct, job.sizePx);
        if (storePackedImage(job, direct)) {
          const QString url = packedUrl(job);
          m_ready.insert(job.key, url);
          if (notify)
            notify(job.path, url);
          kick();
          return;
        }
      }
    }

    ensureThumbnailers();
    const Thumbnailer *thumb =
        matchThumbnailer(m_handlers, job.path, job.mime, m_mime);
    if (!thumb)
      thumb = matchThumbnailer(m_thumbnailers, job.path, job.mime, m_mime);
    if (!thumb) {
      const QString scratch = scratchPath(job);
      if (looksLikeArchive(job.path, job.mime) &&
          renderArchiveCard(job.path, scratch, job.sizePx)) {
        QFile f(scratch);
        QByteArray png;
        if (f.open(QIODevice::ReadOnly))
          png = f.readAll();
        f.remove();
        if (storePacked(job, png)) {
          const QString url = packedUrl(job);
          m_ready.insert(job.key, url);
          if (notify)
            notify(job.path, url);
          kick();
          return;
        }
      }
      if (looksLikeParquet(job.path, job.mime) &&
          renderParquetCard(job.path, scratch, job.sizePx)) {
        QFile f(scratch);
        QByteArray png;
        if (f.open(QIODevice::ReadOnly))
          png = f.readAll();
        f.remove();
        if (storePacked(job, png)) {
          const QString url = packedUrl(job);
          m_ready.insert(job.key, url);
          if (notify)
            notify(job.path, url);
          kick();
          return;
        }
      }
      if (looksLikeText(job.path, job.mime) &&
          renderTextCard(job.path, scratch, job.sizePx)) {
        QFile f(scratch);
        QByteArray png;
        if (f.open(QIODevice::ReadOnly))
          png = f.readAll();
        f.remove();
        if (storePacked(job, png)) {
          const QString url = packedUrl(job);
          m_ready.insert(job.key, url);
          if (notify)
            notify(job.path, url);
          kick();
          return;
        }
      }
      if (renderUnknownCard(job.path, scratch, job.sizePx)) {
        QFile f(scratch);
        QByteArray png;
        if (f.open(QIODevice::ReadOnly))
          png = f.readAll();
        f.remove();
        if (storePacked(job, png)) {
          const QString url = packedUrl(job);
          m_ready.insert(job.key, url);
          if (notify)
            notify(job.path, url);
          kick();
          return;
        }
      }
      fail(job.key, job.path);
      kick();
      return;
    }

    const QString dest = scratchPath(job);
    const QString temp = dest +
                         QStringLiteral(".tmp.%1").arg(QString::number(
                             reinterpret_cast<quintptr>(this), 16)) +
                         QLatin1Char('.') + QString::number(m_seq++);
    QFile::remove(temp);

    const QStringList argv = ThumbnailService::parseExec(
        thumb->exec, ThumbnailService::canonicalPath(job.path), temp,
        job.sizePx);
    if (argv.isEmpty()) {
      fail(job.key, job.path);
      kick();
      return;
    }
    const QString program = resolveProgram(argv.first());
    if (program.isEmpty() || !QFileInfo(program).isExecutable()) {
      fail(job.key, job.path);
      kick();
      return;
    }

    auto *proc = new QProcess(this);
    proc->setProgram(program);
    proc->setArguments(argv.mid(1));
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setStandardOutputFile(QProcess::nullDevice());
    proc->setStandardErrorFile(QProcess::nullDevice());

    Active a;
    a.key = job.key;
    a.path = job.path;
    a.dest = dest;
    a.temp = temp;
    a.mtime = job.mtime;
    a.sizePx = job.sizePx;
    a.proc = proc;
    m_active.append(a);

    QObject::connect(
        proc, &QProcess::finished, this,
        [this, proc](int, QProcess::ExitStatus) { onProcessDone(proc); });
    QObject::connect(proc, &QProcess::errorOccurred, this,
                     [this, proc](QProcess::ProcessError err) {
                       if (err == QProcess::FailedToStart)
                         onProcessDone(proc);
                     });

    auto *timer = new QTimer(proc);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, proc, [proc] {
      if (proc->state() != QProcess::NotRunning)
        proc->kill();
    });
    timer->start(kTimeoutMs);
    proc->start();
  }

  void onProcessDone(QProcess *proc) {
    int idx = -1;
    for (int i = 0; i < m_active.size(); ++i) {
      if (m_active.at(i).proc == proc) {
        idx = i;
        break;
      }
    }
    if (idx < 0) {
      proc->deleteLater();
      return;
    }
    Active a = m_active.takeAt(idx);
    const bool ok = a.proc && a.proc->exitStatus() == QProcess::NormalExit &&
                    a.proc->exitCode() == 0 && QFileInfo::exists(a.temp) &&
                    QFileInfo(a.temp).size() > 0;
    if (ok) {
      QFile f(a.temp);
      QByteArray png;
      if (f.open(QIODevice::ReadOnly))
        png = f.readAll();
      f.remove();
      QFile::remove(a.dest);
      if (!png.isEmpty() && a.sizePx > 0 && a.mtime > 0) {
        ThumbCache::instance().putPng(cachePathFor(a.path), a.mtime, a.sizePx,
                                      png);
        const QString url =
            ThumbCache::imageUrl(cachePathFor(a.path), a.mtime, a.sizePx);
        m_ready.insert(a.key, url);
        if (notify)
          notify(a.path, url);
      } else {
        fail(a.key, a.path);
      }
    } else {
      QFile::remove(a.temp);
      Job fb;
      fb.key = a.key;
      fb.path = a.path;
      fb.mtime = a.mtime;
      fb.sizePx = a.sizePx;
      const QString scratch = scratchPath(fb);
      bool saved = false;
      if (renderUnknownCard(fb.path, scratch, fb.sizePx)) {
        QFile f(scratch);
        QByteArray png;
        if (f.open(QIODevice::ReadOnly))
          png = f.readAll();
        f.remove();
        saved = storePacked(fb, png);
      }
      if (saved) {
        const QString url = packedUrl(fb);
        m_ready.insert(a.key, url);
        if (notify)
          notify(a.path, url);
      } else {
        fail(a.key, a.path);
      }
    }
    a.proc->deleteLater();
    kick();
  }

  void killActive(int index, bool markFailed) {
    if (index < 0 || index >= m_active.size())
      return;
    Active a = m_active.takeAt(index);
    if (a.proc) {
      if (a.proc->state() != QProcess::NotRunning) {
        a.proc->disconnect();
        a.proc->kill();
        a.proc->waitForFinished(200);
      }
      a.proc->deleteLater();
    }
    QFile::remove(a.temp);
    if (markFailed)
      fail(a.key, a.path);
  }

  void fail(const QString &key, const QString &path) {
    m_failed.insert(key);
    if (notify)
      notify(path, QString());
  }

  QStringList m_thumbnailerDirs;
  QVector<Thumbnailer> m_thumbnailers;
  QVector<ExecThumbnailer> m_handlerSpecs;
  QVector<Thumbnailer> m_handlers;
  bool m_loaded = false;
  bool m_handlersLoaded = false;
  QHash<QString, Job> m_pending;
  QHash<QString, QString> m_ready;
  QSet<QString> m_failed;
  QVector<Active> m_active;
  QMimeDatabase m_mime;
  quint64 m_seq = 0;
};

QString ThumbnailService::xdgCacheHome() {
  const QByteArray env = qgetenv("XDG_CACHE_HOME");
  if (!env.isEmpty())
    return QString::fromLocal8Bit(env);
  return QDir::homePath() + QStringLiteral("/.cache");
}

QString ThumbnailService::synchroThumbsDir() {
  return xdgCacheHome() + QStringLiteral("/synchro/thumbs");
}

QString ThumbnailService::canonicalPath(const QString &path) {
  const QFileInfo fi(path);
  const QString canon = fi.canonicalFilePath();
  return canon.isEmpty() ? fi.absoluteFilePath() : canon;
}

QString ThumbnailService::canonicalFileUri(const QString &path) {
  const QByteArray bytes = QFile::encodeName(canonicalPath(path));
  QByteArray out("file://");
  out.reserve(7 + bytes.size() * 3);
  for (unsigned char c : bytes) {
    if (uriPathAllowed(c)) {
      out.append(char(c));
    } else {
      char buf[4];
      std::snprintf(buf, sizeof(buf), "%%%02X", c);
      out.append(buf, 3);
    }
  }
  return QString::fromLatin1(out);
}

QString ThumbnailService::md5Hex(const QByteArray &data) {
  return QString::fromLatin1(
      QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
}

QString ThumbnailService::synchroThumbPath(const QString &path, qint64 mtime,
                                           int sizePx) {
  QCryptographicHash hash(QCryptographicHash::Md5);
  hash.addData(path.toUtf8());
  hash.addData(QByteArray::number(mtime));
  hash.addData(QByteArray::number(sizePx));
  return synchroThumbsDir() + QLatin1Char('/') +
         QString::fromLatin1(hash.result().toHex()) + QStringLiteral(".png");
}

QString ThumbnailService::xdgSizeDir(int sizePx) {
  for (const auto &s : kXdgSizes) {
    if (sizePx <= s.px)
      return QLatin1String(s.dir);
  }
  return QStringLiteral("xx-large");
}

QString ThumbnailService::xdgThumbPath(const QString &path, int sizePx) {
  const QString uri = canonicalFileUri(path);
  return xdgCacheHome() + QStringLiteral("/thumbnails/") + xdgSizeDir(sizePx) +
         QLatin1Char('/') + md5Hex(uri.toUtf8()) + QStringLiteral(".png");
}

QHash<QString, QString> ThumbnailService::readPngText(const QString &pngPath) {
  QHash<QString, QString> out;
  QFile f(pngPath);
  if (!f.open(QIODevice::ReadOnly))
    return out;
  static const unsigned char kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  const QByteArray sig = f.read(8);
  if (sig.size() != 8 || memcmp(sig.constData(), kSig, 8) != 0)
    return out;
  while (!f.atEnd()) {
    const QByteArray lenb = f.read(4);
    if (lenb.size() < 4)
      break;
    const quint32 len = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(lenb.constData()));
    const QByteArray type = f.read(4);
    if (type.size() < 4)
      break;
    if (len > 4u * 1024u * 1024u)
      break;
    const QByteArray data = f.read(len);
    f.read(4);
    if (data.size() != int(len))
      break;
    if (type == "tEXt") {
      const int z = data.indexOf('\0');
      if (z > 0) {
        const QString key = QString::fromLatin1(data.constData(), z);
        const QString value =
            QString::fromLatin1(data.constData() + z + 1, data.size() - z - 1);
        out.insert(key, value);
      }
    } else if (type == "zTXt") {
      const int z = data.indexOf('\0');
      if (z > 0 && z + 2 <= data.size() && data.at(z + 1) == char(0)) {
        const QByteArray plain = inflateZlib(data.mid(z + 2));
        if (!plain.isEmpty()) {
          out.insert(QString::fromLatin1(data.constData(), z),
                     QString::fromLatin1(plain));
        }
      }
    } else if (type == "iTXt") {
      const int z = data.indexOf('\0');
      if (z > 0 && z + 3 < data.size()) {
        const QString key = QString::fromLatin1(data.constData(), z);
        const int comp = int(uchar(data.at(z + 1)));
        int p = z + 3;
        const int langEnd = data.indexOf('\0', p);
        if (langEnd < 0)
          continue;
        p = langEnd + 1;
        const int transEnd = data.indexOf('\0', p);
        if (transEnd < 0)
          continue;
        if (comp == 0) {
          out.insert(key, QString::fromUtf8(data.constData() + transEnd + 1,
                                            data.size() - transEnd - 1));
        }
      }
    } else if (type == "IEND") {
      break;
    }
  }
  return out;
}

bool ThumbnailService::isValidXdgThumbnail(const QString &pngPath,
                                           const QString &uri,
                                           qint64 mtimeSec) {
  if (!QFileInfo::exists(pngPath))
    return false;
  const QHash<QString, QString> text = readPngText(pngPath);
  const QString gotUri = text.value(QStringLiteral("Thumb::URI"));
  const QString gotMtime = text.value(QStringLiteral("Thumb::MTime"));
  if (gotUri.isEmpty() || gotMtime.isEmpty())
    return false;
  if (gotUri != uri)
    return false;
  bool ok = false;
  const qint64 parsed = gotMtime.toLongLong(&ok);
  return ok && parsed == mtimeSec;
}

QStringList ThumbnailService::splitExec(const QString &exec) {
  QStringList out;
  QString cur;
  bool inQuote = false;
  for (int i = 0; i < exec.size(); ++i) {
    const QChar c = exec.at(i);
    if (c == QLatin1Char('\\') && i + 1 < exec.size()) {
      cur += exec.at(++i);
      continue;
    }
    if (c == QLatin1Char('"')) {
      inQuote = !inQuote;
      continue;
    }
    if (!inQuote && c.isSpace()) {
      if (!cur.isEmpty()) {
        out.append(cur);
        cur.clear();
      }
      continue;
    }
    cur += c;
  }
  if (!cur.isEmpty())
    out.append(cur);
  return out;
}

QString ThumbnailService::expandFieldCodes(const QString &token,
                                           const QString &inputPath,
                                           const QString &uri,
                                           const QString &outputPath,
                                           int sizePx) {
  QString out;
  out.reserve(token.size());
  for (int i = 0; i < token.size(); ++i) {
    if (token.at(i) != QLatin1Char('%')) {
      out += token.at(i);
      continue;
    }
    if (i + 1 >= token.size())
      break;
    const QChar code = token.at(++i);
    if (code == QLatin1Char('%'))
      out += QLatin1Char('%');
    else if (code == QLatin1Char('i'))
      out += inputPath;
    else if (code == QLatin1Char('u'))
      out += uri;
    else if (code == QLatin1Char('o'))
      out += outputPath;
    else if (code == QLatin1Char('s'))
      out += QString::number(sizePx);
  }
  return out;
}

QStringList ThumbnailService::parseExec(const QString &exec,
                                        const QString &inputPath,
                                        const QString &outputPath, int sizePx) {
  const QString uri = canonicalFileUri(inputPath);
  const QStringList raw = splitExec(exec);
  QStringList out;
  out.reserve(raw.size());
  for (const QString &tok : raw) {
    const QString expanded =
        expandFieldCodes(tok, inputPath, uri, outputPath, sizePx);
    if (!expanded.isEmpty())
      out.append(expanded);
  }
  return out;
}

QString ThumbnailService::fileUrl(const QString &path) {
  return QString::fromUtf8(QUrl::fromLocalFile(path).toEncoded());
}

QImage readWithQt(const QString &path, int maxEdge, bool scaleHint,
                  bool autoXform) {
  QImageReader reader(path);
  reader.setAutoTransform(autoXform);
  if (!reader.canRead())
    return {};
  const QSize sz = reader.size();
  if (scaleHint && maxEdge > 0 && sz.isValid() &&
      (sz.width() > maxEdge || sz.height() > maxEdge)) {
    reader.setScaledSize(sz.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio));
  } else if (!scaleHint && sz.isValid()) {
    const qint64 px = qint64(sz.width()) * qint64(sz.height());
    if (sz.width() > 4096 || sz.height() > 4096 || px > 8ll * 1000 * 1000)
      return {};
  }
  return reader.read();
}

QImage ThumbnailService::decodeRaster(const QString &path, int maxEdge) {
  QImage img = readWithQt(path, maxEdge, true, true);
  if (img.isNull())
    img = readWithQt(path, maxEdge, false, true);
  if (img.isNull())
    img = readWithQt(path, maxEdge, true, false);
  if (!img.isNull())
    return scaleToFit(img, maxEdge);

#ifdef SYNCHRO_HAVE_WEBP
  if (looksLikeWebP(path, QString()) || sniffKind(path) == SniffKind::WebP) {
    QFile f(path);
    if (f.open(QIODevice::ReadOnly) && f.size() > 0 &&
        f.size() <= 80ll * 1024 * 1024) {
      const QImage webp = decodeWebPBytes(f.readAll(), maxEdge);
      if (!webp.isNull())
        return webp;
    }
  }
#endif

  if (looksLikeImage(path, QString()) || looksLikeWebP(path, QString()) ||
      looksLikeVideo(path, QString()))
    return scaleToFit(decodeViaFfmpeg(path, maxEdge), maxEdge);
  return {};
}

QVariantMap ThumbnailService::deterministicImageFacts(const QString &path,
                                                       const QImage &image) {
  QVariantMap facts;
  if (image.isNull())
    return facts;

  QSize sourceSize;
  QImageReader reader(path);
  if (reader.canRead())
    sourceSize = reader.size();
  if (!sourceSize.isValid())
    sourceSize = image.size();

  const double aspect = sourceSize.height() > 0
                            ? double(sourceSize.width()) / sourceSize.height()
                            : 0.0;
  facts.insert(QStringLiteral("width"), sourceSize.width());
  facts.insert(QStringLiteral("height"), sourceSize.height());
  facts.insert(QStringLiteral("aspect_ratio"),
               qRound(aspect * 1000.0) / 1000.0);
  facts.insert(QStringLiteral("orientation"),
               aspect > 1.08   ? QStringLiteral("landscape")
               : aspect < 0.92 ? QStringLiteral("portrait")
                               : QStringLiteral("square"));

  const QImage sample =
      image.convertToFormat(QImage::Format_RGB32)
          .scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  quint64 red = 0, green = 0, blue = 0;
  double luminance = 0.0;
  double saturation = 0.0;
  int chromatic = 0;
  QHash<QString, int> families;
  for (int y = 0; y < sample.height(); ++y) {
    const auto *line = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
    for (int x = 0; x < sample.width(); ++x) {
      const QColor color(line[x]);
      red += color.red();
      green += color.green();
      blue += color.blue();
      luminance += 0.2126 * color.redF() + 0.7152 * color.greenF() +
                   0.0722 * color.blueF();
      saturation += color.hsvSaturationF();
      if (color.hsvSaturationF() < 0.16 || color.valueF() < 0.10)
        continue;
      ++chromatic;
      const double hue = color.hsvHueF() * 360.0;
      QString family;
      if (hue < 18.0 || hue >= 345.0)
        family = QStringLiteral("red");
      else if (hue < 48.0)
        family = QStringLiteral("orange");
      else if (hue < 72.0)
        family = QStringLiteral("yellow");
      else if (hue < 165.0)
        family = QStringLiteral("green");
      else if (hue < 195.0)
        family = QStringLiteral("cyan");
      else if (hue < 260.0)
        family = QStringLiteral("blue");
      else if (hue < 305.0)
        family = QStringLiteral("purple");
      else
        family = QStringLiteral("pink");
      ++families[family];
    }
  }
  const int pixels = qMax(1, sample.width() * sample.height());
  const QColor average(int(red / pixels), int(green / pixels),
                       int(blue / pixels));
  QString dominant = luminance / pixels < 0.13 ? QStringLiteral("black")
                     : luminance / pixels > 0.90 && saturation / pixels < 0.12
                         ? QStringLiteral("white")
                         : QStringLiteral("gray");
  int dominantCount = 0;
  for (auto it = families.cbegin(); it != families.cend(); ++it) {
    if (it.value() > dominantCount) {
      dominant = it.key();
      dominantCount = it.value();
    }
  }
  const int bluePixels = families.value(QStringLiteral("blue")) +
                         families.value(QStringLiteral("cyan"));
  facts.insert(QStringLiteral("dominant_color"), average.name(QColor::HexRgb));
  facts.insert(QStringLiteral("color_family"), dominant);
  facts.insert(QStringLiteral("brightness"),
               qRound((luminance / pixels) * 1000.0) / 1000.0);
  facts.insert(QStringLiteral("saturation"),
               qRound((saturation / pixels) * 1000.0) / 1000.0);
  facts.insert(QStringLiteral("blue_share"),
               qRound((double(bluePixels) / pixels) * 1000.0) / 1000.0);
  facts.insert(QStringLiteral("chromatic_share"),
               qRound((double(chromatic) / pixels) * 1000.0) / 1000.0);

  const QImage hashSample =
      image.convertToFormat(QImage::Format_Grayscale8)
          .scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  int grayTotal = 0;
  for (int y = 0; y < 8; ++y) {
    const uchar *line = hashSample.constScanLine(y);
    for (int x = 0; x < 8; ++x)
      grayTotal += line[x];
  }
  const int grayAverage = grayTotal / 64;
  quint64 hash = 0;
  for (int y = 0; y < 8; ++y) {
    const uchar *line = hashSample.constScanLine(y);
    for (int x = 0; x < 8; ++x) {
      hash <<= 1;
      if (line[x] >= grayAverage)
        hash |= 1;
    }
  }
  facts.insert(QStringLiteral("visual_hash"),
               QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0')));
  return facts;
}

QString ThumbnailService::ensureRasterPng(const QString &path, qint64 mtime,
                                          int maxEdge) {
  if (path.isEmpty())
    return {};
  if (mtime <= 0)
    mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
  if (maxEdge <= 0)
    maxEdge = 4096;
  const QString dest = synchroThumbPath(path, mtime, maxEdge);
  if (QFileInfo::exists(dest) && QFileInfo(dest).size() > 0)
    return dest;
  const QImage img = decodeRaster(path, maxEdge);
  if (img.isNull())
    return {};
  QDir().mkpath(synchroThumbsDir());
  const QFile::Permissions ownerOnly =
      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
  QFile::setPermissions(synchroThumbsDir(), ownerOnly);
  if (!img.save(dest, "PNG"))
    return {};
  QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
  return dest;
}

namespace {

QVector<QRect> adaptiveMosaicTargets(const QRect &area, int gap, int count) {
  QVector<QRect> out;
  count = qBound(0, count, kMaxMosaicTiles);
  if (count == 0 || area.width() < 1 || area.height() < 1)
    return out;
  if (count == 1) {
    out.append(area);
    return out;
  }
  const int leftW = qMax(1, (area.width() - gap) / 2);
  const int rightW = qMax(1, area.width() - gap - leftW);
  if (count == 2) {
    out.append(QRect(area.left(), area.top(), leftW, area.height()));
    out.append(QRect(area.left() + leftW + gap, area.top(), rightW,
                     area.height()));
    return out;
  }
  const int topH = qMax(1, (area.height() - gap) / 2);
  const int bottomH = qMax(1, area.height() - gap - topH);
  if (count == 3) {
    out.append(QRect(area.left(), area.top(), leftW, area.height()));
    out.append(QRect(area.left() + leftW + gap, area.top(), rightW, topH));
    out.append(QRect(area.left() + leftW + gap, area.top() + topH + gap,
                     rightW, bottomH));
    return out;
  }

  const auto appendRow = [&](int rowCount, int y, int height) {
    const int available = qMax(rowCount, area.width() - gap * (rowCount - 1));
    int x = area.left();
    for (int column = 0; column < rowCount; ++column) {
      const int next = area.left() +
                       ((column + 1) * available) / rowCount + column * gap;
      const int width = qMax(1, next - x);
      out.append(QRect(x, y, width, height));
      x = next + gap;
    }
  };
  const int topCount = (count + 1) / 2;
  const int bottomCount = count / 2;
  appendRow(topCount, area.top(), topH);
  appendRow(bottomCount, area.top() + topH + gap, bottomH);
  return out;
}

} // namespace

QImage ThumbnailService::renderFolderMosaicImage(const QString &dirPath,
                                                 int sizePx) {
  if (dirPath.isEmpty())
    return {};
  const QFileInfo dirInfo(dirPath);
  if (!dirInfo.exists() || !dirInfo.isDir() || !dirInfo.isReadable())
    return {};
  const int px = qBound(64, sizePx, 512);
  const QVector<MosaicPick> kids = pickMosaicChildren(dirPath);
  if (kids.isEmpty())
    return renderFolderCard(dirInfo.fileName(), QImage(), px);

  const ThumbTheme th = ThumbTheme::current();
  const int gap = qMax(2, px / 32);
  const QRect body(gap, gap, px - gap * 2, px - gap * 2);
  const QRect contact = body.adjusted(gap, gap, -gap, -gap);
  if (contact.width() < 8 || contact.height() < 8)
    return {};

  QImage canvas(px, px, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QPainter p(&canvas);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.fillRect(body, th.surfaceRaised);

  const QString scratchDir = ThumbCache::homeDir() + QStringLiteral("/tmp");
  QDir().mkpath(scratchDir);
  const QString scratchBase =
      scratchDir + QLatin1Char('/') +
      ThumbCache::makeKey(dirPath, 0, sizePx) + QStringLiteral(".tile");
  QVector<QImage> tiles;
  const QVector<QRect> expectedTargets =
      adaptiveMosaicTargets(contact, gap, kids.size());
  for (int i = 0; i < kids.size() && tiles.size() < kMaxMosaicTiles; ++i) {
    const QString scratch =
        scratchBase + QLatin1Char('.') + QString::number(i) +
        QStringLiteral(".png");
    const QRect expected = expectedTargets.value(i, contact);
    const int tilePx = qMax(expected.width(), expected.height());
    const QImage tile = tileForChild(kids.at(i), tilePx, scratch);
    QFile::remove(scratch);
    if (tile.isNull())
      continue;
    tiles.append(tile);
  }
  const QVector<QRect> targets =
      adaptiveMosaicTargets(contact, gap, tiles.size());
  for (int i = 0; i < tiles.size(); ++i)
    p.drawImage(targets.at(i), cropToAspect(tiles.at(i),
                                            targets.at(i).size()));
  p.setBrush(Qt::NoBrush);
  p.setPen(th.border);
  p.drawRect(body.adjusted(0, 0, -1, -1));
  p.end();
  if (tiles.isEmpty())
    return renderFolderCard(dirInfo.fileName(), QImage(), px);
  return canvas;
}

QImage ThumbnailService::renderPathMosaicImage(const QStringList &paths,
                                               const QString &label,
                                               int sizePx) {
  QVector<MosaicPick> picks;
  QMimeDatabase db;
  for (const QString &raw : paths) {
    const QFileInfo fi(raw);
    if (!fi.exists() || !fi.isReadable())
      continue;
    MosaicPick pick;
    pick.path = fi.absoluteFilePath();
    pick.name = fi.fileName();
    pick.isDir = fi.isDir();
    pick.mime = pick.isDir
                    ? QStringLiteral("inode/directory")
                    : db.mimeTypeForFile(pick.path,
                                         QMimeDatabase::MatchExtension)
                          .name();
    picks.append(pick);
    if (picks.size() >= kMaxMosaicTiles)
      break;
  }

  const int px = qBound(64, sizePx, 512);
  if (picks.isEmpty())
    return renderFolderCard(label, QImage(), px);
  const ThumbTheme th = ThumbTheme::current();
  const int gap = qMax(2, px / 32);
  const QRect body(gap, gap, px - gap * 2, px - gap * 2);
  const QRect contact = body.adjusted(gap, gap, -gap, -gap);
  if (contact.width() < 8 || contact.height() < 8)
    return {};

  QImage canvas(px, px, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.fillRect(body, th.surfaceRaised);
  const QString scratchDir = ThumbCache::homeDir() + QStringLiteral("/tmp");
  QDir().mkpath(scratchDir);
  const QString scratchBase =
      scratchDir + QLatin1Char('/') +
      ThumbCache::makeKey(paths.join(QLatin1Char('\n')), 0, sizePx) +
      QStringLiteral(".query-tile");
  QVector<QImage> tiles;
  const QVector<QRect> expectedTargets =
      adaptiveMosaicTargets(contact, gap, picks.size());
  for (int i = 0; i < picks.size() && tiles.size() < kMaxMosaicTiles; ++i) {
    const QString scratch = scratchBase + QLatin1Char('.') +
                            QString::number(i) + QStringLiteral(".png");
    const QRect expected = expectedTargets.value(i, contact);
    const int tilePx = qMax(expected.width(), expected.height());
    const QImage tile = tileForChild(picks.at(i), tilePx, scratch);
    QFile::remove(scratch);
    if (tile.isNull())
      continue;
    tiles.append(tile);
  }
  const QVector<QRect> targets =
      adaptiveMosaicTargets(contact, gap, tiles.size());
  for (int i = 0; i < tiles.size(); ++i)
    painter.drawImage(targets.at(i), cropToAspect(tiles.at(i),
                                                  targets.at(i).size()));
  painter.setBrush(Qt::NoBrush);
  painter.setPen(th.border);
  painter.drawRect(body.adjusted(0, 0, -1, -1));
  painter.end();
  if (tiles.isEmpty())
    return renderFolderCard(label, QImage(), px);
  return canvas;
}

bool ThumbnailService::renderFolderMosaic(const QString &dirPath,
                                          const QString &dest, int sizePx) {
  if (dest.isEmpty())
    return false;
  const QImage canvas = renderFolderMosaicImage(dirPath, sizePx);
  if (canvas.isNull())
    return false;
  QDir().mkpath(QFileInfo(dest).absolutePath());
  if (!canvas.save(dest, "PNG"))
    return false;
  QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
  return true;
}

ThumbnailService::ThumbnailService(QObject *parent) : QObject(parent) {
  qRegisterMetaType<ThumbnailJob>();
  qRegisterMetaType<QVector<ThumbnailJob>>();
  qRegisterMetaType<ThumbnailResult>();
  qRegisterMetaType<QVector<ThumbnailResult>>();
  qRegisterMetaType<ExecThumbnailer>();
  qRegisterMetaType<QVector<ExecThumbnailer>>();

  m_engine = new ThumbnailEngine;
  m_engine->notify = [this](const QString &path, const QString &url) {
    QMetaObject::invokeMethod(
        this,
        [this, path, url] { emit thumbnailReady(path, displayUrl(path, url)); },
        Qt::QueuedConnection);
  };
  m_engine->notifyBatch = [this](const QVector<ThumbnailResult> &results) {
    QMetaObject::invokeMethod(
        this,
        [this, results] {
          QVector<ThumbnailResult> display;
          display.reserve(results.size());
          for (const ThumbnailResult &result : results)
            display.append(
                {result.path, displayUrl(result.path, result.url)});
          emit thumbnailsReady(display);
        },
        Qt::QueuedConnection);
  };
  m_engine->notifyFacts =
      [this](const QString &path, qint64 mtime, const QVariantMap &facts) {
        QMetaObject::invokeMethod(
            this,
            [this, path, mtime, facts] {
              emit imageFactsReady(path, mtime, facts);
            },
            Qt::QueuedConnection);
      };
  m_engine->moveToThread(&m_thread);
  connect(
      this, &ThumbnailService::submitted, m_engine,
      [eng = m_engine](const QVector<ThumbnailJob> &jobs, bool exclusive) {
        eng->submit(jobs, exclusive);
      },
      Qt::QueuedConnection);
  connect(
      this, &ThumbnailService::cancelRequested, m_engine,
      [eng = m_engine] { eng->cancelAll(); }, Qt::QueuedConnection);
  connect(
      this, &ThumbnailService::thumbnailerDirectoriesChanged, m_engine,
      [eng = m_engine](const QStringList &dirs) {
        eng->setThumbnailerDirectories(dirs);
      },
      Qt::QueuedConnection);
  connect(
      this, &ThumbnailService::handlerThumbnailersChanged, m_engine,
      [eng = m_engine](const QVector<ExecThumbnailer> &list) {
        eng->setHandlerThumbnailers(list);
      },
      Qt::QueuedConnection);
  connect(this, &ThumbnailService::invalidateRequested, m_engine,
          [eng = m_engine](const QString &path) { eng->invalidate(path); },
          Qt::QueuedConnection);
  m_thread.start();
}

ThumbnailService::~ThumbnailService() {
  if (m_thread.isRunning() && m_engine) {
    QMetaObject::invokeMethod(
        m_engine, [eng = m_engine] { eng->cancelAll(); },
        Qt::BlockingQueuedConnection);
  }
  m_thread.quit();
  m_thread.wait();
  delete m_engine;
  m_engine = nullptr;
}

QString ThumbnailService::packedUrl(const QString &path, qint64 mtime,
                                    int sizePx) {
  return ThumbCache::imageUrl(cachePathFor(path), mtime, sizePx);
}

bool hydratePacked(const ThumbnailJob &job, QString *url) {
  const QString packed = cachePathFor(job.path);
  auto *cache = &ThumbCache::instance();
  QString hit = cache->lookupUrl(packed, job.mtime, job.sizePx);
  if (hit.isEmpty()) {
    const QString uri = ThumbnailService::canonicalFileUri(job.path);
    const qint64 mtimeSec = job.mtime / 1000;
    const QString xdg =
        ThumbnailService::xdgThumbPath(job.path, job.sizePx);
    if (ThumbnailService::isValidXdgThumbnail(xdg, uri, mtimeSec))
      cache->ingestFile(packed, job.mtime, job.sizePx, xdg);
    else {
      const QString syn = ThumbnailService::synchroThumbPath(
          packed, job.mtime, job.sizePx);
      if (QFileInfo::exists(syn))
        cache->ingestFile(packed, job.mtime, job.sizePx, syn);
    }
  }
  hit = cache->lookupUrl(packed, job.mtime, job.sizePx);
  if (hit.isEmpty())
    return false;
  if (url)
    *url = hit;
  return true;
}

void ThumbnailService::request(const QString &path, qint64 mtime, int sizePx) {
  ThumbnailJob job;
  job.path = path;
  job.mtime = mtime;
  job.sizePx = sizePx;
  request(QVector<ThumbnailJob>{job});
}

void ThumbnailService::request(const QVector<ThumbnailJob> &jobs) {
  QVector<ThumbnailJob> miss;
  miss.reserve(jobs.size());
  for (ThumbnailJob job : jobs) {
    job.sizePx = ThumbCache::canonicalSize(job.sizePx);
    QString url;
    if (hydratePacked(job, &url))
      emit thumbnailReady(job.path, displayUrl(job.path, url));
    else
      miss.append(job);
  }
  if (!miss.isEmpty())
    emit submitted(miss, false);
}

void ThumbnailService::requestBackground(
    const QVector<ThumbnailJob> &jobs) {
  QVector<ThumbnailJob> normalized;
  normalized.reserve(jobs.size());
  for (ThumbnailJob job : jobs) {
    if (job.path.isEmpty() || job.mtime <= 0 || job.sizePx <= 0)
      continue;
    job.sizePx = ThumbCache::canonicalSize(job.sizePx);
    normalized.append(std::move(job));
  }
  if (!normalized.isEmpty())
    emit submitted(normalized, false);
}

void ThumbnailService::requestVisible(const QVector<ThumbnailJob> &jobs) {
  QVector<ThumbnailJob> normalized;
  normalized.reserve(jobs.size());
  for (ThumbnailJob job : jobs) {
    if (job.path.isEmpty() || job.mtime <= 0 || job.sizePx <= 0)
      continue;
    job.sizePx = ThumbCache::canonicalSize(job.sizePx);
    normalized.append(std::move(job));
  }
  // Cache probing and legacy-cache ingestion can perform SQLite and file I/O;
  // keep the entire visible-page lookup on the thumbnail thread.
  emit submitted(normalized, true);
}

void ThumbnailService::invalidate(const QString &path) {
  if (path.isEmpty())
    return;
  // Remove the packed entry before request() performs its synchronous cache
  // lookup. The engine signal remains queued so its pending/active maps are
  // cleared before the subsequently queued generation request.
  ThumbCache::instance().removePath(cachePathFor(path));
  ++m_displayRevisions[path];
  emit invalidateRequested(path);
}

QString ThumbnailService::displayUrl(const QString &path,
                                     const QString &url) const {
  if (url.isEmpty())
    return url;
  const quint64 revision = m_displayRevisions.value(path);
  return revision > 0
             ? url + QStringLiteral("/r") + QString::number(revision)
             : url;
}

void ThumbnailService::cancelAll() { emit cancelRequested(); }

void ThumbnailService::setThumbnailerDirectories(const QStringList &dirs) {
  emit thumbnailerDirectoriesChanged(dirs);
}

void ThumbnailService::setHandlerThumbnailers(
    const QVector<ExecThumbnailer> &list) {
  emit handlerThumbnailersChanged(list);
}
