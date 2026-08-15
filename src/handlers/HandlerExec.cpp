#include "HandlerExec.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

namespace {

QStringList g_selectionFiles;
bool g_quitHooked = false;

void trackSelectionFile(const QString &path) {
  auto *app = QCoreApplication::instance();
  if (!app)
    return;
  if (!g_quitHooked) {
    g_quitHooked = true;
    QObject::connect(app, &QCoreApplication::aboutToQuit, app, [] {
      for (const QString &p : g_selectionFiles)
        QFile::remove(p);
      g_selectionFiles.clear();
    });
  }
  g_selectionFiles.append(path);
  QTimer::singleShot(60000, app, [path] {
    QFile::remove(path);
    g_selectionFiles.removeAll(path);
  });
}



QString firstPath(const HandlerExec::Request &req) {
  return req.items.isEmpty() ? QString() : req.items.constFirst().path;
}

QUrl firstUri(const HandlerExec::Request &req) {
  if (req.items.isEmpty())
    return {};
  const auto &item = req.items.constFirst();
  if (!item.uri.isEmpty())
    return item.uri;
  return QUrl::fromLocalFile(item.path);
}

QString firstUriString(const HandlerExec::Request &req) {
  return firstUri(req).toString(QUrl::FullyEncoded);
}

QString dirOfFirst(const HandlerExec::Request &req) {
  const QString path = firstPath(req);
  if (path.isEmpty())
    return req.cwd;
  const QFileInfo info(path);
  if (info.isDir())
    return info.absoluteFilePath();
  // File items use the listing cwd (parent of the path).
  return info.absolutePath();
}

QString themeDir() {
  return QDir::homePath() +
         QStringLiteral("/.local/state/omarchy/current/theme");
}

QString expandCodes(const QString &token, const HandlerExec::Request &req) {
  QString out;
  out.reserve(token.size());
  for (int i = 0; i < token.size(); ++i) {
    if (token[i] != QLatin1Char('%') || i + 1 >= token.size()) {
      out += token[i];
      continue;
    }
    const QChar code = token[i + 1];
    ++i;
    switch (code.toLatin1()) {
    case '%':
      out += QLatin1Char('%');
      break;
    case 'f':
      out += firstPath(req);
      break;
    case 'u':
      out += firstUriString(req);
      break;
    case 'd':
      out += dirOfFirst(req);
      break;
    case 'i':
      out += req.handlerId;
      break;
    case 'F': {
      QStringList paths;
      paths.reserve(req.items.size());
      for (const auto &item : req.items)
        paths.append(item.path);
      out += paths.join(QLatin1Char(' '));
      break;
    }
    case 'U': {
      QStringList uris;
      uris.reserve(req.items.size());
      for (const auto &item : req.items) {
        const QUrl uri =
            item.uri.isEmpty() ? QUrl::fromLocalFile(item.path) : item.uri;
        uris.append(uri.toString(QUrl::FullyEncoded));
      }
      out += uris.join(QLatin1Char(' '));
      break;
    }
    default:
      out += QLatin1Char('%');
      out += code;
      break;
    }
  }
  return out;
}

} // namespace

QStringList HandlerExec::tokenize(const QString &exec) {
  QStringList out;
  QString cur;
  bool inQuote = false;
  bool escaped = false;
  for (const QChar c : exec) {
    if (escaped) {
      cur += c;
      escaped = false;
      continue;
    }
    if (c == QLatin1Char('\\')) {
      escaped = true;
      continue;
    }
    if (c == QLatin1Char('"')) {
      inQuote = !inQuote;
      continue;
    }
    if (!inQuote && c.isSpace()) {
      if (!cur.isEmpty()) {
        out.append(cur);
        cur.clear();
      }
      continue;
    }
    cur += c;
  }
  if (!cur.isEmpty())
    out.append(cur);
  return out;
}

QStringList HandlerExec::substitute(const QString &exec, const Request &req) {
  const QStringList tokens = tokenize(exec);
  QStringList out;
  out.reserve(tokens.size() + req.items.size());
  for (QString token : tokens) {
    token.replace(QLatin1String("${handlerDir}"), req.handlerDir);
    if (token == QLatin1String("%F")) {
      for (const auto &item : req.items)
        out.append(item.path);
      continue;
    }
    if (token == QLatin1String("%U")) {
      for (const auto &item : req.items) {
        const QUrl uri =
            item.uri.isEmpty() ? QUrl::fromLocalFile(item.path) : item.uri;
        out.append(uri.toString(QUrl::FullyEncoded));
      }
      continue;
    }
    out.append(expandCodes(token, req));
  }
  return out;
}

QStringList HandlerExec::wrapWithSession(const QStringList &argv,
                                         const QString &setsid,
                                         const QString &uwsmApp) {
  QStringList out = argv;
  if (!uwsmApp.isEmpty()) {
    QStringList prefix{uwsmApp, QStringLiteral("--")};
    out = prefix + out;
  }
  if (!setsid.isEmpty())
    out.prepend(setsid);
  return out;
}

QStringList HandlerExec::wrapWithSession(const QStringList &argv) {
  return wrapWithSession(
      argv, QStandardPaths::findExecutable(QStringLiteral("setsid")),
      QStandardPaths::findExecutable(QStringLiteral("uwsm-app")));
}

QString HandlerExec::writeSelection(const Request &req) {
  QTemporaryFile file(
      QDir::temp().filePath(QStringLiteral("synchro-sel-XXXXXX.json")));
  file.setAutoRemove(false);
  if (!file.open())
    return {};
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const QString path = file.fileName();

  QJsonArray items;
  for (const auto &item : req.items) {
    QJsonObject o;
    o.insert(QStringLiteral("path"), item.path);
    const QUrl uri =
        item.uri.isEmpty() ? QUrl::fromLocalFile(item.path) : item.uri;
    o.insert(QStringLiteral("uri"), uri.toString(QUrl::FullyEncoded));
    o.insert(QStringLiteral("mime"), item.mime);
    o.insert(QStringLiteral("isDir"), item.isDir);
    items.append(o);
  }
  QJsonObject root;
  root.insert(QStringLiteral("cwd"), req.cwd);
  root.insert(QStringLiteral("items"), items);
  root.insert(QStringLiteral("themeDir"), themeDir());
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.flush();
  file.close();

  trackSelectionFile(path);
  return path;
}

QProcessEnvironment HandlerExec::buildEnv(const Request &req) {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("SYNCHRO_HANDLER_ID"), req.handlerId);
  env.insert(QStringLiteral("SYNCHRO_CWD"), req.cwd);
  env.insert(QStringLiteral("SYNCHRO_THEME_DIR"), themeDir());
  const QString selection = writeSelection(req);
  if (!selection.isEmpty())
    env.insert(QStringLiteral("SYNCHRO_SELECTION"), selection);
  return env;
}

bool HandlerExec::run(const Request &req) {
  m_error.clear();
  const QStringList argv = substitute(req.exec, req);
  if (argv.isEmpty()) {
    m_error = QStringLiteral("empty exec");
    return false;
  }

  const QStringList wrapped = wrapWithSession(argv);
  if (wrapped.isEmpty()) {
    m_error = QStringLiteral("empty launch argv");
    return false;
  }

  const QString program = wrapped.constFirst();
  const QStringList arguments = wrapped.mid(1);
  const QProcessEnvironment env = buildEnv(req);

  if (m_hook) {
    if (m_hook(program, arguments, env))
      return true;
    if (m_error.isEmpty())
      m_error = QStringLiteral("launch hook rejected");
    return false;
  }

  QProcess proc;
  proc.setProgram(program);
  proc.setArguments(arguments);
  proc.setProcessEnvironment(env);
  if (!req.cwd.isEmpty())
    proc.setWorkingDirectory(req.cwd);
  qint64 pid = 0;
  if (!proc.startDetached(&pid)) {
    m_error = QStringLiteral("failed to start %1").arg(program);
    return false;
  }
  return true;
}
