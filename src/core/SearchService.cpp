#include "SearchService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

SearchService::SearchService(QObject *parent) : QObject(parent) {}

SearchService::~SearchService() { cancel(); }

QString SearchService::executable() { return executable(Kind::Name); }

QString SearchService::executable(Kind kind) {
  if (kind == Kind::Content)
    return QStandardPaths::findExecutable(QStringLiteral("rg"));
  QString path = QStandardPaths::findExecutable(QStringLiteral("fd"));
  if (path.isEmpty())
    path = QStandardPaths::findExecutable(QStringLiteral("fdfind"));
  return path;
}

QString SearchService::fuzzyPattern(const QString &query) {
  const QString t = query.trimmed();
  if (t.isEmpty())
    return {};
  const QStringList parts =
      t.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  QStringList escaped;
  escaped.reserve(parts.size());
  for (const QString &part : parts)
    escaped.append(QRegularExpression::escape(part));
  return escaped.join(QStringLiteral(".*"));
}

QStringList SearchService::arguments(const QString &query, const QString &root,
                                     bool hidden, Kind kind) {
  if (kind == Kind::Content) {
    QStringList args{
        QStringLiteral("--color=never"),
        QStringLiteral("--no-heading"),
        QStringLiteral("--files-with-matches"),
        QStringLiteral("--max-count"),
        QStringLiteral("1"),
        QStringLiteral("--max-filesize"),
        QStringLiteral("8M"),
        QStringLiteral("--glob"),
        QStringLiteral("!.git/**"),
        QStringLiteral("-F"),
    };
    if (hidden)
      args.append(QStringLiteral("--hidden"));
    args.append(QStringLiteral("--"));
    args.append(query);
    args.append(root);
    return args;
  }
  const QString pattern = fuzzyPattern(query);
  QStringList args{QStringLiteral("--color=never"), QStringLiteral("--exclude"),
                   QStringLiteral(".git"),          QStringLiteral("-a"),
                   QStringLiteral("--max-results"),
                   QString::number(kMaxResults)};
  if (hidden)
    args.append(QStringLiteral("--hidden"));
  // `--` so a query that starts with '-' is still one pattern argv.
  args.append(QStringLiteral("--"));
  args.append(pattern.isEmpty() ? query : pattern);
  args.append(root);
  return args;
}

QString SearchService::resolveRoot(const QString &root) {
  if (root.isEmpty() || root.contains(QLatin1String("://")))
    return {};
  const QFileInfo fi(root);
  if (!fi.exists() || !fi.isDir())
    return {};
  const QString canon = fi.canonicalFilePath();
  return canon.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canon;
}

bool SearchService::isUnderRoot(const QString &root, const QString &path) {
  const QString r = QDir::cleanPath(root);
  const QString p = QDir::cleanPath(path);
  if (r.isEmpty() || p.isEmpty())
    return false;
  if (p == r)
    return true;
  return p.startsWith(r + QLatin1Char('/'));
}

void SearchService::start(const QString &query, const QString &root, bool hidden,
                          Kind kind) {
  cancel();
  ++m_gen;
  m_buf.clear();
  m_hits = 0;
  m_firstLineMs = -1;
  m_finishPending = false;
  m_pendingExit = 0;
  m_pendingStatus = 0;
  m_pendingError.clear();
  m_kind = kind;

  const QString exe = executable(kind);
  const QString missing = kind == Kind::Content
                              ? QStringLiteral("rg is not available")
                              : QStringLiteral("fd is not available");
  if (exe.isEmpty()) {
    finish(false, missing);
    return;
  }
  if (query.isEmpty() || root.isEmpty()) {
    finish(true, QString());
    return;
  }
  const QString absRoot = resolveRoot(root);
  if (absRoot.isEmpty()) {
    finish(false, QStringLiteral("search root is not a folder"));
    return;
  }
  m_root = absRoot;

  const quint64 gen = m_gen;
  m_proc = new QProcess(this);
  m_proc->setProgram(exe);
  m_proc->setWorkingDirectory(absRoot);
  m_proc->setArguments(arguments(query, absRoot, hidden, kind));
  m_proc->setProcessChannelMode(QProcess::SeparateChannels);
  connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, gen] {
    if (gen != m_gen)
      return;
    drainHits();
  });
  connect(m_proc, &QProcess::errorOccurred, this,
          [this, gen, missing](QProcess::ProcessError error) {
            if (gen != m_gen)
              return;
            if (error == QProcess::FailedToStart)
              finish(false, missing);
          });
  connect(m_proc, &QProcess::finished, this,
          [this, gen](int exitCode, QProcess::ExitStatus status) {
            if (gen != m_gen)
              return;
            onFinished(exitCode, static_cast<int>(status));
          });

  m_running = true;
  m_timer.start();
  m_proc->start();
}

void SearchService::cancel() {
  ++m_gen;
  m_drainScheduled = false;
  m_finishPending = false;
  destroyProcess();
  m_buf.clear();
  m_running = false;
  m_root.clear();
}

int SearchService::maxHits() const {
  return m_kind == Kind::Content ? kMaxContentResults : kMaxResults;
}

bool SearchService::running() const {
  return m_running && m_proc && m_proc->state() != QProcess::NotRunning;
}

void SearchService::onReadyRead() { drainHits(); }

void SearchService::drainHits() {
  m_drainScheduled = false;
  const quint64 gen = m_gen;
  if (m_proc)
    m_buf += m_proc->readAllStandardOutput();
  // Yield so a short query cannot ingest thousands of paths in one UI
  // turn. onFinished used to emit the leftover buffer as one path and
  // drop the rest — a fast fd/rg then kept only the first ~12 hits.
  int budget = 12;
  while (budget-- > 0 && gen == m_gen) {
    const int nl = m_buf.indexOf('\n');
    if (nl < 0)
      break;
    const QByteArray line = m_buf.left(nl);
    m_buf.remove(0, nl + 1);
    emitLine(line);
    if (m_hits >= maxHits()) {
      m_finishPending = false;
      destroyProcess();
      finish(true, QString());
      return;
    }
  }
  if (gen != m_gen)
    return;
  const bool more =
      m_buf.contains('\n') || (m_proc && m_proc->bytesAvailable() > 0);
  if (more && !m_drainScheduled) {
    m_drainScheduled = true;
    QTimer::singleShot(0, this, [this, gen] {
      if (gen != m_gen)
        return;
      drainHits();
    });
    return;
  }
  if (m_finishPending)
    completeFinish();
}

void SearchService::onFinished(int exitCode, int status) {
  if (m_proc)
    m_buf += m_proc->readAllStandardOutput();
  m_pendingExit = exitCode;
  m_pendingStatus = status;
  m_pendingError.clear();
  if (m_proc && exitCode != 0 && m_hits == 0)
    m_pendingError =
        QString::fromLocal8Bit(m_proc->readAllStandardError()).trimmed();
  m_finishPending = true;
  destroyProcess();
  drainHits();
}

void SearchService::completeFinish() {
  m_finishPending = false;
  if (!m_buf.isEmpty()) {
    emitLine(m_buf);
    m_buf.clear();
  }
  const bool crashed =
      m_pendingStatus == static_cast<int>(QProcess::CrashExit);
  QString err;
  if (crashed)
    err = m_kind == Kind::Content ? QStringLiteral("rg crashed")
                                  : QStringLiteral("fd crashed");
  else if (m_kind == Kind::Content && m_pendingExit == 1)
    err.clear();
  else if (m_pendingExit != 0 && m_hits == 0)
    err = m_pendingError;
  finish(err.isEmpty(), err);
}

void SearchService::emitLine(const QByteArray &raw) {
  QByteArray line = raw;
  if (line.endsWith('\r'))
    line.chop(1);
  if (line.isEmpty() || m_hits >= maxHits())
    return;
  QString path = QFile::decodeName(line);
  if (path.isEmpty())
    return;
  if (!QFileInfo(path).isAbsolute())
    path = QDir(m_root).filePath(path);
  path = QDir::cleanPath(path);
  if (!m_root.isEmpty() && !isUnderRoot(m_root, path))
    return;
  ++m_hits;
  if (m_firstLineMs < 0) {
    m_firstLineMs = m_timer.elapsed();
    emit firstHit(m_firstLineMs);
  }
  emit hit(path);
}

void SearchService::finish(bool ok, const QString &error) {
  if (!m_running && ok && error.isEmpty()) {
    emit finished(ok, error);
    return;
  }
  m_running = false;
  emit finished(ok, error);
}

void SearchService::destroyProcess() {
  if (!m_proc)
    return;
  QProcess *proc = m_proc;
  m_proc = nullptr;
  proc->disconnect(this);
  if (proc->state() != QProcess::NotRunning)
    proc->kill();
  proc->deleteLater();
}
