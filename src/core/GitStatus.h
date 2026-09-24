#pragma once

#include <QString>
#include <QStringList>

// Counts for one git checkout, parsed from `git status --porcelain -b`.
struct GitCounts {
  int untracked = 0;
  int modified = 0;
  int ahead = 0;
  int behind = 0;
  bool upstream = false;
  bool ok = false;
};

namespace GitStatus {

// Plain english title: parse git status porcelain.
// Reads a `git status --porcelain=v1 --branch` transcript.
// Returns untracked, modified, and ahead/behind counts.
GitCounts parseStatus(const QString &porcelain);

// Plain english title: format the counts for a row.
// Returns a short mark, or "git" when the command failed.
QString formatMark(const GitCounts &counts);

// Plain english title: strip terminal color codes.
QString stripAnsi(const QString &text);

// Plain english title: decide if a line is waiting for an answer.
// A short line ending in ':' or '?' is a question.
bool looksLikePrompt(const QString &line);

// Plain english title: nearest directory that contains .git.
// Walks parents of a file or folder. Empty when there is no checkout.
QString repoRoot(const QString &path);

// Plain english title: run git and return its stdout.
// Empty on timeout or failure. stderr is dropped.
QString runGit(const QString &cwd, const QStringList &args, int timeoutMs);

} // namespace GitStatus
