#include "SearchService.h"

#include <QFile>
#include <QProcess>
#include <QStandardPaths>

SearchService::SearchService(QObject *parent) : QObject(parent) {}

SearchService::~SearchService() { cancel(); }

QString SearchService::executable() {
  QString path = QStandardPaths::findExecutable(QStringLiteral("fd"));
  if (path.isEmpty())
    path = QStandardPaths::findExecutable(QStringLiteral("fdfind"));
  return path;
}

QStringList SearchService::arguments(const QString &query, const QString &root,
                                     bool hidden) {
  QStringList args{QStringLiteral("--color=never"), QStringLiteral("--exclude"),
                   QStringLiteral(".git"),          QStringLiteral("-F"),
                   QStringLiteral("-a"),            QStringLiteral("--max-results"),
                   QString::number(kMaxResults)};
  if (hidden)
    args.append(QStringLiteral("--hidden"));
  // `--` so a query that starts with '-' is still one pattern argv.
  args.append(QStringLiteral("--"));
  args.append(query);
  args.append(root);
  return args;
}

void SearchService::start(const QString &query, const QString &root,
                          bool hidden) {
  cancel();
  ++m_gen;
  m_buf.clear();
  m_hits = 0;
  m_firstLineMs = -1;

  const QString exe = executable();
  if (exe.isEmpty()) {
    finish(false, QStringLiteral("fd is not available"));
    return;
  }
  if (query.isEmpty() || root.isEmpty()) {
    finish(true, QString());
    return;
  }

  const quint64 gen = m_gen;
  m_proc = new QProcess(this);
  m_proc->setProgram(exe);
  m_proc->setArguments(arguments(query, root, hidden));
  m_proc->setProcessChannelMode(QProcess::SeparateChannels);
  connect(m_proc, &QProcess::readyReadStandardOutput, this, [this, gen] {
    if (gen != m_gen)
      return;
    onReadyRead();
  });
  connect(m_proc, &QProcess::errorOccurred, this,
          [this, gen](QProcess::ProcessError error) {
            if (gen != m_gen)
              return;
            if (error == QProcess::FailedToStart)
              finish(false, QStringLiteral("fd is not available"));
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
  destroyProcess();
  m_buf.clear();
  m_running = false;
}

bool SearchService::running() const {
  return m_running && m_proc && m_proc->state() != QProcess::NotRunning;
}

void SearchService::onReadyRead() {
  if (!m_proc)
    return;
  m_buf += m_proc->readAllStandardOutput();
  int nl = 0;
  while ((nl = m_buf.indexOf('\n')) >= 0) {
    const QByteArray line = m_buf.left(nl);
    m_buf.remove(0, nl + 1);
    emitLine(line);
    if (m_hits >= kMaxResults) {
      destroyProcess();
      finish(true, QString());
      return;
    }
  }
}

void SearchService::onFinished(int exitCode, int status) {
  if (!m_buf.isEmpty()) {
    emitLine(m_buf);
    m_buf.clear();
  }
  const bool crashed = status == static_cast<int>(QProcess::CrashExit);
  QString err;
  if (crashed)
    err = QStringLiteral("fd crashed");
  else if (exitCode != 0 && m_hits == 0 && m_proc)
    err = QString::fromLocal8Bit(m_proc->readAllStandardError()).trimmed();
  destroyProcess();
  finish(err.isEmpty(), err);
}

void SearchService::emitLine(const QByteArray &raw) {
  QByteArray line = raw;
  if (line.endsWith('\r'))
    line.chop(1);
  if (line.isEmpty() || m_hits >= kMaxResults)
    return;
  const QString path = QFile::decodeName(line);
  if (path.isEmpty())
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
  if (proc->state() != QProcess::NotRunning) {
    proc->kill();
    proc->waitForFinished(100);
  }
  proc->deleteLater();
}
