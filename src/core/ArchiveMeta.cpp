#include "ArchiveMeta.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#ifdef SYNCHRO_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {

constexpr int kMaxName = 4096;
constexpr qint64 kMaxGzipInflate = 2 * 1024 * 1024;

quint16 ru16(const char *p) {
  return quint16(uchar(p[0])) | (quint16(uchar(p[1])) << 8);
}

quint32 ru32(const char *p) {
  return quint32(uchar(p[0])) | (quint32(uchar(p[1])) << 8) |
         (quint32(uchar(p[2])) << 16) | (quint32(uchar(p[3])) << 24);
}

bool readExact(QFile &f, char *buf, int n) {
  return f.read(buf, n) == n;
}

QString decodeZipName(const QByteArray &raw, quint16 flags) {
  if (raw.isEmpty())
    return {};
  if (flags & 0x800)
    return QString::fromUtf8(raw);
  const QString utf = QString::fromUtf8(raw);
  if (!utf.contains(QChar::ReplacementCharacter))
    return utf;
  return QString::fromLatin1(raw);
}

QString zipMethodName(quint16 method) {
  switch (method) {
  case 0:
    return QStringLiteral("store");
  case 8:
    return QStringLiteral("deflate");
  case 9:
    return QStringLiteral("deflate64");
  case 12:
    return QStringLiteral("bzip2");
  case 14:
    return QStringLiteral("lzma");
  case 93:
    return QStringLiteral("zstd");
  case 95:
    return QStringLiteral("xz");
  default:
    return QStringLiteral("method %1").arg(method);
  }
}

QString fmtTime(qint64 epoch) {
  if (epoch <= 0)
    return {};
  return QDateTime::fromSecsSinceEpoch(epoch).toString(Qt::ISODate);
}

qint64 parseOctal(const char *p, int n) {
  if (n <= 0)
    return 0;
  if (uchar(p[0]) & 0x80) {
    qint64 v = 0;
    for (int i = 1; i < n; ++i)
      v = (v << 8) | uchar(p[i]);
    return v;
  }
  qint64 v = 0;
  bool any = false;
  for (int i = 0; i < n; ++i) {
    const unsigned char c = uchar(p[i]);
    if (c == 0 || c == ' ') {
      if (any)
        break;
      continue;
    }
    if (c < '0' || c > '7')
      break;
    any = true;
    v = v * 8 + (c - '0');
  }
  return v;
}

bool suffixLooksArchive(const QString &path) {
  static const char *const kSuf[] = {".zip",    ".zipx",   ".gz",     ".gzip",
                                     ".tar",    ".tgz",    ".taz",    ".tar.gz",
                                     ".tar.gzip"};
  for (const char *s : kSuf) {
    if (path.endsWith(QLatin1String(s), Qt::CaseInsensitive))
      return true;
  }
  return false;
}

bool mimeLooksArchive(const QString &mime) {
  static const char *const kMime[] = {
      "application/zip",
      "application/x-zip-compressed",
      "application/gzip",
      "application/x-gzip",
      "application/x-tar",
      "application/x-gtar",
      "application/x-compressed-tar",
      "application/x-gtar-compressed"};
  for (const char *m : kMime) {
    if (mime == QLatin1String(m))
      return true;
  }
  return false;
}

enum class Kind { Unknown, Zip, Gzip, Tar };

Kind sniff(QFile &f) {
  if (!f.seek(0))
    return Kind::Unknown;
  const QByteArray h = f.read(8);
  if (h.size() >= 4 && h[0] == 'P' && h[1] == 'K' &&
      (h[2] == 3 || h[2] == 5 || h[2] == 7))
    return Kind::Zip;
  if (h.size() >= 2 && uchar(h[0]) == 0x1f && uchar(h[1]) == 0x8b)
    return Kind::Gzip;
  if (!f.seek(257))
    return Kind::Unknown;
  const QByteArray magic = f.read(5);
  if (magic == "ustar")
    return Kind::Tar;
  return Kind::Unknown;
}

Kind detect(const QString &path, QFile &f) {
  const Kind s = sniff(f);
  if (s != Kind::Unknown)
    return s;
  if (path.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) ||
      path.endsWith(QLatin1String(".zipx"), Qt::CaseInsensitive))
    return Kind::Zip;
  if (path.endsWith(QLatin1String(".tar"), Qt::CaseInsensitive))
    return Kind::Tar;
  if (path.endsWith(QLatin1String(".gz"), Qt::CaseInsensitive) ||
      path.endsWith(QLatin1String(".tgz"), Qt::CaseInsensitive) ||
      path.endsWith(QLatin1String(".taz"), Qt::CaseInsensitive) ||
      path.endsWith(QLatin1String(".gzip"), Qt::CaseInsensitive))
    return Kind::Gzip;
  return Kind::Unknown;
}

void addEntry(ArchiveInfo &info, const ArchiveEntry &e, int maxEntries) {
  if (e.kind == QLatin1String("dir"))
    ++info.dirs;
  else
    ++info.files;
  if (e.size > 0)
    info.uncompressed += e.size;
  if (e.packed > 0)
    info.packed += e.packed;
  if (int(info.entries.size()) < maxEntries)
    info.entries.append(e);
  else
    info.truncated = true;
}

ArchiveInfo fail(const QString &error) {
  ArchiveInfo info;
  info.error = error;
  return info;
}

ArchiveInfo inspectZip(QFile &f, int maxEntries) {
  ArchiveInfo info;
  info.format = QStringLiteral("zip");
  info.fileSize = f.size();
  if (info.fileSize < 22)
    return fail(QStringLiteral("not a zip file"));

  const qint64 scan = qMin(info.fileSize, qint64(64 * 1024 + 22));
  if (!f.seek(info.fileSize - scan))
    return fail(QStringLiteral("unreadable"));
  const QByteArray tail = f.read(scan);
  int eocd = -1;
  for (int i = tail.size() - 22; i >= 0; --i) {
    if (tail[i] == 'P' && tail[i + 1] == 'K' && uchar(tail[i + 2]) == 5 &&
        uchar(tail[i + 3]) == 6) {
      const quint16 comment = ru16(tail.constData() + i + 20);
      if (i + 22 + int(comment) <= tail.size()) {
        eocd = i;
        break;
      }
    }
  }
  if (eocd < 0)
    return fail(QStringLiteral("not a zip file"));

  const char *e = tail.constData() + eocd;
  quint64 nEntries = ru16(e + 10);
  quint64 cdSize = ru32(e + 12);
  quint64 cdOff = ru32(e + 16);
  if (nEntries == 0xffff || cdOff == 0xffffffffu) {
    info.sampleNote = QStringLiteral("zip64 · listing first members");
    if (nEntries == 0xffff)
      nEntries = quint64(maxEntries);
  }

  if (!f.seek(qint64(cdOff)))
    return fail(QStringLiteral("zip directory unreadable"));
  QByteArray cd = f.read(qint64(qMin(cdSize, quint64(8 * 1024 * 1024))));
  int pos = 0;
  int seen = 0;
  while (pos + 46 <= cd.size() && seen < int(nEntries)) {
    const char *h = cd.constData() + pos;
    if (h[0] != 'P' || h[1] != 'K' || uchar(h[2]) != 1 || uchar(h[3]) != 2)
      break;
    const quint16 flags = ru16(h + 8);
    const quint16 method = ru16(h + 10);
    const quint32 packed = ru32(h + 20);
    const quint32 size = ru32(h + 24);
    const quint16 nameLen = ru16(h + 28);
    const quint16 extraLen = ru16(h + 30);
    const quint16 commentLen = ru16(h + 32);
    const int rec = 46 + nameLen + extraLen + commentLen;
    if (pos + rec > cd.size())
      break;
    QByteArray rawName = cd.mid(pos + 46, qMin(int(nameLen), kMaxName));
    ArchiveEntry ent;
    ent.name = decodeZipName(rawName, flags);
    ent.size = size == 0xffffffffu ? -1 : qint64(size);
    ent.packed = packed == 0xffffffffu ? -1 : qint64(packed);
    if (ent.name.endsWith(QLatin1Char('/')))
      ent.kind = QStringLiteral("dir");
    else
      ent.kind = QStringLiteral("file");
    if (info.method.isEmpty())
      info.method = zipMethodName(method);
    addEntry(info, ent, maxEntries);
    pos += rec;
    ++seen;
  }
  if (seen < int(nEntries) && nEntries != 0xffff)
    info.truncated = true;
  info.ok = true;
  if (info.packed <= 0)
    info.packed = info.fileSize;
  return info;
}

ArchiveInfo inspectGzipHeader(QFile &f) {
  ArchiveInfo info;
  info.format = QStringLiteral("gzip");
  info.fileSize = f.size();
  info.method = QStringLiteral("deflate");
  if (!f.seek(0))
    return fail(QStringLiteral("unreadable"));
  char hdr[10];
  if (!readExact(f, hdr, 10))
    return fail(QStringLiteral("not gzip"));
  if (uchar(hdr[0]) != 0x1f || uchar(hdr[1]) != 0x8b)
    return fail(QStringLiteral("not gzip"));
  const quint8 flags = uchar(hdr[3]);
  const quint32 mtime = ru32(hdr + 4);
  info.mtime = fmtTime(mtime);
  if (flags & 4) {
    char xlen[2];
    if (!readExact(f, xlen, 2))
      return fail(QStringLiteral("truncated gzip"));
    const quint16 n = ru16(xlen);
    if (!f.seek(f.pos() + n))
      return fail(QStringLiteral("truncated gzip"));
  }
  if (flags & 8) {
    QByteArray name;
    char c = 0;
    while (f.getChar(&c) && c != 0 && name.size() < kMaxName)
      name.append(c);
    info.origName = QString::fromUtf8(name);
  }
  if (flags & 16) {
    char c = 0;
    int n = 0;
    while (f.getChar(&c) && c != 0 && n++ < 4096) {
    }
  }
  if (flags & 2) {
    if (!f.seek(f.pos() + 2))
      return fail(QStringLiteral("truncated gzip"));
  }
  if (info.fileSize >= 8 && f.seek(info.fileSize - 4)) {
    char isize[4];
    if (readExact(f, isize, 4))
      info.uncompressed = ru32(isize);
  }
  info.packed = info.fileSize;
  info.ok = true;
  return info;
}

bool tarHeaderOk(const char *h) {
  bool zero = true;
  for (int i = 0; i < 512; ++i) {
    if (h[i] != 0) {
      zero = false;
      break;
    }
  }
  if (zero)
    return false;
  const QByteArray magic = QByteArray::fromRawData(h + 257, 5);
  if (magic == "ustar")
    return true;
  // pre-POSIX tar: checksum field is octal
  return h[156] == '0' || h[156] == 0 || h[156] == '5' || h[156] == '1' ||
         h[156] == '2';
}

QString tarKind(char type) {
  switch (type) {
  case '5':
    return QStringLiteral("dir");
  case '1':
  case '2':
    return QStringLiteral("link");
  default:
    return QStringLiteral("file");
  }
}

template <typename ReadFn, typename SkipFn>
bool walkTar(ArchiveInfo &info, int maxEntries, ReadFn read512, SkipFn skip) {
  QByteArray pendingLong;
  int empty = 0;
  for (int i = 0; i < 20000; ++i) {
    char blk[512];
    if (!read512(blk))
      break;
    bool zero = true;
    for (int b = 0; b < 512; ++b) {
      if (blk[b] != 0) {
        zero = false;
        break;
      }
    }
    if (zero) {
      if (++empty >= 2)
        break;
      continue;
    }
    empty = 0;
    if (!tarHeaderOk(blk))
      return false;
    const char type = blk[156];
    const qint64 size = parseOctal(blk + 124, 12);
    const qint64 skipTo = (size + 511) & ~qint64(511);
    if (type == 'L') {
      pendingLong.clear();
      qint64 left = size;
      qint64 remain = skipTo;
      while (remain > 0) {
        char pay[512];
        if (!read512(pay))
          return false;
        remain -= 512;
        if (left > 0) {
          const int take = int(qMin(left, qint64(512)));
          const int room = kMaxName - pendingLong.size();
          if (room > 0)
            pendingLong.append(pay, qMin(take, room));
          left -= take;
        }
      }
      continue;
    }
    if (type == 'K') {
      if (!skip(skipTo))
        return false;
      continue;
    }
    if (type == 'g' || type == 'x' || type == 'X') {
      if (!skip(skipTo))
        return false;
      continue;
    }
    ArchiveEntry ent;
    if (!pendingLong.isEmpty()) {
      ent.name = QString::fromUtf8(pendingLong);
      pendingLong.clear();
    } else {
      QByteArray prefix;
      auto nlen = [](const char *p, int max) {
        int i = 0;
        while (i < max && p[i])
          ++i;
        return i;
      };
      if (QByteArray::fromRawData(blk + 257, 5) == "ustar" && blk[345] != 0)
        prefix = QByteArray(blk + 345, nlen(blk + 345, 155));
      QByteArray base(blk, nlen(blk, 100));
      if (!prefix.isEmpty())
        ent.name = QString::fromUtf8(prefix + '/' + base);
      else
        ent.name = QString::fromUtf8(base);
    }
    ent.size = size;
    ent.kind = tarKind(type);
    if (ent.kind == QLatin1String("file") && ent.name.endsWith(QLatin1Char('/')))
      ent.kind = QStringLiteral("dir");
    addEntry(info, ent, maxEntries);
    if (!skip(skipTo))
      return false;
    if (info.truncated && int(info.entries.size()) >= maxEntries)
      break;
  }
  return true;
}

ArchiveInfo inspectTar(QFile &f, int maxEntries) {
  ArchiveInfo info;
  info.format = QStringLiteral("tar");
  info.fileSize = f.size();
  if (!f.seek(0))
    return fail(QStringLiteral("unreadable"));
  const bool ok = walkTar(
      info, maxEntries,
      [&](char *blk) { return readExact(f, blk, 512); },
      [&](qint64 n) {
        if (n <= 0)
          return true;
        return f.seek(f.pos() + n);
      });
  if (!ok && info.files == 0 && info.dirs == 0)
    return fail(QStringLiteral("not a tar file"));
  info.ok = true;
  info.packed = info.fileSize;
  return info;
}

#ifdef SYNCHRO_HAVE_ZLIB
struct GzipReader {
  QFile *file = nullptr;
  z_stream zs{};
  QByteArray in;
  bool started = false;
  bool ended = false;
  qint64 inflated = 0;

  ~GzipReader() {
    if (started)
      inflateEnd(&zs);
  }

  bool start(QFile *f) {
    file = f;
    if (!file->seek(0))
      return false;
    zs.zalloc = Z_NULL;
    zs.zfree = Z_NULL;
    zs.opaque = Z_NULL;
    zs.avail_in = 0;
    zs.next_in = Z_NULL;
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK)
      return false;
    started = true;
    in.resize(16 * 1024);
    return true;
  }

  int pull(char *out, int want) {
    if (!started || ended || want <= 0)
      return 0;
    int got = 0;
    while (got < want) {
      if (zs.avail_in == 0 && file) {
        const qint64 n = file->read(in.data(), in.size());
        if (n <= 0)
          break;
        zs.next_in = reinterpret_cast<Bytef *>(in.data());
        zs.avail_in = uInt(n);
      }
      zs.next_out = reinterpret_cast<Bytef *>(out + got);
      zs.avail_out = uInt(want - got);
      const int rc = inflate(&zs, Z_NO_FLUSH);
      const int chunk = (want - got) - int(zs.avail_out);
      got += chunk;
      inflated += chunk;
      if (rc == Z_STREAM_END) {
        ended = true;
        break;
      }
      if (rc != Z_OK)
        return -1;
      if (chunk == 0 && zs.avail_in == 0)
        break;
    }
    return got;
  }

  bool read(char *out, int n) { return pull(out, n) == n; }

  bool skip(qint64 n) {
    char buf[4096];
    while (n > 0) {
      if (inflated >= kMaxGzipInflate)
        return false;
      const int want = int(qMin(n, qint64(sizeof(buf))));
      const int got = pull(buf, want);
      if (got <= 0)
        return false;
      n -= got;
    }
    return true;
  }
};

ArchiveInfo inspectTarGz(QFile &f, int maxEntries, ArchiveInfo gzip) {
  GzipReader gr;
  if (!gr.start(&f)) {
    gzip.sampleNote = QStringLiteral("could not inflate gzip");
    return gzip;
  }
  gzip.format = QStringLiteral("tar.gz");
  const bool ok = walkTar(
      gzip, maxEntries, [&](char *blk) { return gr.read(blk, 512); },
      [&](qint64 n) { return gr.skip(n); });
  if (!ok && gzip.files == 0 && gzip.dirs == 0) {
    if (gr.inflated >= kMaxGzipInflate)
      gzip.sampleNote = QStringLiteral("gzip · members not listed (large stream)");
    else
      gzip.sampleNote = QStringLiteral("gzip · not a tar payload");
    gzip.format = QStringLiteral("gzip");
    gzip.ok = true;
    return gzip;
  }
  if (gr.inflated >= kMaxGzipInflate)
    gzip.truncated = true;
  gzip.ok = true;
  return gzip;
}
#endif

} // namespace

bool ArchiveMeta::looksLike(const QString &path, const QString &mimeHint) {
  if (mimeLooksArchive(mimeHint) || suffixLooksArchive(path))
    return true;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  return sniff(f) != Kind::Unknown;
}

ArchiveInfo ArchiveMeta::inspect(const QString &path, int maxEntries) {
  if (maxEntries <= 0)
    maxEntries = 200;
  if (maxEntries > 500)
    maxEntries = 500;
  QFile f(path);
  ArchiveInfo info;
  info.name = QFileInfo(path).fileName();
  if (!f.open(QIODevice::ReadOnly))
    return fail(QStringLiteral("unreadable"));
  const Kind kind = detect(path, f);
  switch (kind) {
  case Kind::Zip:
    info = inspectZip(f, maxEntries);
    break;
  case Kind::Tar:
    info = inspectTar(f, maxEntries);
    break;
  case Kind::Gzip: {
    ArchiveInfo gz = inspectGzipHeader(f);
    if (!gz.ok)
      return gz;
    const bool wantTar =
        path.endsWith(QLatin1String(".tgz"), Qt::CaseInsensitive) ||
        path.endsWith(QLatin1String(".taz"), Qt::CaseInsensitive) ||
        path.endsWith(QLatin1String(".tar.gz"), Qt::CaseInsensitive) ||
        path.endsWith(QLatin1String(".tar.gzip"), Qt::CaseInsensitive) ||
        gz.origName.endsWith(QLatin1String(".tar"), Qt::CaseInsensitive);
#ifdef SYNCHRO_HAVE_ZLIB
    info = wantTar ? inspectTarGz(f, maxEntries, gz) : gz;
#else
    Q_UNUSED(wantTar);
    info = gz;
    if (wantTar)
      info.sampleNote = QStringLiteral("rebuild with zlib to list tar.gz members");
#endif
    break;
  }
  case Kind::Unknown:
    return fail(QStringLiteral("not an archive"));
  }
  info.name = QFileInfo(path).fileName();
  info.fileSize = f.size();
  return info;
}

QVariantMap ArchiveMeta::toMap(const ArchiveInfo &info) {
  QVariantMap out;
  out.insert(QStringLiteral("ok"), info.ok);
  out.insert(QStringLiteral("error"), info.error);
  out.insert(QStringLiteral("format"), info.format);
  out.insert(QStringLiteral("name"), info.name);
  out.insert(QStringLiteral("fileSize"), info.fileSize);
  out.insert(QStringLiteral("uncompressed"), info.uncompressed);
  out.insert(QStringLiteral("packed"), info.packed);
  out.insert(QStringLiteral("files"), info.files);
  out.insert(QStringLiteral("dirs"), info.dirs);
  out.insert(QStringLiteral("truncated"), info.truncated);
  out.insert(QStringLiteral("origName"), info.origName);
  out.insert(QStringLiteral("mtime"), info.mtime);
  out.insert(QStringLiteral("method"), info.method);
  out.insert(QStringLiteral("sampleNote"), info.sampleNote);
  QVariantList rows;
  rows.reserve(info.entries.size());
  for (const ArchiveEntry &e : info.entries) {
    QVariantMap row;
    row.insert(QStringLiteral("name"), e.name);
    row.insert(QStringLiteral("size"), e.size);
    row.insert(QStringLiteral("packed"), e.packed);
    row.insert(QStringLiteral("kind"), e.kind);
    rows.append(row);
  }
  out.insert(QStringLiteral("entries"), rows);
  return out;
}
