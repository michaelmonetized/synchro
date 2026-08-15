#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class HandlerRegistry;

// Third-party git install ritual. Copy of `omarchy plugin add`: clone to a
// staging dir, validate, refuse reserved ids, land disabled, no sudo, no
// install hooks from the tree.
class HandlerInstall {
public:
  using PromptFn = std::function<bool(const QString &question)>;

  explicit HandlerInstall(HandlerRegistry *registry);

  void setAssumeYes(bool yes) { m_yes = yes; }
  void setEnableAfterAdd(bool on) { m_enableAfter = on; }
  void setPrompt(PromptFn fn) { m_prompt = std::move(fn); }
  void setGitProgram(const QString &git) { m_git = git; }

  bool add(const QString &url);
  bool update(const QString &id);
  bool remove(const QString &id);

  QString lastError() const { return m_error; }
  QString lastMessage() const { return m_message; }
  QString lastId() const { return m_lastId; }

private:
  bool confirm(const QString &question);
  bool runGit(const QStringList &args, const QString &cwd, QByteArray *out,
              QByteArray *err, int timeoutMs);
  bool updateOne(const QString &id, const QString &dir);
  void cleanupStage(const QString &stage);

  HandlerRegistry *m_reg = nullptr;
  PromptFn m_prompt;
  QString m_git;
  QString m_error;
  QString m_message;
  QString m_lastId;
  bool m_yes = false;
  bool m_enableAfter = false;
};
