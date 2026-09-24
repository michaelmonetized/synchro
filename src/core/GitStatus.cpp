#include "GitStatus.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace GitStatus {

namespace {

int numberAfter(const QString &line, const QString &key) {
  const int at = line.indexOf(key);
  if (at < 0)
    return 0;
  int i = at + key.size();
  if (i >= line.size() || !line.at(i).isDigit())
    return 0;
  int n = 0;
  while (i < line.size() && line.at(i).isDigit()) {
    n = n * 10 + line.at(i).digitValue();
    ++i;
  }
  return n;
}

} // namespace

GitCounts parseStatus(const QString &porcelain) {
  GitCounts counts;
  counts.ok = true;
  const QStringList lines =
      porcelain.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &raw : lines) {
    const QString line = raw.endsWith(QLatin1Char('\r')) ? raw.chopped(1) : raw;
    if (line.startsWith(QLatin1String("##"))) {
      counts.upstream = line.contains(QLatin1String("..."));
      counts.ahead = numberAfter(line, QStringLiteral("ahead "));
      counts.behind = numberAfter(line, QStringLiteral("behind "));
      continue;
    }
    if (line.startsWith(QLatin1String("??")))
      ++counts.untracked;
    else if (line.size() >= 2)
      ++counts.modified;
  }
  return counts;
}

QString formatMark(const GitCounts &counts) {
  if (!counts.ok)
    return QStringLiteral("git");
  QString text = QStringLiteral("?%1 ~%2").arg(counts.untracked).arg(counts.modified);
  if (counts.upstream)
    text += QStringLiteral(" ↑%1 ↓%2").arg(counts.ahead).arg(counts.behind);
  return text;
}

QString stripAnsi(const QString &text) {
  QString out;
  out.reserve(text.size());
  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i) == QChar(0x1b)) {
      if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('[')) {
        i += 2;
        while (i < text.size()) {
          const ushort c = text.at(i).unicode();
          if (c >= 0x40 && c <= 0x7e)
            break;
          ++i;
        }
      }
      continue;
    }
    if (text.at(i) != QLatin1Char('\r'))
      out.append(text.at(i));
  }
  return out;
}

bool looksLikePrompt(const QString &line) {
  const QString text = stripAnsi(line).trimmed();
  if (text.isEmpty() || text.size() > 240)
    return false;
  const QChar last = text.back();
  return last == QLatin1Char(':') || last == QLatin1Char('?');
}

QString repoRoot(const QString &path) {
  if (path.isEmpty())
    return {};
  QFileInfo info(path);
  QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
  dir = QDir::cleanPath(dir);
  for (int i = 0; i < 64; ++i) {
    if (QFileInfo::exists(dir + QStringLiteral("/.git")))
      return dir;
    const QString parent = QFileInfo(dir).dir().absolutePath();
    if (parent == dir)
      break;
    dir = parent;
  }
  return {};
}

QString runGit(const QString &cwd, const QStringList &args, int timeoutMs) {
  QProcess process;
  process.setProgram(QStringLiteral("git"));
  process.setArguments(args);
  process.setWorkingDirectory(cwd);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(2000))
    return {};
  if (!process.waitForFinished(timeoutMs)) {
    process.kill();
    process.waitForFinished(1000);
    return {};
  }
  return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace GitStatus
