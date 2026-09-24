#include "GitLane.h"

#include "DirectoryLister.h"
#include "DirectoryModel.h"
#include "GitStatus.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QtConcurrent>

#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>

namespace {

constexpr int kGitTimeoutMs = 8000;
constexpr int kDiffLineCap = 8000;
constexpr int kDiffByteCap = 512 * 1024;

QString unquote(QString path) {
  path = path.trimmed();
  if (path.size() >= 2 && path.front() == QLatin1Char('"') &&
      path.back() == QLatin1Char('"')) {
    path = path.mid(1, path.size() - 2);
    path.replace(QLatin1String("\\\""), QStringLiteral("\""));
    path.replace(QLatin1String("\\\\"), QStringLiteral("\\"));
  }
  return path;
}

QString porcelainPath(const QString &line) {
  QString path = line.size() > 3 ? line.mid(3) : QString();
  const int arrow = path.indexOf(QLatin1String(" -> "));
  if (arrow >= 0)
    path = path.mid(arrow + 4);
  return unquote(path);
}

bool underScope(const QString &path, const QString &scope) {
  if (scope.isEmpty() || scope == QLatin1String("."))
    return true;
  return path == scope || path.startsWith(scope + QLatin1Char('/'));
}

QStringList splitLines(const QString &text) {
  QStringList lines = text.split(QLatin1Char('\n'));
  if (!lines.isEmpty() && lines.constLast().isEmpty())
    lines.removeLast();
  if (lines.size() > kDiffLineCap) {
    lines = lines.mid(0, kDiffLineCap);
    lines.append(QStringLiteral("… truncated"));
  }
  return lines;
}

} // namespace

GitLane::GitLane(DirectoryModel *model, QObject *parent)
    : QObject(parent), m_model(model) {
  m_scanTimer.setSingleShot(true);
  m_scanTimer.setInterval(80);
  connect(&m_scanTimer, &QTimer::timeout, this, &GitLane::scan);
  m_promptTimer.setSingleShot(true);
  m_promptTimer.setInterval(180);
  connect(&m_promptTimer, &QTimer::timeout, this, &GitLane::considerPrompt);
  if (m_model)
    connect(m_model, &DirectoryModel::listingChanged, this,
            &GitLane::scheduleScan);
}

GitLane::~GitLane() { cancelCommand(); }

QString GitLane::mark(const QString &path) const {
  return m_marks.value(path);
}

void GitLane::scheduleScan() { m_scanTimer.start(); }

void GitLane::scan() {
  if (!m_model || m_model->listing())
    return;
  const QString folder = m_model->path();
  if (!folder.startsWith(QLatin1Char('/'))) {
    if (!m_marks.isEmpty()) {
      m_marks.clear();
      ++m_revision;
      emit revisionChanged();
    }
    return;
  }
  QStringList dirs;
  const int rows = m_model->rowCount();
  dirs.reserve(rows);
  for (int i = 0; i < rows; ++i) {
    const DirectoryEntry *entry = m_model->entryAt(i);
    if (entry && entry->isDir && !entry->path.isEmpty())
      dirs.append(entry->path);
  }
  if (m_scanning) {
    m_scanAgain = true;
    ++m_generation;
    return;
  }
  const int generation = ++m_generation;
  m_scanning = true;
  (void)QtConcurrent::run([this, generation, dirs] {
    QHash<QString, QString> marks;
    for (const QString &dir : dirs) {
      if (generation != m_generation.load())
        break;
      if (!QFileInfo::exists(dir + QStringLiteral("/.git")))
        continue;
      const QString out = GitStatus::runGit(
          dir,
          {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
           QStringLiteral("-uall"), QStringLiteral("--branch"),
           QStringLiteral("--no-renames")},
          kGitTimeoutMs);
      GitCounts counts;
      if (!out.isEmpty())
        counts = GitStatus::parseStatus(out);
      marks.insert(dir, GitStatus::formatMark(counts));
    }
    QMetaObject::invokeMethod(
        this, [this, generation, marks] { publishMarks(generation, marks); },
        Qt::QueuedConnection);
  });
}

void GitLane::publishMarks(int generation, const QHash<QString, QString> &marks) {
  m_scanning = false;
  if (generation == m_generation.load()) {
    m_marks = marks;
    ++m_revision;
    emit revisionChanged();
  }
  if (m_scanAgain) {
    m_scanAgain = false;
    scheduleScan();
  }
}

void GitLane::openDiff(const QString &path) {
  QString scope = path;
  QFileInfo info(path);
  if (!info.isDir())
    scope = info.absolutePath();
  const QString root = GitStatus::repoRoot(scope.isEmpty() ? path : scope);
  if (root.isEmpty()) {
    emit note(QStringLiteral("not a git repo"));
    return;
  }
  m_diffRoot = root;
  m_diffScope = QDir(root).relativeFilePath(QDir::cleanPath(scope));
  m_diffTitle = QFileInfo(root).fileName();
  if (!m_diffScope.isEmpty() && m_diffScope != QLatin1String("."))
    m_diffTitle += QStringLiteral(" / ") + m_diffScope;
  m_diffOpen = true;
  m_diffBusy = true;
  m_diffFiles.clear();
  m_diffLines.clear();
  m_diffFile = 0;
  m_diffLine = 0;
  m_diffCol = 0;
  emit diffChanged();
  loadDiffFiles();
}

void GitLane::closeDiff() {
  if (!m_diffOpen)
    return;
  m_diffOpen = false;
  m_diffBusy = false;
  ++m_diffGeneration;
  emit diffChanged();
}

void GitLane::loadDiffFiles() {
  const int generation = ++m_diffGeneration;
  const QString root = m_diffRoot;
  const QString scope = m_diffScope;
  m_diffBusy = true;
  (void)QtConcurrent::run([this, generation, root, scope] {
    const QString out = GitStatus::runGit(
        root,
        {QStringLiteral("status"), QStringLiteral("--porcelain=v1"),
         QStringLiteral("-uall"), QStringLiteral("--no-renames")},
        kGitTimeoutMs);
    QVariantList files;
    const QStringList lines =
        out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
      if (line.startsWith(QLatin1String("##")) || line.size() < 4)
        continue;
      const QString path = porcelainPath(line);
      if (path.isEmpty() || !underScope(path, scope))
        continue;
      QVariantMap row;
      row.insert(QStringLiteral("status"), line.left(2));
      row.insert(QStringLiteral("path"), path);
      row.insert(QStringLiteral("label"), line.left(2) + QLatin1Char(' ') + path);
      files.append(row);
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, files] { publishDiffFiles(generation, files); },
        Qt::QueuedConnection);
  });
}

void GitLane::publishDiffFiles(int generation, const QVariantList &files) {
  if (generation != m_diffGeneration || !m_diffOpen)
    return;
  m_diffFiles = files;
  m_diffFile = 0;
  m_diffLine = 0;
  m_diffCol = 0;
  emit diffChanged();
  if (files.isEmpty()) {
    m_diffBusy = false;
    m_diffLines = QStringList{QStringLiteral("clean")};
    emit diffChanged();
    return;
  }
  loadDiffBody();
}

void GitLane::loadDiffBody() {
  if (m_diffFile < 0 || m_diffFile >= m_diffFiles.size())
    return;
  const QVariantMap row = m_diffFiles.at(m_diffFile).toMap();
  const QString path = row.value(QStringLiteral("path")).toString();
  const QString status = row.value(QStringLiteral("status")).toString();
  const int generation = m_diffGeneration;
  const QString root = m_diffRoot;
  m_diffBusy = true;
  emit diffChanged();
  (void)QtConcurrent::run([this, generation, root, path, status] {
    QString text;
    if (status == QLatin1String("??")) {
      text = GitStatus::runGit(
          root,
          {QStringLiteral("diff"), QStringLiteral("--no-color"),
           QStringLiteral("--no-index"), QStringLiteral("--"),
           QStringLiteral("/dev/null"), path},
          kGitTimeoutMs);
    } else {
      text = GitStatus::runGit(
          root,
          {QStringLiteral("diff"), QStringLiteral("--no-color"),
           QStringLiteral("--no-ext-diff"), QStringLiteral("-U3"),
           QStringLiteral("HEAD"), QStringLiteral("--"), path},
          kGitTimeoutMs);
    }
    if (text.size() > kDiffByteCap)
      text = text.left(kDiffByteCap) + QStringLiteral("\n… truncated");
    if (text.trimmed().isEmpty())
      text = status == QLatin1String("??") ? QStringLiteral("untracked, empty")
                                           : QStringLiteral("no diff text");
    const QStringList lines = splitLines(text);
    QMetaObject::invokeMethod(
        this,
        [this, generation, lines] { publishDiffBody(generation, lines); },
        Qt::QueuedConnection);
  });
}

void GitLane::publishDiffBody(int generation, const QStringList &lines) {
  if (generation != m_diffGeneration || !m_diffOpen)
    return;
  m_diffLines = lines;
  m_diffLine = 0;
  m_diffCol = 0;
  m_diffBusy = false;
  emit diffChanged();
}

void GitLane::moveDiffLine(int delta) {
  if (m_diffLines.isEmpty())
    return;
  const int next = qBound(0, m_diffLine + delta, m_diffLines.size() - 1);
  if (next == m_diffLine)
    return;
  m_diffLine = next;
  m_diffCol = 0;
  emit diffChanged();
}

void GitLane::pageDiffLine(int direction, int page) {
  moveDiffLine(direction * qMax(1, page));
}

void GitLane::edgeDiffLine(int direction) {
  if (m_diffLines.isEmpty())
    return;
  m_diffLine = direction < 0 ? 0 : m_diffLines.size() - 1;
  m_diffCol = 0;
  emit diffChanged();
}

void GitLane::moveDiffCol(int delta) {
  if (m_diffLine < 0 || m_diffLine >= m_diffLines.size())
    return;
  const int width = m_diffLines.at(m_diffLine).size();
  m_diffCol = qBound(0, m_diffCol + delta, qMax(0, width));
  emit diffChanged();
}

void GitLane::edgeDiffCol(int direction) {
  if (m_diffLine < 0 || m_diffLine >= m_diffLines.size())
    return;
  m_diffCol = direction < 0 ? 0 : m_diffLines.at(m_diffLine).size();
  emit diffChanged();
}

void GitLane::moveDiffFile(int delta) {
  if (m_diffFiles.isEmpty())
    return;
  const int next = qBound(0, m_diffFile + delta, m_diffFiles.size() - 1);
  if (next == m_diffFile)
    return;
  m_diffFile = next;
  m_diffLine = 0;
  m_diffCol = 0;
  m_diffLines.clear();
  emit diffChanged();
  loadDiffBody();
}

void GitLane::commit(const QString &path, const QString &message) {
  const QString root = GitStatus::repoRoot(path);
  if (root.isEmpty()) {
    emit note(QStringLiteral("not a git repo"));
    return;
  }
  emit note(QStringLiteral("committing"));
  startShell(root, QStringLiteral("git add -A && git commit -m \"$1\""),
             {message});
}

void GitLane::push(const QString &path) {
  const QString root = GitStatus::repoRoot(path);
  if (root.isEmpty()) {
    emit note(QStringLiteral("not a git repo"));
    return;
  }
  emit note(QStringLiteral("pushing"));
  startShell(root,
             QStringLiteral("source \"$HOME/.config/zsh/git\" && git_push"),
             {});
}

void GitLane::gitcp(const QString &path, const QString &message) {
  const QString root = GitStatus::repoRoot(path);
  if (root.isEmpty()) {
    emit note(QStringLiteral("not a git repo"));
    return;
  }
  emit note(QStringLiteral("committing and pushing"));
  startShell(root,
             QStringLiteral(
                 "source \"$HOME/.config/zsh/git\" && git_commit_push \"$1\""),
             {message});
}

void GitLane::openNvim(const QString &path) {
  QFileInfo info(path);
  if (!info.exists()) {
    emit note(QStringLiteral("nothing to open"));
    return;
  }
  const QString dir =
      info.isDir() ? info.absoluteFilePath() : info.absolutePath();
  const QString arg = info.isDir() ? QStringLiteral(".") : info.fileName();
  const QString term = QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec"));
  if (term.isEmpty()) {
    emit note(QStringLiteral("xdg-terminal-exec is missing"));
    return;
  }
  if (!QProcess::startDetached(term, {QStringLiteral("--dir=") + dir,
                                      QStringLiteral("nvim"), arg}))
    emit note(QStringLiteral("could not open nvim"));
}

void GitLane::openT3(const QString &path, bool isFile) {
  QString target = path;
  if (isFile) {
    const QString root = GitStatus::repoRoot(path);
    target = root.isEmpty() ? QFileInfo(path).absolutePath() : root;
  } else if (!QFileInfo(path).isDir()) {
    target = QFileInfo(path).absolutePath();
  }
  if (target.isEmpty()) {
    emit note(QStringLiteral("nothing to open"));
    return;
  }
  const QString bin = QStandardPaths::findExecutable(QStringLiteral("t3"));
  if (bin.isEmpty()) {
    emit note(QStringLiteral("t3 is missing"));
    return;
  }
  auto *process = new QProcess(this);
  connect(process,
          QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, process](int code, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(process->readAllStandardOutput() +
                                                   process->readAllStandardError())
                                    .trimmed();
            const QString line = out.isEmpty()
                                     ? (code ? QStringLiteral("t3 failed")
                                             : QStringLiteral("opened in t3"))
                                     : out.section(QLatin1Char('\n'), -1);
            emit note(line.left(180));
            process->deleteLater();
          });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
              return;
            emit note(process->errorString());
            process->deleteLater();
          });
  process->start(bin, {QStringLiteral("app"), target});
}

void GitLane::startShell(const QString &cwd, const QString &script,
                         const QStringList &args) {
  cancelCommand();
  struct winsize size {};
  size.ws_col = 120;
  size.ws_row = 40;
  int master = -1;
  const pid_t pid = forkpty(&master, nullptr, nullptr, &size);
  if (pid < 0) {
    emit note(QStringLiteral("could not start the shell"));
    emit commandFinished();
    return;
  }
  if (pid == 0) {
    if (!cwd.isEmpty())
      ::chdir(cwd.toLocal8Bit().constData());
    std::vector<std::string> owned;
    owned.emplace_back("zsh");
    owned.emplace_back("-c");
    owned.emplace_back(script.toStdString());
    owned.emplace_back("synchro");
    for (const QString &arg : args)
      owned.emplace_back(arg.toStdString());
    std::vector<char *> argv;
    argv.reserve(owned.size() + 1);
    for (std::string &word : owned)
      argv.push_back(word.data());
    argv.push_back(nullptr);
    ::execvp("zsh", argv.data());
    _exit(127);
  }
  const int flags = fcntl(master, F_GETFL, 0);
  if (flags >= 0)
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
  m_master = master;
  m_child = pid;
  m_pending.clear();
  m_log.clear();
  m_waiting = false;
  m_pty = new QSocketNotifier(master, QSocketNotifier::Read, this);
  connect(m_pty, &QSocketNotifier::activated, this, &GitLane::onPty);
}

void GitLane::onPty() {
  if (m_master < 0)
    return;
  char buffer[4096];
  for (;;) {
    const ssize_t n = ::read(m_master, buffer, sizeof buffer);
    if (n > 0) {
      const QString chunk = GitStatus::stripAnsi(QString::fromUtf8(buffer, int(n)));
      m_pending += chunk;
      m_log += chunk;
      if (m_log.size() > 16000)
        m_log = m_log.right(8000);
      if (!m_waiting)
        m_promptTimer.start();
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      return;
    reap(false);
    return;
  }
}

void GitLane::considerPrompt() {
  if (m_child < 0 || m_waiting)
    return;
  const int nl = m_pending.lastIndexOf(QLatin1Char('\n'));
  const QString tail = nl < 0 ? m_pending : m_pending.mid(nl + 1);
  if (!GitStatus::looksLikePrompt(tail))
    return;
  m_waiting = true;
  emit question(GitStatus::stripAnsi(tail).trimmed());
}

void GitLane::answer(const QString &text) {
  if (m_master < 0)
    return;
  QByteArray bytes = text.toUtf8();
  bytes.append('\n');
  ::write(m_master, bytes.constData(), size_t(bytes.size()));
  m_waiting = false;
  m_pending.clear();
}

QString GitLane::shellTail() const {
  const QStringList lines =
      m_log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  if (lines.isEmpty())
    return {};
  return lines.constLast().trimmed().left(180);
}

void GitLane::reap(bool killed) {
  if (m_pty) {
    m_pty->deleteLater();
    m_pty = nullptr;
  }
  if (m_master >= 0) {
    ::close(m_master);
    m_master = -1;
  }
  int status = 0;
  if (m_child > 0) {
    if (killed)
      ::kill(m_child, SIGTERM);
    ::waitpid(m_child, &status, killed ? 0 : 0);
    m_child = -1;
  }
  m_waiting = false;
  m_promptTimer.stop();
  if (!killed) {
    const QString tail = shellTail();
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
      emit note(tail.isEmpty() ? QStringLiteral("done") : tail);
    else
      emit note(tail.isEmpty() ? QStringLiteral("git failed") : tail);
    scheduleScan();
  }
  emit commandFinished();
}

void GitLane::cancelCommand() {
  if (m_child > 0)
    reap(true);
}
