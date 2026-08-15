#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>

// Detached peer launch: tokenize + substitute, never a shell.
// Wraps argv as `setsid uwsm-app -- <argv>` so apps land in
// app-graphical.slice instead of the compositor service.
class HandlerExec {
public:
  struct Item {
    QString path;
    QUrl uri;
    QString mime;
    bool isDir = false;
  };

  struct Request {
    QString exec;
    QString handlerId;
    QString handlerDir;
    QString cwd;
    QVector<Item> items;
  };

  using LaunchHook = std::function<bool(
      const QString &program, const QStringList &arguments,
      const QProcessEnvironment &env)>;

  static QStringList tokenize(const QString &exec);
  static QStringList substitute(const QString &exec, const Request &req);
  static QStringList wrapWithSession(const QStringList &argv);
  static QStringList wrapWithSession(const QStringList &argv,
                                     const QString &setsid,
                                     const QString &uwsmApp);

  bool run(const Request &req);
  void setLaunchHook(LaunchHook hook) { m_hook = std::move(hook); }
  QString lastError() const { return m_error; }

private:
  QProcessEnvironment buildEnv(const Request &req);
  QString writeSelection(const Request &req);

  LaunchHook m_hook;
  QString m_error;
};
