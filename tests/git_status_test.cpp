#include "GitStatus.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class GitStatusTest : public QObject {
  Q_OBJECT

private slots:
  void parseBranchCounts();
  void promptLines();
  void repoRootWalksToGit();
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

QTEST_GUILESS_MAIN(GitStatusTest)
#include "git_status_test.moc"
