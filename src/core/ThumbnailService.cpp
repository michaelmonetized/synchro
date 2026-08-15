#include "ThumbnailService.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMimeDatabase>
#include <QMimeType>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <functional>
#include <limits>
#include <utility>

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
  return path.startsWith(cache + QStringLiteral("/thumbnails/")) ||
         path.startsWith(cache + QStringLiteral("/synchro/thumbs/"));
}

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

const Thumbnailer *matchThumbnailer(const QVector<Thumbnailer> &list,
                                    const QString &path,
                                    const QString &mimeHint,
                                    QMimeDatabase &db) {
  QStringList candidates;
  if (!mimeHint.isEmpty())
    candidates.append(mimeHint);
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

  void setThumbnailerDirectories(const QStringList &dirs) {
    m_thumbnailerDirs = dirs;
    m_thumbnailers.clear();
    m_loaded = false;
  }

  void cancelAll() {
    m_pending.clear();
    while (!m_active.isEmpty())
      killActive(0, false);
  }

  void submit(const QVector<ThumbnailJob> &jobs, bool exclusive) {
    QSet<QString> keep;
    keep.reserve(jobs.size());
    for (const ThumbnailJob &in : jobs) {
      if (in.path.isEmpty() || in.mtime <= 0 || in.sizePx <= 0)
        continue;
      if (isThumbCachePath(in.path))
        continue;
      Job job;
      job.key = jobKey(in.path, in.mtime, in.sizePx);
      job.path = in.path;
      job.mime = in.mime;
      job.mtime = in.mtime;
      job.sizePx = in.sizePx;
      job.priority = in.priority;
      keep.insert(job.key);

      if (const auto it = m_ready.constFind(job.key); it != m_ready.cend()) {
        if (notify)
          notify(job.path, it.value());
        continue;
      }
      if (m_failed.contains(job.key)) {
        if (notify)
          notify(job.path, QString());
        continue;
      }
      if (isActive(job.key)) {
        continue;
      }
      const QString hit = lookupCache(job);
      if (!hit.isEmpty()) {
        m_ready.insert(job.key, hit);
        if (notify)
          notify(job.path, hit);
        continue;
      }
      if (m_pending.contains(job.key))
        m_pending[job.key].priority = job.priority;
      else
        m_pending.insert(job.key, job);
    }

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

    debugLog("thumb queue pending=%d active=%d", m_pending.size(),
             m_active.size());
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
  };

  struct Active {
    QString key;
    QString path;
    QString dest;
    QString temp;
    QProcess *proc = nullptr;
  };

  bool isActive(const QString &key) const {
    for (const Active &a : m_active) {
      if (a.key == key)
        return true;
    }
    return false;
  }

  QString lookupCache(const Job &job) const {
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
      if (ThumbnailService::isValidXdgThumbnail(png, uri, mtimeSec))
        return ThumbnailService::fileUrl(png);
    }
    const QString syn =
        ThumbnailService::synchroThumbPath(job.path, job.mtime, job.sizePx);
    if (QFileInfo::exists(syn))
      return ThumbnailService::fileUrl(syn);
    return {};
  }

  void ensureThumbnailers() {
    if (m_loaded)
      return;
    m_thumbnailers = loadThumbnailers(m_thumbnailerDirs);
    m_loaded = true;
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

    const QFileInfo fi(job.path);
    if (!fi.exists() || !fi.isReadable() || fi.isDir()) {
      fail(job.key, job.path);
      kick();
      return;
    }

    ensureThumbnailers();
    const Thumbnailer *thumb =
        matchThumbnailer(m_thumbnailers, job.path, job.mime, m_mime);
    if (!thumb) {
      fail(job.key, job.path);
      kick();
      return;
    }

    const QString dest =
        ThumbnailService::synchroThumbPath(job.path, job.mtime, job.sizePx);
    QDir().mkpath(ThumbnailService::synchroThumbsDir());
    const QFile::Permissions ownerOnly =
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    QFile::setPermissions(ThumbnailService::synchroThumbsDir(), ownerOnly);

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
      QFile::remove(a.dest);
      if (QFile::rename(a.temp, a.dest)) {
        QFile::setPermissions(a.dest, QFile::ReadOwner | QFile::WriteOwner);
        const QString url = ThumbnailService::fileUrl(a.dest);
        m_ready.insert(a.key, url);
        if (notify)
          notify(a.path, url);
      } else {
        QFile::remove(a.temp);
        fail(a.key, a.path);
      }
    } else {
      QFile::remove(a.temp);
      fail(a.key, a.path);
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
  bool m_loaded = false;
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

ThumbnailService::ThumbnailService(QObject *parent) : QObject(parent) {
  qRegisterMetaType<ThumbnailJob>();
  qRegisterMetaType<QVector<ThumbnailJob>>();

  m_engine = new ThumbnailEngine;
  m_engine->notify = [this](const QString &path, const QString &url) {
    QMetaObject::invokeMethod(
        this, [this, path, url] { emit thumbnailReady(path, url); },
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

void ThumbnailService::request(const QString &path, qint64 mtime, int sizePx) {
  QVector<ThumbnailJob> jobs(1);
  jobs[0].path = path;
  jobs[0].mtime = mtime;
  jobs[0].sizePx = sizePx;
  emit submitted(jobs, false);
}

void ThumbnailService::requestVisible(const QVector<ThumbnailJob> &jobs) {
  emit submitted(jobs, true);
}

void ThumbnailService::cancelAll() { emit cancelRequested(); }

void ThumbnailService::setThumbnailerDirectories(const QStringList &dirs) {
  emit thumbnailerDirectoriesChanged(dirs);
}
