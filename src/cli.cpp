#include "cli.h"

#include "AgentBridge.h"
#include "AgentIntegration.h"
#include "FileCatalog.h"
#include "HandlerInstall.h"
#include "HandlerRegistry.h"
#include "Manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <unistd.h>

namespace {

void usage() {
  std::fprintf(stderr,
               "Usage: synchro handler add <git-url> [--enable] [--yes]\n"
               "       synchro handler update [id] [--yes]\n"
               "       synchro handler remove <id> [--yes]\n"
               "       synchro handler enable <id>\n"
               "       synchro handler disable <id>\n"
               "       synchro handler list [--json]\n"
               "       synchro handler validate <dir>\n");
}

void queryUsage() {
  std::fprintf(stderr, "Usage: synchro query --sql <SELECT> [--cwd <folder>] "
                       "[--selection <path>]... [--limit <1-500>] [--compact]\n"
                       "       synchro query <SELECT> [same options]\n");
}

void catalogUsage() {
  std::fprintf(stderr,
               "Usage: synchro catalog shadow status [--compact]\n"
               "       synchro catalog shadow rebuild [--force] [--compact]\n");
}

void mcpUsage() { std::fprintf(stderr, "Usage: synchro mcp [--stdio]\n"); }

void agentUsage() {
  std::fprintf(
      stderr,
      "Usage: synchro agent context [--manifest <file>] [--cwd <folder>] "
      "[--compact]\n"
      "       synchro agent query --sql <SELECT> [query options]\n"
      "       synchro agent show --sql <SELECT> [--cwd <folder>] "
      "[--label <name>] [--compact]\n"
      "       synchro agent install [--json]\n"
      "       synchro agent doctor [--json]\n");
}

int runAgentBrokered(const QStringList &command) {
  const QString executable = QCoreApplication::applicationFilePath();
  const QString systemdRun =
      QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
  QString program = executable;
  QStringList args = command;
  if (!systemdRun.isEmpty()) {
    program = systemdRun;
    args = {QStringLiteral("--user"), QStringLiteral("--pipe"),
            QStringLiteral("--wait"), QStringLiteral("--collect"),
            QStringLiteral("--quiet")};
    const QByteArray catalogHome = qgetenv("SYNCHRO_HOME");
    if (!catalogHome.isEmpty())
      args.append(QStringLiteral("--setenv=SYNCHRO_HOME=%1")
                      .arg(QString::fromLocal8Bit(catalogHome)));
    args.append(executable);
    args.append(command);
  }

  QProcess process;
  process.setProgram(program);
  process.setArguments(args);
  process.setProcessChannelMode(QProcess::SeparateChannels);
  process.start();
  if (!process.waitForStarted(5000)) {
    std::fprintf(stderr, "synchro: could not start agent catalog broker\n");
    return 1;
  }
  if (!process.waitForFinished(60000)) {
    process.kill();
    process.waitForFinished(1000);
    std::fprintf(stderr, "synchro: agent catalog broker timed out\n");
    return 1;
  }
  const QByteArray output = process.readAllStandardOutput();
  const QByteArray error = process.readAllStandardError();
  if (!output.isEmpty())
    std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()),
                stdout);
  if (!error.isEmpty())
    std::fwrite(error.constData(), 1, static_cast<size_t>(error.size()),
                stderr);
  return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
}

void writeJson(const QJsonObject &object, bool compact = false) {
  QByteArray encoded = QJsonDocument(object).toJson(
      compact ? QJsonDocument::Compact : QJsonDocument::Indented);
  if (compact)
    encoded.append('\n');
  std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()),
              stdout);
}

int agentContext(const QStringList &args) {
  QString manifest;
  QString cwd;
  bool compact = false;
  for (int i = 0; i < args.size(); ++i) {
    const QString arg = args.at(i);
    auto next = [&](const char *option) -> QString {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "synchro: %s needs a value\n", option);
        return {};
      }
      return args.at(++i);
    };
    if (arg == QLatin1String("--manifest")) {
      manifest = next("--manifest");
      if (manifest.isNull())
        return 2;
    } else if (arg == QLatin1String("--cwd")) {
      cwd = next("--cwd");
      if (cwd.isNull())
        return 2;
    } else if (arg == QLatin1String("--compact")) {
      compact = true;
    } else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      agentUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown agent context option %s\n",
                   qPrintable(arg));
      agentUsage();
      return 2;
    }
  }
  const QJsonObject context = AgentIntegration::context(manifest, cwd);
  writeJson(context, compact);
  return context.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

int agentShow(const QStringList &args) {
  QString sql;
  QString cwd = QDir::currentPath();
  QString label = QStringLiteral("agent result");
  bool compact = false;
  for (int i = 0; i < args.size(); ++i) {
    const QString arg = args.at(i);
    auto next = [&](const char *option) -> QString {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "synchro: %s needs a value\n", option);
        return {};
      }
      return args.at(++i);
    };
    if (arg == QLatin1String("--sql")) {
      sql = next("--sql");
      if (sql.isNull())
        return 2;
    } else if (arg == QLatin1String("--cwd")) {
      cwd = next("--cwd");
      if (cwd.isNull())
        return 2;
    } else if (arg == QLatin1String("--label")) {
      label = next("--label");
      if (label.isNull())
        return 2;
    } else if (arg == QLatin1String("--compact")) {
      compact = true;
    } else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      agentUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown agent show option %s\n",
                   qPrintable(arg));
      agentUsage();
      return 2;
    }
  }
  if (sql.trimmed().isEmpty()) {
    agentUsage();
    return 2;
  }

  QString validationError;
  if (!FileCatalog::validateReadOnlySql(sql, &validationError)) {
    writeJson(QJsonObject{{QStringLiteral("ok"), false},
                          {QStringLiteral("error"), validationError}},
              compact);
    return 1;
  }

  const QString executable = QCoreApplication::applicationFilePath();
  const QStringList windowArgs{QStringLiteral("--sql-query"), sql,
                               QStringLiteral("--sql-cwd"),   cwd,
                               QStringLiteral("--sql-label"), label};
  const QString systemdRun =
      QStandardPaths::findExecutable(QStringLiteral("systemd-run"));
  bool launched = false;
  QString launchError;
  if (!systemdRun.isEmpty()) {
    QStringList serviceArgs{
        QStringLiteral("--user"), QStringLiteral("--collect"),
        QStringLiteral("--quiet"), QStringLiteral("--service-type=exec")};
    const QByteArray catalogHome = qgetenv("SYNCHRO_HOME");
    if (!catalogHome.isEmpty())
      serviceArgs.append(QStringLiteral("--setenv=SYNCHRO_HOME=%1")
                             .arg(QString::fromLocal8Bit(catalogHome)));
    serviceArgs.append(executable);
    serviceArgs.append(windowArgs);
    QProcess launcher;
    launcher.setProgram(systemdRun);
    launcher.setArguments(serviceArgs);
    launcher.start();
    if (!launcher.waitForStarted(5000)) {
      launchError = QStringLiteral("could not start Omarchy app broker");
    } else if (!launcher.waitForFinished(5000)) {
      launcher.kill();
      launcher.waitForFinished(1000);
      launchError = QStringLiteral("Omarchy app broker timed out");
    } else {
      launched = launcher.exitStatus() == QProcess::NormalExit &&
                 launcher.exitCode() == 0;
      if (!launched)
        launchError =
            QString::fromLocal8Bit(launcher.readAllStandardError()).trimmed();
    }
  } else {
    launched = QProcess::startDetached(executable, windowArgs);
  }
  if (!launched && launchError.isEmpty())
    launchError = QStringLiteral("could not launch Synchro");
  const QJsonObject result{
      {QStringLiteral("ok"), launched},
      {QStringLiteral("launched"), launched},
      {QStringLiteral("cwd"),
       QDir::cleanPath(QFileInfo(cwd).absoluteFilePath())},
      {QStringLiteral("label"), label},
      {QStringLiteral("error"), launched ? QString() : launchError},
  };
  writeJson(result, compact);
  return launched ? 0 : 1;
}

int agentInstall(const QStringList &args) {
  bool json = false;
  for (const QString &arg : args) {
    if (arg == QLatin1String("--json"))
      json = true;
    else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      agentUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown agent install option %s\n",
                   qPrintable(arg));
      return 2;
    }
  }
  const AgentSkillStatus status = AgentIntegration::installSkill();
  if (json) {
    writeJson(status.toJson());
  } else {
    std::printf("Synchro agent skill: %s\n",
                status.ok() ? "ready" : "needs attention");
    std::printf("  source       %s\n", qPrintable(status.sourceDir));
    std::printf("  installed    %lld\n",
                static_cast<long long>(status.installed.size()));
    std::printf("  already ready %lld\n",
                static_cast<long long>(status.present.size()));
    for (const QString &path : status.conflicts)
      std::printf("  conflict     %s\n", qPrintable(path));
    for (const QString &error : status.errors)
      std::printf("  error        %s\n", qPrintable(error));
  }
  return status.ok() ? 0 : 1;
}

int agentDoctor(const QStringList &args) {
  bool json = false;
  for (const QString &arg : args) {
    if (arg == QLatin1String("--json"))
      json = true;
    else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      agentUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown agent doctor option %s\n",
                   qPrintable(arg));
      return 2;
    }
  }
  const QJsonObject result = AgentIntegration::doctor();
  if (json) {
    writeJson(result);
  } else {
    const QJsonObject omarchy =
        result.value(QStringLiteral("omarchy")).toObject();
    const QJsonObject agent =
        result.value(QStringLiteral("defaultAgent")).toObject();
    const QJsonObject skill = result.value(QStringLiteral("skill")).toObject();
    std::printf("Synchro agent bridge: %s\n",
                result.value(QStringLiteral("ok")).toBool()
                    ? "ready"
                    : "needs attention");
    std::printf("  Omarchy       %s\n",
                omarchy.value(QStringLiteral("available")).toBool()
                    ? "ready"
                    : "missing");
    std::printf(
        "  default agent %s%s\n",
        qPrintable(agent.value(QStringLiteral("name")).toString()),
        agent.value(QStringLiteral("available")).toBool() ? "" : " (missing)");
    std::printf("  shared skill  %s\n",
                skill.value(QStringLiteral("ok")).toBool()
                    ? "ready"
                    : "run: synchro agent install");
    std::printf("  catalog CLI   ready (required)\n");
    std::printf("  MCP           optional\n");
  }
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

bool interactive() { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

bool confirmCli(const QString &prompt, bool yes) {
  if (yes)
    return true;
  if (!interactive()) {
    std::fprintf(stderr, "synchro: refusing to continue without confirmation; "
                         "pass --yes\n");
    return false;
  }
  std::fprintf(stdout, "%s [y/N] ", qPrintable(prompt));
  std::fflush(stdout);
  char buf[32];
  if (!std::fgets(buf, sizeof(buf), stdin))
    return false;
  return buf[0] == 'y' || buf[0] == 'Y';
}

void printUnsandboxedWarning(const QString &url) {
  std::fprintf(stderr,
               "\n"
               "⚠️  Handlers run as arbitrary, unsandboxed code inside Synchro\n"
               "  (in-process QML) or as detached peer processes. Only add\n"
               "  repos you trust, and review the code before you enable it.\n"
               "\n"
               "      URL: %s\n"
               "\n",
               qPrintable(url));
}

int cmdList(const QStringList &args) {
  bool json = false;
  for (const QString &a : args) {
    if (a == QLatin1String("--json"))
      json = true;
    else {
      std::fprintf(stderr, "synchro: unknown argument %s\n", qPrintable(a));
      usage();
      return 2;
    }
  }
  HandlerRegistry reg;
  reg.scan();
  const auto handlers = reg.handlers();
  if (json) {
    QJsonArray arr;
    for (const auto &h : handlers) {
      QJsonObject o;
      o.insert(QStringLiteral("id"), h.manifest.id);
      o.insert(QStringLiteral("name"), h.manifest.name);
      o.insert(QStringLiteral("version"), h.manifest.version);
      o.insert(QStringLiteral("kinds"),
               QJsonArray::fromStringList(h.manifest.kinds));
      o.insert(QStringLiteral("enabled"), h.enabled);
      o.insert(QStringLiteral("firstParty"), h.firstParty);
      o.insert(QStringLiteral("sourceDir"), h.sourceDir);
      arr.append(o);
    }
    const QByteArray out = QJsonDocument(arr).toJson(QJsonDocument::Indented);
    std::fwrite(out.constData(), 1, static_cast<size_t>(out.size()), stdout);
    return 0;
  }
  for (const auto &h : handlers) {
    std::printf("%-28s %-16s %-8s %-12s %s\n", qPrintable(h.manifest.id),
                qPrintable(h.manifest.kinds.join(QLatin1Char(','))),
                h.enabled ? "enabled" : "disabled",
                h.firstParty ? "first-party" : "user", qPrintable(h.sourceDir));
  }
  return 0;
}

int cmdValidate(const QStringList &args) {
  if (args.size() != 1) {
    usage();
    return 2;
  }
  const QString dir = QFileInfo(args.constFirst()).absoluteFilePath();
  HandlerRegistry scratch;
  const QString first = QDir::cleanPath(scratch.firstPartyDir());
  const QString clean = QDir::cleanPath(dir);
  const bool firstParty =
      clean == first || clean.startsWith(first + QLatin1Char('/'));
  const ManifestValidation v = validateManifestDir(dir, firstParty);
  if (!v.ok) {
    for (const QString &e : v.errors)
      std::fprintf(stderr, "synchro: %s\n", qPrintable(e));
    return 1;
  }
  std::printf("ok %s\n", qPrintable(v.manifest.id));
  return 0;
}

int cmdAdd(const QStringList &args) {
  QString url;
  bool enable = false;
  bool yes = false;
  for (const QString &a : args) {
    if (a == QLatin1String("--enable"))
      enable = true;
    else if (a == QLatin1String("--yes") || a == QLatin1String("-y"))
      yes = true;
    else if (a.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown add option %s\n", qPrintable(a));
      usage();
      return 2;
    } else if (url.isEmpty())
      url = a;
    else {
      std::fprintf(stderr, "synchro: unexpected argument %s\n", qPrintable(a));
      usage();
      return 2;
    }
  }
  if (url.isEmpty()) {
    usage();
    return 2;
  }
  if (!yes)
    printUnsandboxedWarning(url);

  HandlerRegistry reg;
  HandlerInstall install(&reg);
  install.setAssumeYes(yes);
  install.setEnableAfterAdd(enable);
  install.setPrompt([&](const QString &q) { return confirmCli(q, yes); });
  if (!install.add(url)) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(install.lastError()));
    return 1;
  }
  std::printf("%s\n", qPrintable(install.lastMessage()));
  if (!enable && !yes && interactive() &&
      confirmCli(QStringLiteral("Enable '%1' now?").arg(install.lastId()),
                 false)) {
    QString err;
    reg.scan();
    if (!reg.setEnabled(install.lastId(), true, &err)) {
      std::fprintf(stderr, "synchro: %s\n", qPrintable(err));
      return 1;
    }
    std::printf("Enabled %s\n", qPrintable(install.lastId()));
  }
  return 0;
}

int cmdUpdate(const QStringList &args) {
  QString id;
  bool yes = false;
  for (const QString &a : args) {
    if (a == QLatin1String("--yes") || a == QLatin1String("-y"))
      yes = true;
    else if (a.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown update option %s\n",
                   qPrintable(a));
      usage();
      return 2;
    } else if (id.isEmpty())
      id = a;
    else {
      usage();
      return 2;
    }
  }
  HandlerRegistry reg;
  HandlerInstall install(&reg);
  install.setAssumeYes(yes);
  install.setPrompt([&](const QString &q) { return confirmCli(q, yes); });
  install.setReviewSink([](const QString &diff) {
    std::fprintf(stdout, "%s", qPrintable(diff));
    if (!diff.endsWith(QLatin1Char('\n')))
      std::fputc('\n', stdout);
    std::fflush(stdout);
  });
  if (!install.update(id)) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(install.lastError()));
    if (!install.lastMessage().isEmpty())
      std::printf("%s\n", qPrintable(install.lastMessage()));
    return 1;
  }
  if (!install.lastMessage().isEmpty())
    std::printf("%s\n", qPrintable(install.lastMessage()));
  return 0;
}

int cmdRemove(const QStringList &args) {
  QString id;
  bool yes = false;
  for (const QString &a : args) {
    if (a == QLatin1String("--yes") || a == QLatin1String("-y"))
      yes = true;
    else if (a.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown remove option %s\n",
                   qPrintable(a));
      usage();
      return 2;
    } else if (id.isEmpty())
      id = a;
    else {
      usage();
      return 2;
    }
  }
  if (id.isEmpty()) {
    usage();
    return 2;
  }
  HandlerRegistry reg;
  HandlerInstall install(&reg);
  install.setAssumeYes(yes);
  install.setPrompt([&](const QString &q) { return confirmCli(q, yes); });
  if (!install.remove(id)) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(install.lastError()));
    return 1;
  }
  std::printf("%s\n", qPrintable(install.lastMessage()));
  return 0;
}

int cmdEnable(const QStringList &args, bool enable) {
  if (args.size() != 1) {
    usage();
    return 2;
  }
  const QString id = args.constFirst();
  HandlerRegistry reg;
  reg.scan();
  QString err;
  if (!reg.setEnabled(id, enable, &err)) {
    std::fprintf(stderr, "synchro: %s\n", qPrintable(err));
    return 1;
  }
  std::printf("%s %s\n", enable ? "Enabled" : "Disabled", qPrintable(id));
  return 0;
}

} // namespace

int runHandlerCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments();
  // argv: synchro handler <cmd> ...
  if (args.size() < 3) {
    usage();
    return 2;
  }
  const QString cmd = args.at(2);
  const QStringList rest = args.mid(3);
  if (cmd == QLatin1String("list"))
    return cmdList(rest);
  if (cmd == QLatin1String("validate"))
    return cmdValidate(rest);
  if (cmd == QLatin1String("add"))
    return cmdAdd(rest);
  if (cmd == QLatin1String("update"))
    return cmdUpdate(rest);
  if (cmd == QLatin1String("remove") || cmd == QLatin1String("rm"))
    return cmdRemove(rest);
  if (cmd == QLatin1String("enable"))
    return cmdEnable(rest, true);
  if (cmd == QLatin1String("disable"))
    return cmdEnable(rest, false);
  if (cmd == QLatin1String("-h") || cmd == QLatin1String("--help") ||
      cmd == QLatin1String("help")) {
    usage();
    return 0;
  }
  std::fprintf(stderr, "synchro: unknown handler command '%s'\n",
               qPrintable(cmd));
  usage();
  return 2;
}

int runQueryCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments().mid(2);
  QString sql;
  QString cwd = QDir::currentPath();
  QStringList selection;
  int limit = 200;
  bool compact = false;
  for (int i = 0; i < args.size(); ++i) {
    const QString arg = args.at(i);
    auto next = [&](const char *option) -> QString {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "synchro: %s needs a value\n", option);
        return {};
      }
      return args.at(++i);
    };
    if (arg == QLatin1String("--sql")) {
      sql = next("--sql");
      if (sql.isNull())
        return 2;
    } else if (arg == QLatin1String("--cwd")) {
      cwd = next("--cwd");
      if (cwd.isNull())
        return 2;
    } else if (arg == QLatin1String("--selection") ||
               arg == QLatin1String("--select")) {
      const QString path = next("--selection");
      if (path.isNull())
        return 2;
      selection.append(path);
    } else if (arg == QLatin1String("--limit")) {
      bool ok = false;
      const QString value = next("--limit");
      if (value.isNull())
        return 2;
      limit = value.toInt(&ok);
      if (!ok || limit < 1 || limit > 500) {
        std::fprintf(stderr, "synchro: --limit must be between 1 and 500\n");
        return 2;
      }
    } else if (arg == QLatin1String("--compact")) {
      compact = true;
    } else if (arg == QLatin1String("--json")) {
      // JSON is the only output format; accept this for explicit scripts.
    } else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      queryUsage();
      return 0;
    } else if (arg.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown query option %s\n",
                   qPrintable(arg));
      queryUsage();
      return 2;
    } else if (sql.isEmpty()) {
      sql = arg;
    } else {
      std::fprintf(stderr, "synchro: unexpected query argument %s\n",
                   qPrintable(arg));
      queryUsage();
      return 2;
    }
  }
  if (sql == QLatin1String("-")) {
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
      std::fprintf(stderr, "synchro: could not read SQL from stdin\n");
      return 1;
    }
    sql = QString::fromUtf8(input.readAll());
  }
  if (sql.trimmed().isEmpty()) {
    queryUsage();
    return 2;
  }
  const QVariantMap result = FileCatalog::querySync(sql, cwd, selection, limit);
  QByteArray encoded =
      QJsonDocument(QJsonObject::fromVariantMap(result))
          .toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
  if (compact)
    encoded.append('\n');
  std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()),
              stdout);
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

int runCatalogCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments().mid(2);
  if (args.size() < 2 || args.first() != QLatin1String("shadow")) {
    catalogUsage();
    return 2;
  }
  const QString command = args.at(1);
  bool force = false;
  bool compact = false;
  for (const QString &arg : args.mid(2)) {
    if (arg == QLatin1String("--force"))
      force = true;
    else if (arg == QLatin1String("--compact"))
      compact = true;
    else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      catalogUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown catalog option %s\n",
                   qPrintable(arg));
      catalogUsage();
      return 2;
    }
  }
  QVariantMap result;
  if (command == QLatin1String("status"))
    result = FileCatalog::shadowStatus();
  else if (command == QLatin1String("rebuild"))
    result = FileCatalog::rebuildShadow(force);
  else {
    std::fprintf(stderr, "synchro: unknown catalog shadow command '%s'\n",
                 qPrintable(command));
    catalogUsage();
    return 2;
  }
  if (command == QLatin1String("status"))
    result.insert(QStringLiteral("ok"), true);
  QByteArray encoded =
      QJsonDocument(QJsonObject::fromVariantMap(result))
          .toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
  if (compact)
    encoded.append('\n');
  std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()),
              stdout);
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

int runMcpCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments().mid(2);
  for (const QString &arg : args) {
    if (arg == QLatin1String("--stdio"))
      continue;
    if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      mcpUsage();
      return 0;
    }
    std::fprintf(stderr, "synchro: unknown mcp option %s\n", qPrintable(arg));
    mcpUsage();
    return 2;
  }
  return AgentBridge::runStdio();
}

int runAgentCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments();
  if (args.size() < 3) {
    agentUsage();
    return 2;
  }
  const QString command = args.at(2);
  const QStringList rest = args.mid(3);
  if (command == QLatin1String("context"))
    return agentContext(rest);
  if (command == QLatin1String("query"))
    return runAgentBrokered(QStringList{QStringLiteral("query")} + rest);
  if (command == QLatin1String("show"))
    return agentShow(rest);
  if (command == QLatin1String("install"))
    return agentInstall(rest);
  if (command == QLatin1String("doctor"))
    return agentDoctor(rest);
  if (command == QLatin1String("-h") || command == QLatin1String("--help") ||
      command == QLatin1String("help")) {
    agentUsage();
    return 0;
  }
  std::fprintf(stderr, "synchro: unknown agent command '%s'\n",
               qPrintable(command));
  agentUsage();
  return 2;
}
