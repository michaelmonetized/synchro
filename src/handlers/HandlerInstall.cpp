#include "HandlerInstall.h"

#include "HandlerRegistry.h"
#include "Manifest.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

bool isGitCheckout(const QString &dir) {
  return QFileInfo(QDir(dir).filePath(QStringLiteral(".git"))).exists();
}

void removeRecursively(const QString &path) {
  QDir dir(path);
  if (dir.exists())
    dir.removeRecursively();
  else
    QFile::remove(path);
}

} // namespace

HandlerInstall::HandlerInstall(HandlerRegistry *registry) : m_reg(registry) {
  m_git = QStandardPaths::findExecutable(QStringLiteral("git"));
}

bool HandlerInstall::confirm(const QString &question) {
  if (m_yes)
    return true;
  if (m_prompt)
    return m_prompt(question);
  m_error = QStringLiteral("refusing to continue without confirmation; pass --yes");
  return false;
}

void HandlerInstall::cleanupStage(const QString &stage) {
  if (!stage.isEmpty())
    removeRecursively(stage);
}

bool HandlerInstall::runGit(const QStringList &args, const QString &cwd,
                            QByteArray *out, QByteArray *err, int timeoutMs) {
  if (m_git.isEmpty()) {
    m_error = QStringLiteral("git not found on PATH");
    return false;
  }
  QProcess proc;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  if (!env.contains(QStringLiteral("GIT_SSH_COMMAND")))
    env.insert(QStringLiteral("GIT_SSH_COMMAND"),
               QStringLiteral("ssh -oBatchMode=yes"));
  proc.setProcessEnvironment(env);
  proc.setProgram(m_git);
  proc.setArguments(args);
  if (!cwd.isEmpty())
    proc.setWorkingDirectory(cwd);
  proc.start();
  if (!proc.waitForFinished(timeoutMs)) {
    proc.kill();
    proc.waitForFinished(2000);
    m_error = QStringLiteral("git timed out");
    return false;
  }
  if (out)
    *out = proc.readAllStandardOutput();
  if (err)
    *err = proc.readAllStandardError();
  if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
    const QByteArray combined = err ? *err : proc.readAllStandardError();
    m_error = QString::fromUtf8(combined).trimmed();
    if (m_error.isEmpty())
      m_error = QStringLiteral("git failed");
    return false;
  }
  return true;
}

bool HandlerInstall::add(const QString &url) {
  m_error.clear();
  m_message.clear();
  m_lastId.clear();
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  if (url.trimmed().isEmpty()) {
    m_error = QStringLiteral("a git URL is required");
    return false;
  }
  if (!confirm(QStringLiteral("Clone and add this handler?")))
    return false;

  const QString userDir = m_reg->userDir();
  if (!QDir().mkpath(userDir)) {
    m_error = QStringLiteral("cannot create %1").arg(userDir);
    return false;
  }

  const QString stage =
      QDir(userDir).filePath(QStringLiteral(".add.tmp.%1")
                                 .arg(QCoreApplication::applicationPid()));
  cleanupStage(stage);

  // Clone only. Never exec anything from the tree (no install hooks, no sudo).
  if (!runGit({QStringLiteral("clone"), QStringLiteral("--"), url, stage},
              QString(), nullptr, nullptr, 120000)) {
    cleanupStage(stage);
    if (!m_error.contains(QLatin1String("git")))
      m_error = QStringLiteral("failed to clone %1").arg(url);
    else
      m_error = QStringLiteral("failed to clone %1: %2").arg(url, m_error);
    return false;
  }

  const ManifestValidation v = validateManifestDir(stage, false);
  if (!v.ok) {
    cleanupStage(stage);
    m_error = QStringLiteral("refusing to add: %1")
                  .arg(v.errors.join(QStringLiteral("; ")));
    return false;
  }
  const QString id = v.manifest.id;
  m_lastId = id;

  m_reg->scan();
  if (m_reg->contains(id)) {
    cleanupStage(stage);
    m_error = QStringLiteral("handler id '%1' is already installed").arg(id);
    return false;
  }

  const QString target = QDir(userDir).filePath(id);
  if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) {
    cleanupStage(stage);
    m_error = QStringLiteral("handler '%1' is already installed; update it "
                             "with: synchro handler update %1")
                  .arg(id);
    return false;
  }

  if (!QDir().rename(stage, target)) {
    cleanupStage(stage);
    m_error = QStringLiteral("failed to land %1").arg(target);
    return false;
  }

  m_reg->scan();
  if (m_enableAfter) {
    QString err;
    if (!m_reg->setEnabled(id, true, &err)) {
      m_error = err;
      return false;
    }
    m_message = QStringLiteral("Added and enabled %1 into %2").arg(id, target);
  } else {
    m_message = QStringLiteral("Added %1 into %2 (disabled). Enable it later "
                               "with: synchro handler enable %1")
                    .arg(id, target);
  }
  return true;
}

bool HandlerInstall::updateOne(const QString &id, const QString &dir) {
  QByteArray fetchErr;
  if (!runGit({QStringLiteral("fetch"), QStringLiteral("--quiet"),
               QStringLiteral("origin"), QStringLiteral("HEAD")},
              dir, nullptr, &fetchErr, 120000)) {
    m_error = QStringLiteral("fetch failed for '%1': %2").arg(id, m_error);
    return false;
  }

  QByteArray head;
  QByteArray fetchHead;
  if (!runGit({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, dir, &head,
              nullptr, 10000))
    return false;
  if (!runGit({QStringLiteral("rev-parse"), QStringLiteral("FETCH_HEAD")}, dir,
              &fetchHead, nullptr, 10000))
    return false;
  if (head.trimmed() == fetchHead.trimmed()) {
    m_message = QStringLiteral("%1 is up to date.").arg(id);
    return true;
  }

  QByteArray diff;
  runGit({QStringLiteral("diff"), QStringLiteral("HEAD"),
          QStringLiteral("FETCH_HEAD")},
         dir, &diff, nullptr, 30000);
  if (!diff.isEmpty())
    m_message = QString::fromUtf8(diff);

  if (!confirm(QStringLiteral("Update %1?").arg(id))) {
    m_message = QStringLiteral("Skipped %1.").arg(id);
    return true;
  }

  if (!runGit({QStringLiteral("merge"), QStringLiteral("--ff-only"),
               QStringLiteral("FETCH_HEAD")},
              dir, nullptr, nullptr, 30000)) {
    m_error = QStringLiteral("cannot fast-forward '%1'; you have local "
                             "changes in %2")
                  .arg(id, dir);
    return false;
  }

  const ManifestValidation v = validateManifestDir(dir, false);
  if (!v.ok) {
    runGit({QStringLiteral("reset"), QStringLiteral("--hard"),
            QStringLiteral("ORIG_HEAD")},
           dir, nullptr, nullptr, 10000);
    m_error = QStringLiteral("update of '%1' failed validation; rolled back: %2")
                  .arg(id, v.errors.join(QStringLiteral("; ")));
    return false;
  }
  m_lastId = id;
  m_message = QStringLiteral("Updated %1.").arg(id);
  return true;
}

bool HandlerInstall::update(const QString &id) {
  m_error.clear();
  m_message.clear();
  m_lastId.clear();
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  const QString userDir = m_reg->userDir();
  if (!QDir(userDir).exists()) {
    m_error = QStringLiteral("no handlers installed");
    return false;
  }

  QStringList targets;
  if (!id.isEmpty()) {
    if (!Manifest::isValidId(id)) {
      m_error = QStringLiteral("invalid handler id '%1'").arg(id);
      return false;
    }
    const QString dir = QDir(userDir).filePath(id);
    if (!QFileInfo(dir).isDir()) {
      m_error = QStringLiteral("handler '%1' is not installed").arg(id);
      return false;
    }
    if (!isGitCheckout(dir)) {
      m_error = QStringLiteral("handler '%1' is not a git checkout, so there "
                               "is nothing to pull from")
                    .arg(id);
      return false;
    }
    targets.append(id);
  } else {
    const QStringList subs =
        QDir(userDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : subs) {
      if (name.startsWith(QLatin1Char('.')))
        continue;
      const QString dir = QDir(userDir).filePath(name);
      if (isGitCheckout(dir))
        targets.append(name);
    }
    if (targets.isEmpty()) {
      m_message = QStringLiteral("No git-managed handlers installed.");
      return true;
    }
  }

  bool ok = true;
  QStringList notes;
  for (const QString &one : targets) {
    const QString dir = QDir(userDir).filePath(one);
    if (!updateOne(one, dir)) {
      ok = false;
      notes.append(m_error);
    } else if (!m_message.isEmpty()) {
      notes.append(m_message);
    }
  }
  m_message = notes.join(QLatin1Char('\n'));
  if (ok)
    m_reg->scan();
  return ok;
}

bool HandlerInstall::remove(const QString &id) {
  m_error.clear();
  m_message.clear();
  m_lastId.clear();
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  if (!Manifest::isValidId(id)) {
    m_error = QStringLiteral("invalid handler id '%1'").arg(id);
    return false;
  }
  if (Manifest::isReservedId(id)) {
    m_error = QStringLiteral("cannot remove first-party handler '%1'").arg(id);
    return false;
  }
  const QString target = QDir(m_reg->userDir()).filePath(id);
  const QFileInfo info(target);
  if (!info.exists() && !info.isSymLink()) {
    m_error = QStringLiteral("handler '%1' is not installed").arg(id);
    return false;
  }

  const QString prompt = info.isSymLink()
                             ? QStringLiteral("Unlink '%1'?").arg(id)
                             : isGitCheckout(target)
                                   ? QStringLiteral("Delete '%1'? Its git repo "
                                                    "remains upstream.")
                                         .arg(id)
                                   : QStringLiteral("Remove '%1'?").arg(id);
  if (!confirm(prompt))
    return false;

  m_reg->forget(id);

  if (info.isSymLink()) {
    QFile::remove(target);
    m_message = QStringLiteral("Unlinked %1.").arg(id);
  } else if (isGitCheckout(target)) {
    removeRecursively(target);
    m_message = QStringLiteral("Removed %1.").arg(id);
  } else {
    const QString stamp =
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddhhmmss"));
    QString backup = QDir(m_reg->userDir())
                         .filePath(QStringLiteral(".%1.bak.%2").arg(id, stamp));
    int n = 1;
    while (QFileInfo::exists(backup)) {
      backup = QDir(m_reg->userDir())
                   .filePath(QStringLiteral(".%1.bak.%2-%3").arg(id, stamp)
                                 .arg(n++));
    }
    if (!QDir().rename(target, backup)) {
      m_error = QStringLiteral("failed to move %1 to backup").arg(target);
      return false;
    }
    m_message = QStringLiteral("Removed %1. Backup at: %2").arg(id, backup);
  }
  m_lastId = id;
  return true;
}
