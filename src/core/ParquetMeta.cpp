#include "ParquetMeta.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <cstring>

namespace {

constexpr char kMagic[] = "PAR1";
constexpr int kMaxFooter = 16 * 1024 * 1024;

enum {
  T_STOP = 0,
  T_TRUE = 1,
  T_FALSE = 2,
  T_I8 = 3,
  T_I16 = 4,
  T_I32 = 5,
  T_I64 = 6,
  T_DOUBLE = 7,
  T_BINARY = 8,
  T_LIST = 9,
  T_SET = 10,
  T_MAP = 11,
  T_STRUCT = 12
};

const char *physicalTypeName(int t) {
  switch (t) {
  case 0:
    return "BOOLEAN";
  case 1:
    return "INT32";
  case 2:
    return "INT64";
  case 3:
    return "INT96";
  case 4:
    return "FLOAT";
  case 5:
    return "DOUBLE";
  case 6:
    return "BYTE_ARRAY";
  case 7:
    return "FIXED_LEN_BYTE_ARRAY";
  default:
    return "UNKNOWN";
  }
}

const char *convertedTypeName(int t) {
  switch (t) {
  case 0:
    return "UTF8";
  case 1:
    return "MAP";
  case 2:
    return "MAP_KEY_VALUE";
  case 3:
    return "LIST";
  case 4:
    return "ENUM";
  case 5:
    return "DECIMAL";
  case 6:
    return "DATE";
  case 7:
    return "TIME_MILLIS";
  case 8:
    return "TIME_MICROS";
  case 9:
    return "TIMESTAMP_MILLIS";
  case 10:
    return "TIMESTAMP_MICROS";
  case 11:
    return "UINT_8";
  case 12:
    return "UINT_16";
  case 13:
    return "UINT_32";
  case 14:
    return "UINT_64";
  case 15:
    return "INT_8";
  case 16:
    return "INT_16";
  case 17:
    return "INT_32";
  case 18:
    return "INT_64";
  case 19:
    return "JSON";
  case 20:
    return "BSON";
  case 21:
    return "INTERVAL";
  default:
    return nullptr;
  }
}

const char *repetitionName(int t) {
  switch (t) {
  case 0:
    return "required";
  case 1:
    return "optional";
  case 2:
    return "repeated";
  default:
    return "";
  }
}

const char *codecName(int t) {
  switch (t) {
  case 0:
    return "uncompressed";
  case 1:
    return "snappy";
  case 2:
    return "gzip";
  case 3:
    return "lzo";
  case 4:
    return "brotli";
  case 5:
    return "lz4";
  case 6:
    return "zstd";
  case 7:
    return "lz4_raw";
  default:
    return "";
  }
}

struct Cursor {
  const unsigned char *p = nullptr;
  const unsigned char *end = nullptr;
  bool ok = true;

  bool need(int n) const { return ok && p && end && (end - p) >= n; }

  unsigned char u8() {
    if (!need(1)) {
      ok = false;
      return 0;
    }
    return *p++;
  }

  quint64 varint() {
    quint64 v = 0;
    int shift = 0;
    while (ok) {
      const unsigned char b = u8();
      v |= quint64(b & 0x7f) << shift;
      if ((b & 0x80) == 0)
        break;
      shift += 7;
      if (shift > 63) {
        ok = false;
        return 0;
      }
    }
    return v;
  }

  qint64 zigzag() {
    const quint64 n = varint();
    return qint64((n >> 1) ^ (~qint64(n & 1) + 1));
  }

  QByteArray binary() {
    const quint64 n = varint();
    if (!ok || n > quint64(end - p)) {
      ok = false;
      return {};
    }
    QByteArray out(reinterpret_cast<const char *>(p), int(n));
    p += n;
    return out;
  }

  bool skip(int type);
  bool readField(int *id, int *type, int *lastId);
};

bool Cursor::skip(int type) {
  switch (type) {
  case T_TRUE:
  case T_FALSE:
    return ok;
  case T_I8:
    u8();
    return ok;
  case T_I16:
  case T_I32:
  case T_I64:
    zigzag();
    return ok;
  case T_DOUBLE:
    if (!need(8)) {
      ok = false;
      return false;
    }
    p += 8;
    return ok;
  case T_BINARY:
    binary();
    return ok;
  case T_LIST:
  case T_SET: {
    const unsigned char h = u8();
    int elem = h & 0x0f;
    quint64 n = h >> 4;
    if (n == 15)
      n = varint();
    for (quint64 i = 0; ok && i < n; ++i)
      skip(elem);
    return ok;
  }
  case T_MAP: {
    const unsigned char h = u8();
    const int keyT = (h >> 4) & 0x0f;
    const int valT = h & 0x0f;
    const quint64 n = varint();
    for (quint64 i = 0; ok && i < n; ++i) {
      skip(keyT);
      skip(valT);
    }
    return ok;
  }
  case T_STRUCT: {
    int last = 0;
    for (;;) {
      int id = 0, t = 0;
      if (!readField(&id, &t, &last))
        return ok;
      if (t == T_STOP)
        return ok;
      skip(t);
    }
  }
  default:
    ok = false;
    return false;
  }
}

bool Cursor::readField(int *id, int *type, int *lastId) {
  const unsigned char b = u8();
  if (!ok)
    return false;
  if (b == 0) {
    *type = T_STOP;
    *id = 0;
    return true;
  }
  *type = b & 0x0f;
  const int delta = (b >> 4) & 0x0f;
  if (delta == 0)
    *lastId = int(zigzag());
  else
    *lastId += delta;
  *id = *lastId;
  return ok;
}

struct SchemaEl {
  int type = -1;
  int typeLength = 0;
  int repetition = -1;
  QString name;
  int numChildren = 0;
  int converted = -1;
};

bool parseSchemaEl(Cursor &c, SchemaEl *el) {
  int last = 0;
  for (;;) {
    int id = 0, t = 0;
    if (!c.readField(&id, &t, &last))
      return false;
    if (t == T_STOP)
      return true;
    if (id == 1 && t == T_I32)
      el->type = int(c.zigzag());
    else if (id == 2 && t == T_I32)
      el->typeLength = int(c.zigzag());
    else if (id == 3 && t == T_I32)
      el->repetition = int(c.zigzag());
    else if (id == 4 && t == T_BINARY)
      el->name = QString::fromUtf8(c.binary());
    else if (id == 5 && t == T_I32)
      el->numChildren = int(c.zigzag());
    else if (id == 6 && t == T_I32)
      el->converted = int(c.zigzag());
    else
      c.skip(t);
    if (!c.ok)
      return false;
  }
}

int parseFirstCodec(Cursor &c) {
  // Walk FileMetaData.row_groups[0].columns[0].meta_data.codec
  // We only call this on a row group struct... actually parse at FileMetaData
  // level. This helper parses one RowGroup and returns codec or -1.
  int last = 0;
  int codec = -1;
  for (;;) {
    int id = 0, t = 0;
    if (!c.readField(&id, &t, &last))
      return codec;
    if (t == T_STOP)
      return codec;
    if (id == 1 && t == T_LIST) {
      const unsigned char h = c.u8();
      const int elem = h & 0x0f;
      quint64 n = h >> 4;
      if (n == 15)
        n = c.varint();
      if (elem == T_STRUCT && n > 0 && codec < 0) {
        int clast = 0;
        for (;;) {
          int cid = 0, ct = 0;
          if (!c.readField(&cid, &ct, &clast))
            break;
          if (ct == T_STOP)
            break;
          if (cid == 2 && ct == T_STRUCT) {
            int mlast = 0;
            for (;;) {
              int mid = 0, mt = 0;
              if (!c.readField(&mid, &mt, &mlast))
                break;
              if (mt == T_STOP)
                break;
              if (mid == 4 && mt == T_I32)
                codec = int(c.zigzag());
              else
                c.skip(mt);
            }
          } else {
            c.skip(ct);
          }
        }
        for (quint64 i = 1; i < n; ++i)
          c.skip(T_STRUCT);
      } else {
        for (quint64 i = 0; i < n; ++i)
          c.skip(elem);
      }
    } else {
      c.skip(t);
    }
  }
}

QString sqlQuote(const QString &path) {
  QString p = QFileInfo(path).absoluteFilePath();
  p.replace(QLatin1Char('\''), QStringLiteral("''"));
  return p;
}

QVariant jsonToVariant(const QJsonValue &v) {
  if (v.isNull() || v.isUndefined())
    return QVariant();
  if (v.isBool())
    return v.toBool();
  if (v.isDouble())
    return v.toDouble();
  if (v.isString())
    return v.toString();
  if (v.isArray())
    return QString::fromUtf8(
        QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
  if (v.isObject())
    return QString::fromUtf8(
        QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
  return v.toVariant();
}

} // namespace

bool ParquetMeta::looksLike(const QString &path, const QString &mimeHint) {
  const QString mime = mimeHint.toLower();
  if (mime == QLatin1String("application/vnd.apache.parquet") ||
      mime == QLatin1String("application/x-parquet") ||
      mime == QLatin1String("application/parquet"))
    return true;
  if (path.endsWith(QLatin1String(".parquet"), Qt::CaseInsensitive) ||
      path.endsWith(QLatin1String(".pq"), Qt::CaseInsensitive))
    return true;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  const QByteArray mag = f.read(4);
  return mag == QByteArray::fromRawData(kMagic, 4);
}

ParquetInfo ParquetMeta::parse(const QString &path) {
  ParquetInfo info;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    info.error = QStringLiteral("unreadable");
    return info;
  }
  const qint64 size = f.size();
  if (size < 12) {
    info.error = QStringLiteral("too small");
    return info;
  }
  if (!f.seek(0)) {
    info.error = QStringLiteral("seek");
    return info;
  }
  const QByteArray head = f.read(4);
  if (head != QByteArray::fromRawData(kMagic, 4)) {
    info.error = QStringLiteral("not parquet");
    return info;
  }
  if (!f.seek(size - 8)) {
    info.error = QStringLiteral("seek");
    return info;
  }
  const QByteArray tail = f.read(8);
  if (tail.size() != 8 ||
      tail.mid(4) != QByteArray::fromRawData(kMagic, 4)) {
    info.error = QStringLiteral("not parquet");
    return info;
  }
  const quint32 footerLen = quint32(uchar(tail[0])) |
                            (quint32(uchar(tail[1])) << 8) |
                            (quint32(uchar(tail[2])) << 16) |
                            (quint32(uchar(tail[3])) << 24);
  if (footerLen == 0 || footerLen > quint32(kMaxFooter) ||
      qint64(footerLen) + 8 > size) {
    info.error = QStringLiteral("bad footer");
    return info;
  }
  if (!f.seek(size - 8 - qint64(footerLen))) {
    info.error = QStringLiteral("seek");
    return info;
  }
  const QByteArray footer = f.read(footerLen);
  if (footer.size() != int(footerLen)) {
    info.error = QStringLiteral("short footer");
    return info;
  }

  Cursor c;
  c.p = reinterpret_cast<const unsigned char *>(footer.constData());
  c.end = c.p + footer.size();
  int last = 0;
  QVector<SchemaEl> schema;
  int codec = -1;
  for (;;) {
    int id = 0, t = 0;
    if (!c.readField(&id, &t, &last))
      break;
    if (t == T_STOP)
      break;
    if (id == 3 && t == T_I64) {
      info.numRows = c.zigzag();
    } else if (id == 6 && t == T_BINARY) {
      info.createdBy = QString::fromUtf8(c.binary());
    } else if (id == 2 && t == T_LIST) {
      const unsigned char h = c.u8();
      const int elem = h & 0x0f;
      quint64 n = h >> 4;
      if (n == 15)
        n = c.varint();
      if (elem != T_STRUCT) {
        for (quint64 i = 0; i < n; ++i)
          c.skip(elem);
      } else {
        schema.reserve(int(qMin(n, quint64(4096))));
        for (quint64 i = 0; c.ok && i < n; ++i) {
          SchemaEl el;
          if (!parseSchemaEl(c, &el))
            break;
          schema.append(el);
        }
      }
    } else if (id == 4 && t == T_LIST) {
      const unsigned char h = c.u8();
      const int elem = h & 0x0f;
      quint64 n = h >> 4;
      if (n == 15)
        n = c.varint();
      info.rowGroups = int(n);
      if (elem == T_STRUCT && n > 0) {
        codec = parseFirstCodec(c);
        for (quint64 i = 1; c.ok && i < n; ++i)
          c.skip(T_STRUCT);
      } else {
        for (quint64 i = 0; i < n; ++i)
          c.skip(elem);
      }
    } else {
      c.skip(t);
    }
    if (!c.ok)
      break;
  }
  if (!c.ok && schema.isEmpty()) {
    info.error = QStringLiteral("bad metadata");
    return info;
  }

  for (int i = 0; i < schema.size(); ++i) {
    const SchemaEl &el = schema.at(i);
    if (i == 0 && el.numChildren > 0 && el.type < 0)
      continue;
    if (el.numChildren > 0)
      continue;
    ParquetColumn col;
    col.name = el.name;
    if (el.converted >= 0) {
      if (const char *cv = convertedTypeName(el.converted))
        col.type = QString::fromLatin1(cv);
    }
    if (col.type.isEmpty() && el.type >= 0) {
      col.type = QString::fromLatin1(physicalTypeName(el.type));
      if (el.type == 7 && el.typeLength > 0)
        col.type += QStringLiteral("[%1]").arg(el.typeLength);
    }
    if (el.repetition >= 0)
      col.repetition = QString::fromLatin1(repetitionName(el.repetition));
    if (!col.name.isEmpty())
      info.columns.append(col);
  }
  if (codec >= 0)
    info.codec = QString::fromLatin1(codecName(codec));
  info.ok = true;
  return info;
}

ParquetInfo ParquetMeta::parseWithSample(const QString &path, int maxRows) {
  ParquetInfo info = parse(path);
  if (!info.ok)
    return info;
  const QString bin =
      QStandardPaths::findExecutable(QStringLiteral("duckdb"));
  if (bin.isEmpty()) {
    info.sampleNote = QStringLiteral("install duckdb for row samples");
    return info;
  }
  const int n = qBound(1, maxRows, 50);
  QProcess proc;
  proc.setProcessChannelMode(QProcess::SeparateChannels);
  // VARCHAR so snowflake ids and other INT64s are not rounded in JSON.
  proc.start(bin, {QStringLiteral("-json"), QStringLiteral("-c"),
                   QStringLiteral(
                       "SELECT COLUMNS(*)::VARCHAR FROM read_parquet('%1') "
                       "LIMIT %2")
                       .arg(sqlQuote(path))
                       .arg(n)});
  if (!proc.waitForFinished(5000) ||
      proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    if (proc.state() != QProcess::NotRunning)
      proc.kill();
    info.sampleNote = QStringLiteral("duckdb could not sample this file");
    return info;
  }
  const QByteArray raw = proc.readAllStandardOutput();
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
  if (!doc.isArray()) {
    info.sampleNote = QStringLiteral("duckdb sample was empty");
    return info;
  }
  const QJsonArray arr = doc.array();
  for (const QJsonValue &row : arr) {
    if (!row.isObject())
      continue;
    QVariantMap rec;
    const QJsonObject obj = row.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it)
      rec.insert(it.key(), jsonToVariant(it.value()));
    info.sample.append(rec);
  }
  return info;
}

QVariantMap ParquetMeta::toMap(const ParquetInfo &info) {
  QVariantMap out;
  out.insert(QStringLiteral("ok"), info.ok);
  out.insert(QStringLiteral("error"), info.error);
  out.insert(QStringLiteral("numRows"), info.numRows);
  out.insert(QStringLiteral("rowGroups"), info.rowGroups);
  out.insert(QStringLiteral("createdBy"), info.createdBy);
  out.insert(QStringLiteral("codec"), info.codec);
  out.insert(QStringLiteral("sampleNote"), info.sampleNote);
  QVariantList cols;
  QStringList names;
  for (const ParquetColumn &c : info.columns) {
    QVariantMap m;
    m.insert(QStringLiteral("name"), c.name);
    m.insert(QStringLiteral("type"), c.type);
    m.insert(QStringLiteral("repetition"), c.repetition);
    cols.append(m);
    names.append(c.name);
  }
  out.insert(QStringLiteral("columns"), cols);
  out.insert(QStringLiteral("columnNames"), names);
  out.insert(QStringLiteral("sample"), info.sample);
  return out;
}
