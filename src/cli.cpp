#include "cli.h"

#include "AgentBridge.h"
#include "AgentIntegration.h"
#include "Config.h"
#include "FileCatalog.h"
#include "HotSetWatcher.h"
#include "HandlerInstall.h"
#include "HandlerRegistry.h"
#include "Manifest.h"
#include "SearchApi.h"
#include "ThumbnailService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <sys/resource.h>
#include <utility>
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

void launcherUsage() {
  std::fprintf(stderr,
               "Usage: synchro launcher search --query <text> [--cwd "
               "<folder>] [--limit <1-100>] [--content] [--compact]\n"
               "       synchro launcher search <text> [same options]\n");
}

void searchUsage() {
  std::fprintf(
      stderr,
      "Usage: synchro search <terms> [--cwd <folder>] [--limit <1-100>]\n"
      "                      [--offset <n>] [--content] [--kind <kind>]\n"
      "                      [--extension <ext>] [--scope all|cwd]\n"
      "                      [--include-hidden] [--compact]\n"
      "       synchro search status [--cwd <folder>] [--compact]\n"
      "       synchro search describe [--compact]\n");
}

void semanticUsage() {
  std::fprintf(
      stderr,
      "Usage: synchro semantic search <description> [--cwd <folder>]\n"
      "                              [--limit <1-200>] [--no-download]\n"
      "                              [--compact]\n"
      "       synchro semantic status [--compact]\n");
}

void catalogUsage() {
  std::fprintf(stderr,
               "Usage: synchro catalog shadow status [--compact]\n"
               "       synchro catalog shadow refresh [--compact]\n"
               "       synchro catalog shadow compact [--compact]\n"
               "       synchro catalog shadow rebuild [--force] [--compact]\n");
}

void indexUsage() {
  std::fprintf(stderr,
               "Usage: synchro index serve [--no-scan]\n"
               "       synchro index once [--compact]\n"
               "       synchro index watch <folder> [--compact]\n"
               "       synchro index unwatch <folder> [--compact]\n"
               "       synchro index status [--quick] [--compact]\n");
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

QString semanticWorkerExecutable() {
  const QString override =
      qEnvironmentVariable("SYNCHRO_SEMANTIC_WORKER").trimmed();
  if (!override.isEmpty() && QFileInfo(override).isExecutable())
    return override;
  const QString installed = QStandardPaths::findExecutable(
      QStringLiteral("synchro-semantic-index"));
  if (!installed.isEmpty())
    return installed;
  const QString sibling =
      QDir(QCoreApplication::applicationDirPath())
          .filePath(QStringLiteral("synchro-semantic-index"));
  if (QFileInfo(sibling).isExecutable())
    return sibling;
#ifdef SYNCHRO_SEMANTIC_WORKER_SOURCE
  const QString source = QStringLiteral(SYNCHRO_SEMANTIC_WORKER_SOURCE);
  if (QFileInfo(source).isExecutable())
    return source;
#endif
  return {};
}

QVariantMap semanticStatus() {
  QVariantMap result;
  const QString worker = semanticWorkerExecutable();
  result.insert(QStringLiteral("workerAvailable"), !worker.isEmpty());
  result.insert(QStringLiteral("workerPath"), worker);
  if (worker.isEmpty())
    return result;
  QProcess process;
  process.start(worker, {QStringLiteral("--status")});
  if (!process.waitForStarted(500) || !process.waitForFinished(2000)) {
    process.kill();
    result.insert(QStringLiteral("error"),
                  QStringLiteral("semantic worker did not answer"));
    return result;
  }
  const QJsonDocument document =
      QJsonDocument::fromJson(process.readAllStandardOutput());
  if (document.isObject()) {
    const QVariantMap status = document.object().toVariantMap();
    for (auto it = status.cbegin(); it != status.cend(); ++it)
      result.insert(it.key(), it.value());
  } else
    result.insert(QStringLiteral("error"),
                  QString::fromUtf8(process.readAllStandardError()).trimmed());
  return result;
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
  QVariantMap options{{QStringLiteral("cwd"), cwd},
                      {QStringLiteral("selection"), selection},
                      {QStringLiteral("limit"), limit}};
  QVariantMap result = SearchApiClient::query(sql, options);
  if (result.value(QStringLiteral("transportError")).toBool()) {
    result = FileCatalog::querySync(sql, cwd, selection, limit);
    result.insert(QStringLiteral("transport"),
                  QStringLiteral("direct-fallback"));
  }
  QByteArray encoded =
      QJsonDocument(QJsonObject::fromVariantMap(result))
          .toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
  if (compact)
    encoded.append('\n');
  std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()),
              stdout);
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

int runSearchCommand(bool launcherCommand) {
  const QStringList args = QCoreApplication::arguments().mid(2);
  if (launcherCommand &&
      (args.isEmpty() || args.first() != QLatin1String("search"))) {
    launcherUsage();
    return 2;
  }
  const int firstOption = launcherCommand ? 1 : 0;
  bool compact = args.contains(QStringLiteral("--compact"));
  if (!launcherCommand && !args.isEmpty() &&
      (args.first() == QLatin1String("describe") ||
       args.first() == QLatin1String("status"))) {
    QVariantMap result;
    if (args.first() == QLatin1String("describe")) {
      for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) != QLatin1String("--compact")) {
          searchUsage();
          return 2;
        }
      }
      result = SearchApiClient::describe();
      if (result.value(QStringLiteral("transportError")).toBool()) {
        SearchApi direct;
        result = direct.describe();
        result.insert(QStringLiteral("transport"),
                      QStringLiteral("direct-fallback"));
      }
    } else {
      QVariantMap options;
      for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--cwd") && i + 1 < args.size())
          options.insert(QStringLiteral("cwd"), args.at(++i));
        else if (args.at(i) != QLatin1String("--compact")) {
          searchUsage();
          return 2;
        }
      }
      result = SearchApiClient::status(options);
      if (result.value(QStringLiteral("transportError")).toBool()) {
        SearchApi direct;
        result = direct.status(options);
        result.insert(QStringLiteral("transport"),
                      QStringLiteral("direct-fallback"));
      }
    }
    writeJson(QJsonObject::fromVariantMap(result), compact);
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
  }
  QString query;
  QString cwd = QDir::currentPath();
  int limit = launcherCommand ? 8 : 20;
  int offset = 0;
  bool content = false;
  bool includeHidden = launcherCommand;
  QStringList kinds;
  QStringList extensions;
  QString scope = QStringLiteral("all");
  for (int i = firstOption; i < args.size(); ++i) {
    const QString arg = args.at(i);
    auto next = [&](const char *option) -> QString {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "synchro: %s needs a value\n", option);
        return {};
      }
      return args.at(++i);
    };
    if (arg == QLatin1String("--query")) {
      query = next("--query");
      if (query.isNull())
        return 2;
    } else if (arg == QLatin1String("--cwd")) {
      cwd = next("--cwd");
      if (cwd.isNull())
        return 2;
    } else if (arg == QLatin1String("--limit")) {
      bool ok = false;
      const QString value = next("--limit");
      if (value.isNull())
        return 2;
      limit = value.toInt(&ok);
      if (!ok || limit < 1 || limit > 100) {
        std::fprintf(stderr, "synchro: --limit must be between 1 and 100\n");
        return 2;
      }
    } else if (arg == QLatin1String("--offset")) {
      bool ok = false;
      const QString value = next("--offset");
      if (value.isNull())
        return 2;
      offset = value.toInt(&ok);
      if (!ok || offset < 0 || offset > 10000) {
        std::fprintf(stderr, "synchro: --offset must be 0-10000\n");
        return 2;
      }
    } else if (arg == QLatin1String("--compact")) {
      compact = true;
    } else if (arg == QLatin1String("--content")) {
      content = true;
    } else if (arg == QLatin1String("--include-hidden")) {
      includeHidden = true;
    } else if (arg == QLatin1String("--kind")) {
      const QString value = next("--kind");
      if (value.isNull())
        return 2;
      kinds.append(value);
    } else if (arg == QLatin1String("--extension")) {
      const QString value = next("--extension");
      if (value.isNull())
        return 2;
      extensions.append(value);
    } else if (arg == QLatin1String("--scope")) {
      scope = next("--scope");
      if (scope.isNull())
        return 2;
      if (scope != QLatin1String("all") && scope != QLatin1String("cwd")) {
        std::fprintf(stderr, "synchro: --scope must be all or cwd\n");
        return 2;
      }
    } else if (arg == QLatin1String("--json")) {
      // JSON is the only output format.
    } else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      launcherCommand ? launcherUsage() : searchUsage();
      return 0;
    } else if (arg.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown search option %s\n",
                   qPrintable(arg));
      launcherCommand ? launcherUsage() : searchUsage();
      return 2;
    } else if (query.isEmpty()) {
      query = arg;
    } else {
      query += QLatin1Char(' ');
      query += arg;
    }
  }
  if (query.trimmed().isEmpty()) {
    launcherCommand ? launcherUsage() : searchUsage();
    return 2;
  }
  QVariantMap options{{QStringLiteral("cwd"), cwd},
                      {QStringLiteral("limit"), limit},
                      {QStringLiteral("offset"), offset},
                      {QStringLiteral("mode"),
                       content ? QStringLiteral("content")
                               : QStringLiteral("name")},
                      {QStringLiteral("includeHidden"), includeHidden},
                      {QStringLiteral("scope"), scope}};
  if (!kinds.isEmpty())
    options.insert(QStringLiteral("kinds"), kinds);
  if (!extensions.isEmpty())
    options.insert(QStringLiteral("extensions"), extensions);
  QVariantMap result = SearchApiClient::search(query, options);
  if (result.value(QStringLiteral("transportError")).toBool()) {
    result = SearchApi::executeSearch(query, options);
    result.insert(QStringLiteral("transport"),
                  QStringLiteral("direct-fallback"));
  }
  QByteArray encoded =
      QJsonDocument(QJsonObject::fromVariantMap(result))
          .toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
  if (compact)
    encoded.append('\n');
  std::fwrite(encoded.constData(), 1, static_cast<size_t>(encoded.size()),
              stdout);
  return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
}

int runLauncherCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  return runSearchCommand(true);
}

int runSearchCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  return runSearchCommand(false);
}

int runSemanticCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments().mid(2);
  if (args.isEmpty() || args.first() == QLatin1String("-h") ||
      args.first() == QLatin1String("--help") ||
      args.first() == QLatin1String("help")) {
    semanticUsage();
    return args.isEmpty() ? 2 : 0;
  }

  const QString command = args.first().toLower();
  QString cwd = QDir::currentPath();
  QStringList terms;
  int limit = 60;
  bool noDownload = false;
  for (int i = 1; i < args.size(); ++i) {
    const QString arg = args.at(i);
    auto next = [&](const char *option) -> QString {
      if (i + 1 >= args.size()) {
        std::fprintf(stderr, "synchro: %s needs a value\n", option);
        return {};
      }
      return args.at(++i);
    };
    if (arg == QLatin1String("--cwd")) {
      cwd = next("--cwd");
      if (cwd.isNull())
        return 2;
    } else if (arg == QLatin1String("--limit")) {
      bool ok = false;
      const QString value = next("--limit");
      if (value.isNull())
        return 2;
      limit = value.toInt(&ok);
      if (!ok || limit < 1 || limit > 200) {
        std::fprintf(stderr, "synchro: --limit must be between 1 and 200\n");
        return 2;
      }
    } else if (arg == QLatin1String("--no-download")) {
      noDownload = true;
    } else if (arg == QLatin1String("--compact") ||
               arg == QLatin1String("--json")) {
      // The semantic worker always returns compact JSON.
    } else if (arg.startsWith(QLatin1Char('-'))) {
      std::fprintf(stderr, "synchro: unknown semantic option %s\n",
                   qPrintable(arg));
      semanticUsage();
      return 2;
    } else {
      terms.append(arg);
    }
  }

  const QString worker = semanticWorkerExecutable();
  if (worker.isEmpty()) {
    writeJson(QJsonObject{{QStringLiteral("ok"), false},
                          {QStringLiteral("error"),
                           QStringLiteral("semantic worker is not installed")}},
              true);
    return 1;
  }
  QStringList workerArgs;
  if (command == QLatin1String("status")) {
    if (!terms.isEmpty()) {
      semanticUsage();
      return 2;
    }
    workerArgs.append(QStringLiteral("--status"));
  } else if (command == QLatin1String("search")) {
    const QString query = terms.join(QLatin1Char(' ')).trimmed();
    if (query.isEmpty()) {
      semanticUsage();
      return 2;
    }
    workerArgs = {QStringLiteral("--search"), query, QStringLiteral("--cwd"),
                  QFileInfo(cwd).absoluteFilePath(), QStringLiteral("--limit"),
                  QString::number(limit), QStringLiteral("--threads"),
                  QStringLiteral("2")};
    if (noDownload)
      workerArgs.append(QStringLiteral("--no-download"));
  } else {
    std::fprintf(stderr, "synchro: unknown semantic command '%s'\n",
                 qPrintable(command));
    semanticUsage();
    return 2;
  }

  QProcess process;
  process.start(worker, workerArgs);
  if (!process.waitForStarted(5000) || !process.waitForFinished(5 * 60 * 1000)) {
    process.kill();
    process.waitForFinished(1000);
    writeJson(QJsonObject{{QStringLiteral("ok"), false},
                          {QStringLiteral("error"),
                           QStringLiteral("semantic worker timed out")}},
              true);
    return 1;
  }
  const QByteArray output = process.readAllStandardOutput();
  const QByteArray error = process.readAllStandardError();
  if (!output.isEmpty())
    std::fwrite(output.constData(), 1, static_cast<size_t>(output.size()), stdout);
  if (!error.isEmpty())
    std::fwrite(error.constData(), 1, static_cast<size_t>(error.size()), stderr);
  return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 1;
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
  else if (command == QLatin1String("refresh"))
    result = FileCatalog::refreshShadow();
  else if (command == QLatin1String("compact"))
    result = FileCatalog::rebuildShadow(true);
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

int runIndexCli(int argc, char **argv) {
  Q_UNUSED(argc);
  Q_UNUSED(argv);
  const QStringList args = QCoreApplication::arguments().mid(2);
  if (args.isEmpty()) {
    indexUsage();
    return 2;
  }
  const QString command = args.first();
  bool compact = false;
  bool scan = true;
  bool quick = false;
  const int optionStart =
      command == QLatin1String("watch") || command == QLatin1String("unwatch")
          ? 2
          : 1;
  for (const QString &arg : args.mid(optionStart)) {
    if (arg == QLatin1String("--compact"))
      compact = true;
    else if (arg == QLatin1String("--quick"))
      quick = true;
    else if (arg == QLatin1String("--no-scan"))
      scan = false;
    else if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
      indexUsage();
      return 0;
    } else {
      std::fprintf(stderr, "synchro: unknown index option %s\n",
                   qPrintable(arg));
      indexUsage();
      return 2;
    }
  }

  const QString lockPath = FileCatalog::dbPath() + QStringLiteral(".indexd.lock");
  if (command == QLatin1String("status")) {
    QLockFile probe(lockPath);
    probe.setStaleLockTime(30000);
    const bool acquired = probe.tryLock(0);
    QVariantMap result = quick ? FileCatalog::catalogStatus()
                               : FileCatalog::shadowStatus();
    if (quick && result.contains(QStringLiteral("indexedRows")))
      result.insert(QStringLiteral("rowCount"),
                    result.value(QStringLiteral("indexedRows")));
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("running"), !acquired);
    result.insert(QStringLiteral("lockPath"), lockPath);
    result.insert(QStringLiteral("hotDirectoryHints"),
                  FileCatalog::hotDirectories(2048).size());
    Config current;
    result.insert(QStringLiteral("settings"), current.indexerSettings());
    result.insert(QStringLiteral("semantic"), semanticStatus());
    result.insert(QStringLiteral("enrichment"),
                  FileCatalog::enrichmentStatus());
    if (!acquired) {
      qint64 pid = 0;
      QString host;
      QString app;
      if (probe.getLockInfo(&pid, &host, &app)) {
        result.insert(QStringLiteral("ownerPid"), pid);
        result.insert(QStringLiteral("ownerHost"), host);
      }
    }
    if (acquired)
      probe.unlock();
    writeJson(QJsonObject::fromVariantMap(result), compact);
    return 0;
  }
  if (command == QLatin1String("once")) {
    const QVariantMap result = FileCatalog::refreshShadow();
    writeJson(QJsonObject::fromVariantMap(result), compact);
    return result.value(QStringLiteral("ok")).toBool() ? 0 : 1;
  }
  if (command == QLatin1String("watch")) {
    QVariantMap result;
    const QString path = args.size() >= 2 ? args.at(1) : QString();
    const bool ok = FileCatalog::markHotDirectory(path);
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
    if (!ok)
      result.insert(QStringLiteral("error"),
                    QStringLiteral("folder is unavailable"));
    writeJson(QJsonObject::fromVariantMap(result), compact);
    return ok ? 0 : 1;
  }
  if (command == QLatin1String("unwatch")) {
    QVariantMap result;
    const QString path = args.size() >= 2 ? args.at(1) : QString();
    const bool ok = FileCatalog::forgetHotDirectory(path);
    result.insert(QStringLiteral("ok"), ok);
    result.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
    writeJson(QJsonObject::fromVariantMap(result), compact);
    return ok ? 0 : 1;
  }
  if (command != QLatin1String("serve")) {
    indexUsage();
    return 2;
  }

  QLockFile owner(lockPath);
  owner.setStaleLockTime(30000);
  if (!owner.tryLock(0)) {
    std::fprintf(stderr, "synchro-indexd: another owner is already running\n");
    return 0;
  }

  ::setpriority(PRIO_PROCESS, 0, 15);

  SearchApi searchApi;
  if (!searchApi.start()) {
    std::fprintf(stderr, "synchro-indexd: search API: %s\n",
                 qPrintable(searchApi.lastError()));
    return 1;
  }

  FileCatalog catalog(nullptr, nullptr, true);
  const auto semanticPriorityRoots = [] {
    Config current;
    QStringList candidates;
    candidates.append(current.lastPath());
    for (const QString &pin : current.pins()) {
      const QFileInfo info(pin);
      candidates.append(info.isDir() ? info.absoluteFilePath()
                                     : info.absolutePath());
    }
    for (const QVariant &value : current.sqlBookmarks())
      candidates.append(value.toMap().value(QStringLiteral("cwd")).toString());
    candidates.append(FileCatalog::hotDirectories(24));

    QStringList roots;
    for (const QString &candidate : std::as_const(candidates)) {
      const QFileInfo info(candidate);
      const QString path = info.isDir() ? info.absoluteFilePath()
                                        : info.absolutePath();
      if (!path.isEmpty() && QFileInfo(path).isDir() && !roots.contains(path))
        roots.append(path);
    }
    // Specific working folders should win over broad home/root pins.
    std::stable_sort(roots.begin(), roots.end(),
                     [](const QString &a, const QString &b) {
                       return a.count(QLatin1Char('/')) >
                              b.count(QLatin1Char('/'));
                     });
    return roots.mid(0, 12);
  };
  const auto environmentInterval = [](const char *name, int fallback) {
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    return ok && value >= 0 ? value : fallback;
  };
  Config initialConfig;
  int neighborhood = environmentInterval(
      "SYNCHRO_INDEX_WATCH_NEIGHBORHOOD",
      initialConfig.backgroundWatchNeighborhood());
  bool backgroundEnabled = initialConfig.backgroundCatalogEnabled();
  bool recursiveEnabled =
      scan && backgroundEnabled && initialConfig.backgroundRecursiveScan();
  int scanIntervalMs = environmentInterval(
      "SYNCHRO_INDEX_SCAN_INTERVAL_MS",
      initialConfig.backgroundScanIntervalMinutes() * 60 * 1000);
  int semanticBatch = initialConfig.semanticBatchSize();
  int semanticIntervalMs = initialConfig.semanticIntervalSeconds() * 1000;
  bool semanticEnabled =
      backgroundEnabled && initialConfig.semanticImageEmbeddings();
  const int maxWatches = environmentInterval(
      "SYNCHRO_INDEX_MAX_WATCHES", initialConfig.backgroundMaxWatches());
  const int debounceMs = environmentInterval(
      "SYNCHRO_INDEX_WATCH_DEBOUNCE_MS",
      initialConfig.backgroundWatchDebounceMs());
  HotSetWatcher hotSet(maxWatches, debounceMs);
  QObject::connect(&hotSet, &HotSetWatcher::directoriesChanged, &catalog,
                   [&catalog, &backgroundEnabled](const QStringList &paths) {
                     if (backgroundEnabled)
                       catalog.reconcileDirectories(paths);
                   });
  QObject::connect(&hotSet, &HotSetWatcher::directoriesDiscovered, &hotSet,
                   [&hotSet, &neighborhood,
                    &backgroundEnabled](const QStringList &paths) {
                     if (!backgroundEnabled)
                       return;
                     for (const QString &path : paths)
                       hotSet.addNeighborhood(path, qMin(neighborhood, 32));
                   });

  QTimer hotSeed;
  hotSeed.setInterval(
      environmentInterval("SYNCHRO_INDEX_WATCH_SEED_MS", 10000));
  const auto seedHotSet = [&hotSet, &neighborhood, &backgroundEnabled] {
    if (!backgroundEnabled)
      return;
    Config current;
    QStringList candidates;
    for (const QVariant &value : current.sqlBookmarks())
      candidates.append(value.toMap().value(QStringLiteral("cwd")).toString());
    for (const QString &pin : current.pins()) {
      const QFileInfo info(pin);
      candidates.append(info.isDir() ? info.absoluteFilePath()
                                     : info.absolutePath());
    }
    candidates.append(current.lastPath());
    QStringList recent = FileCatalog::hotDirectories(32);
    std::reverse(recent.begin(), recent.end());
    candidates.append(recent);
    candidates.removeAll(QString());
    candidates.removeDuplicates();
    for (const QString &path : std::as_const(candidates))
      hotSet.addNeighborhood(path, neighborhood);
  };
  QObject::connect(&hotSeed, &QTimer::timeout, &hotSet, seedHotSet);

  QTimer recursiveScan;
  recursiveScan.setSingleShot(true);
  const int initialDelay = environmentInterval(
      "SYNCHRO_INDEX_INITIAL_SCAN_DELAY_MS", 15 * 60 * 1000);
  QObject::connect(&recursiveScan, &QTimer::timeout, &catalog,
                   [&catalog, &recursiveEnabled] {
                     if (recursiveEnabled && !catalog.indexing() &&
                         !catalog.indexedRoot().isEmpty())
                       catalog.scanTree(catalog.indexedRoot());
                   });
  QObject::connect(
      &catalog, &FileCatalog::statusChanged, &recursiveScan,
      [&catalog, &recursiveScan, &recursiveEnabled, &scanIntervalMs] {
        if (recursiveEnabled && !catalog.indexing() && scanIntervalMs > 0 &&
            !catalog.indexedRoot().isEmpty())
          recursiveScan.start(scanIntervalMs);
      });

  QProcess semanticWorker;
  QTimer semanticTimer;
  semanticTimer.setSingleShot(true);
  const auto scheduleSemantic = [&semanticTimer, &semanticEnabled,
                                 &semanticIntervalMs](int firstDelay = -1) {
    if (!semanticEnabled) {
      semanticTimer.stop();
      return;
    }
    semanticTimer.start(firstDelay >= 0 ? firstDelay : semanticIntervalMs);
  };
  QObject::connect(
      &semanticTimer, &QTimer::timeout, &semanticWorker,
      [&semanticWorker, &semanticEnabled, &semanticBatch, &scheduleSemantic,
       &semanticPriorityRoots] {
        if (!semanticEnabled ||
            semanticWorker.state() != QProcess::NotRunning)
          return;
        const QString worker = semanticWorkerExecutable();
        if (worker.isEmpty()) {
          std::fprintf(stderr,
                       "synchro-indexd: semantic worker is not installed\n");
          scheduleSemantic();
          return;
        }
        semanticWorker.setProgram(worker);
        QStringList workerArgs{
            QStringLiteral("--once"), QStringLiteral("--limit"),
            QString::number(semanticBatch), QStringLiteral("--scan-window"),
            QStringLiteral("50000"), QStringLiteral("--threads"),
            QStringLiteral("2")};
        const Config current;
        if (current.foregroundImageFacts())
          workerArgs.append(QStringLiteral("--emit-facts"));
        for (const QString &root : semanticPriorityRoots())
          workerArgs.append({QStringLiteral("--priority-root"), root});
        semanticWorker.setArguments(workerArgs);
        semanticWorker.start();
      });
  QObject::connect(
      &semanticWorker,
      qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &semanticTimer,
      [&semanticWorker, &scheduleSemantic,
       &catalog](int code, QProcess::ExitStatus) {
        const QByteArray output = semanticWorker.readAllStandardOutput().trimmed();
        const QByteArray error = semanticWorker.readAllStandardError().trimmed();
        if (code == 0) {
          const QJsonDocument result = QJsonDocument::fromJson(output);
          const QJsonArray samples =
              result.isObject()
                  ? result.object().value(QStringLiteral("factSamples")).toArray()
                  : QJsonArray{};
          for (const QJsonValue &value : samples) {
            const QJsonObject sample = value.toObject();
            const QString path = sample.value(QStringLiteral("path")).toString();
            const qint64 mtime =
                sample.value(QStringLiteral("mtime")).toVariant().toLongLong();
            const QByteArray encoded =
                sample.value(QStringLiteral("png")).toString().toLatin1();
            if (path.isEmpty() || mtime <= 0 || encoded.isEmpty() ||
                encoded.size() > 512 * 1024)
              continue;
            const QImage image =
                QImage::fromData(QByteArray::fromBase64(encoded), "PNG");
            if (image.isNull())
              continue;
            catalog.recordImageFacts(
                path, mtime,
                ThumbnailService::deterministicImageFacts(path, image));
          }
        }
        if (code != 0)
          std::fprintf(stderr, "synchro-indexd: semantic: %s%s%s\n",
                       output.constData(),
                       !output.isEmpty() && !error.isEmpty() ? " · " : "",
                       error.constData());
        scheduleSemantic();
      });

  QFileSystemWatcher configWatcher;
  const QString configPath = Config::defaultPath();
  const QString configDirectory = QFileInfo(Config::defaultPath()).absolutePath();
  QDir().mkpath(configDirectory);
  configWatcher.addPath(configDirectory);
  if (QFileInfo::exists(configPath))
    configWatcher.addPath(configPath);
  QTimer configReload;
  configReload.setSingleShot(true);
  configReload.setInterval(250);
  const auto configChanged = [&configWatcher, &configReload, &configPath] {
    if (QFileInfo::exists(configPath) &&
        !configWatcher.files().contains(configPath))
      configWatcher.addPath(configPath);
    configReload.start();
  };
  QObject::connect(&configWatcher, &QFileSystemWatcher::directoryChanged,
                   &configReload,
                   [&configChanged](const QString &) { configChanged(); });
  QObject::connect(&configWatcher, &QFileSystemWatcher::fileChanged,
                   &configReload,
                   [&configChanged](const QString &) { configChanged(); });
  const auto applyConfig = [&] {
    if (QFileInfo::exists(configPath) &&
        !configWatcher.files().contains(configPath))
      configWatcher.addPath(configPath);
    Config current;
    backgroundEnabled = current.backgroundCatalogEnabled();
    recursiveEnabled =
        scan && backgroundEnabled && current.backgroundRecursiveScan();
    neighborhood = environmentInterval(
        "SYNCHRO_INDEX_WATCH_NEIGHBORHOOD",
        current.backgroundWatchNeighborhood());
    hotSet.setMaxWatches(environmentInterval(
        "SYNCHRO_INDEX_MAX_WATCHES", current.backgroundMaxWatches()));
    hotSet.setDebounceMs(environmentInterval(
        "SYNCHRO_INDEX_WATCH_DEBOUNCE_MS",
        current.backgroundWatchDebounceMs()));
    scanIntervalMs = environmentInterval(
        "SYNCHRO_INDEX_SCAN_INTERVAL_MS",
        current.backgroundScanIntervalMinutes() * 60 * 1000);
    semanticBatch = current.semanticBatchSize();
    semanticIntervalMs = current.semanticIntervalSeconds() * 1000;
    semanticEnabled =
        backgroundEnabled && current.semanticImageEmbeddings();

    if (backgroundEnabled) {
      seedHotSet();
      hotSeed.start();
    } else {
      hotSeed.stop();
    }
    if (!recursiveEnabled)
      recursiveScan.stop();
    else if (!catalog.indexing() && !catalog.indexedRoot().isEmpty())
      recursiveScan.start(scanIntervalMs);
    if (semanticEnabled) {
      if (semanticWorker.state() == QProcess::NotRunning)
        scheduleSemantic(1000);
    } else {
      semanticTimer.stop();
      if (semanticWorker.state() != QProcess::NotRunning)
        semanticWorker.terminate();
    }
    std::fprintf(stderr,
                 "synchro-indexd: config reloaded · %d watches · %d ms "
                 "debounce · catalog %s · scan %s · embeddings %s\n",
                 hotSet.maxWatches(), hotSet.debounceMs(),
                 backgroundEnabled ? "on" : "off",
                 recursiveEnabled ? "on" : "off",
                 semanticEnabled ? "on" : "off");
  };
  QObject::connect(&configReload, &QTimer::timeout, &catalog, applyConfig);

  seedHotSet();
  if (backgroundEnabled)
    hotSeed.start();
  if (recursiveEnabled && !catalog.indexedRoot().isEmpty() && initialDelay >= 0)
    recursiveScan.start(initialDelay);
  if (semanticEnabled)
    scheduleSemantic(1000);
  std::fprintf(stderr,
               "synchro-indexd: Search1 ready · %d/%d hot watches · "
               "%d ms debounce%s%s\n",
               hotSet.watchCount(), hotSet.maxWatches(), hotSet.debounceMs(),
               recursiveEnabled ? "" : " · recursive scan off",
               semanticEnabled ? " · image embeddings on" : "");
  return QCoreApplication::instance()->exec();
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
