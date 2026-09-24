#include "GitStatus.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class GitStatusTest : public QObject {
  Q_OBJECT

private slots:
  void parseBranchCounts();
  void promptLines();
  void repoRootWalksToGit();
  void gitcpPushesToExistingOrigin();
  void gitcpAsksThenCreatesOrigin();
};

void GitStatusTest::parseBranchCounts() {
  const QString porcelain =
      QStringLiteral("## main...origin/main [ahead 2, behind 4]\n"
                     " M src/a.cpp\n"
                     "M  src/b.cpp\n"
                     "?? notes.txt\n"
                     "?? more.txt\n");
  const GitCounts counts = GitStatus::parseStatus(porcelain);
  QVERIFY(counts.ok);
  QVERIFY(counts.upstream);
  QCOMPARE(counts.ahead, 2);
  QCOMPARE(counts.behind, 4);
  QCOMPARE(counts.modified, 2);
  QCOMPARE(counts.untracked, 2);
  QCOMPARE(GitStatus::formatMark(counts), QStringLiteral("?2 ~2 ↑2 ↓4"));

  const GitCounts clean =
      GitStatus::parseStatus(QStringLiteral("## main\n"));
  QVERIFY(!clean.upstream);
  QCOMPARE(GitStatus::formatMark(clean), QStringLiteral("?0 ~0"));

  GitCounts failed;
  QCOMPARE(GitStatus::formatMark(failed), QStringLiteral("git"));
}

void GitStatusTest::promptLines() {
  QVERIFY(GitStatus::looksLikePrompt(
      QStringLiteral("Repo name [synchro]: ")));
  QVERIFY(GitStatus::looksLikePrompt(
      QStringLiteral("Visibility [private/public] (default: private):")));
  QVERIFY(!GitStatus::looksLikePrompt(QStringLiteral("Commit successful!")));
  QVERIFY(!GitStatus::looksLikePrompt(QStringLiteral("## main")));
  const QString colored =
      QStringLiteral("\033[32mRepo name [demo]:\033[0m");
  QVERIFY(GitStatus::looksLikePrompt(colored));
  QCOMPARE(GitStatus::stripAnsi(colored), QStringLiteral("Repo name [demo]:"));
}

void GitStatusTest::repoRootWalksToGit() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString root = tmp.filePath(QStringLiteral("proj"));
  QVERIFY(QDir().mkpath(root + QStringLiteral("/src")));
  QVERIFY(QDir().mkpath(root + QStringLiteral("/.git")));
  QFile head(root + QStringLiteral("/.git/HEAD"));
  QVERIFY(head.open(QIODevice::WriteOnly));
  head.write("ref: refs/heads/main\n");
  head.close();
  const QString file = root + QStringLiteral("/src/a.txt");
  QVERIFY(QFile(file).open(QIODevice::WriteOnly));
  QCOMPARE(GitStatus::repoRoot(file), QDir(root).absolutePath());
  QCOMPARE(GitStatus::repoRoot(root + QStringLiteral("/src")),
           QDir(root).absolutePath());
  QVERIFY(GitStatus::repoRoot(tmp.path()).isEmpty());
}

namespace {

bool runGit(const QStringList &args) {
  QProcess process;
  process.start(QStringLiteral("git"), args);
  return process.waitForStarted(2000) && process.waitForFinished(10000) &&
         process.exitCode() == 0;
}

} // namespace

void GitStatusTest::gitcpPushesToExistingOrigin() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString bare = tmp.filePath(QStringLiteral("remote.git"));
  const QString repo = tmp.filePath(QStringLiteral("repo"));
  QVERIFY(runGit({QStringLiteral("init"), QStringLiteral("--bare"), bare}));
  QVERIFY(runGit({QStringLiteral("init"), repo}));
  QVERIFY(runGit({QStringLiteral("-C"), repo, QStringLiteral("config"),
                  QStringLiteral("user.email"),
                  QStringLiteral("test@example.com")}));
  QVERIFY(runGit({QStringLiteral("-C"), repo, QStringLiteral("config"),
                  QStringLiteral("user.name"), QStringLiteral("Test")}));
  QFile file(repo + QStringLiteral("/a.txt"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("a\n");
  file.close();
  QVERIFY(runGit({QStringLiteral("-C"), repo, QStringLiteral("remote"),
                  QStringLiteral("add"), QStringLiteral("origin"), bare}));

  QProcess gitcp;
  gitcp.setWorkingDirectory(repo);
  gitcp.start(QStringLiteral(SYNCHRO_GITCP_SOURCE), {QStringLiteral("hello")});
  QVERIFY(gitcp.waitForStarted(2000));
  QVERIFY(gitcp.waitForFinished(15000));
  QCOMPARE(gitcp.exitCode(), 0);

  QProcess log;
  log.start(QStringLiteral("git"),
            {QStringLiteral("--git-dir"), bare, QStringLiteral("log"),
             QStringLiteral("-1"), QStringLiteral("--format=%s")});
  QVERIFY(log.waitForFinished(5000));
  QCOMPARE(QString::fromUtf8(log.readAllStandardOutput()).trimmed(),
           QStringLiteral("hello"));
}

void GitStatusTest::gitcpAsksThenCreatesOrigin() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString repo = tmp.filePath(QStringLiteral("notes_app"));
  QVERIFY(QDir().mkpath(repo));
  QVERIFY(runGit({QStringLiteral("init"), repo}));
  QVERIFY(runGit({QStringLiteral("-C"), repo, QStringLiteral("config"),
                  QStringLiteral("user.email"),
                  QStringLiteral("test@example.com")}));
  QVERIFY(runGit({QStringLiteral("-C"), repo, QStringLiteral("config"),
                  QStringLiteral("user.name"), QStringLiteral("Test")}));
  QFile file(repo + QStringLiteral("/a.txt"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("a\n");
  file.close();

  const QString bin = tmp.filePath(QStringLiteral("bin"));
  QVERIFY(QDir().mkpath(bin));
  const QString ghLog = tmp.filePath(QStringLiteral("gh.log"));
  QFile gh(bin + QStringLiteral("/gh"));
  QVERIFY(gh.open(QIODevice::WriteOnly));
  gh.write("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$GH_LOG\"\nexit 0\n");
  gh.close();
  QVERIFY(QFile::setPermissions(gh.fileName(),
                                QFile::ExeOwner | QFile::ReadOwner |
                                    QFile::WriteOwner));

  QProcess gitcp;
  gitcp.setWorkingDirectory(repo);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("PATH"),
             bin + QStringLiteral(":") + env.value(QStringLiteral("PATH")));
  env.insert(QStringLiteral("GH_LOG"), ghLog);
  env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  gitcp.setProcessEnvironment(env);
  gitcp.start(QStringLiteral(SYNCHRO_GITCP_SOURCE), {QStringLiteral("ship")});
  QVERIFY(gitcp.waitForStarted(2000));
  gitcp.write("Widget\npublic\n");
  gitcp.closeWriteChannel();
  QVERIFY(gitcp.waitForFinished(15000));

  const QString text = QString::fromUtf8(gitcp.readAllStandardError() +
                                         gitcp.readAllStandardOutput());
  QVERIFY(text.contains(QStringLiteral("Repo name [")));
  QVERIFY(text.contains(QStringLiteral("Visibility [private/public]")));
  QFile log(ghLog);
  QVERIFY(log.open(QIODevice::ReadOnly));
  const QString called = QString::fromUtf8(log.readAll());
  QCOMPARE(called.trimmed(),
           QStringLiteral("repo create michaelmonetized/Widget --public"));
  QProcess remote;
  remote.start(QStringLiteral("git"),
               {QStringLiteral("-C"), repo, QStringLiteral("config"),
                QStringLiteral("--get"), QStringLiteral("remote.origin.url")});
  QVERIFY(remote.waitForFinished(5000));
  QCOMPARE(QString::fromUtf8(remote.readAllStandardOutput()).trimmed(),
           QStringLiteral("https://github.com/michaelmonetized/Widget.git"));
}

QTEST_GUILESS_MAIN(GitStatusTest)
#include "git_status_test.moc"
