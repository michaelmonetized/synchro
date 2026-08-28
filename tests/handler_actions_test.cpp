#include "CoreVerbs.h"
#include "HandlerActions.h"
#include "HandlerExec.h"
#include "HandlerRegistry.h"
#include "Manifest.h"
#include "VolumeStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {

bool writeText(const QString &path, const QByteArray &body) {
  QFileInfo fi(path);
  QDir().mkpath(fi.absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(body);
  return true;
}

Manifest::Item item(const QString &path, const QString &mime,
                    bool isDir = false) {
  Manifest::Item i;
  i.path = path;
  i.uri = QUrl::fromLocalFile(path);
  i.mime = mime;
  i.isDir = isDir;
  return i;
}

} // namespace

class HandlerActionsTest : public QObject {
  Q_OBJECT

private slots:
  void terminalExecShape();
  void terminalDisabledOnVirtual();
  void runActionTerminalDisabledOnVirtual();
  void runActionAgentDisabledOnVirtual();
  void agentUsesOmarchyPromptAndSelectionContext();
  void trashMovesToXdgTrash();
  void trashRefusesHome();
  void runActionById();
  void actionMatchesIncludesCopyAs();
  void ejectRefusesSystemAndRunsHook();
  void ejectOnlyMatchesRemovableVolume();
};

void HandlerActionsTest::terminalExecShape() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.action.terminal")));

  HandlerExec exec;
  QString program;
  QStringList args;
  exec.setLaunchHook(
      [&](const QString &p, const QStringList &a, const QProcessEnvironment &) {
        program = p;
        args = a;
        return true;
      });
  HandlerActions actions(&reg, &exec);
  const QString file = tmp.filePath(QStringLiteral("README.md"));
  QVERIFY(writeText(file, QByteArrayLiteral("hi\n")));
  QVERIFY(actions.runTerminal({item(file, QStringLiteral("text/markdown"))},
                              tmp.path()));
  const QStringList all = QStringList{program} + args;
  QVERIFY(all.contains(QStringLiteral("xdg-terminal-exec")) ||
          program.endsWith(QStringLiteral("xdg-terminal-exec")));
  bool sawDir = false;
  for (const QString &a : all) {
    if (a.startsWith(QStringLiteral("--dir="))) {
      sawDir = true;
      QVERIFY(a.contains(tmp.path()) ||
              a.contains(QFileInfo(file).absolutePath()));
    }
  }
  QVERIFY(sawDir);
  QVERIFY(!all.join(QLatin1Char(' '))
               .contains(QStringLiteral("omarchy-launch-terminal")));
}

void HandlerActionsTest::terminalDisabledOnVirtual() {
  HandlerRegistry reg;
  HandlerExec exec;
  HandlerActions actions(&reg, &exec);
  QVERIFY(!actions.runTerminal({}, QStringLiteral("trash://")));
  QVERIFY(actions.lastError().contains(QStringLiteral("virtual")));
}

void HandlerActionsTest::runActionTerminalDisabledOnVirtual() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();

  HandlerExec exec;
  bool launched = false;
  exec.setLaunchHook(
      [&](const QString &, const QStringList &, const QProcessEnvironment &) {
        launched = true;
        return true;
      });
  HandlerActions actions(&reg, &exec);
  QVERIFY(!actions.runAction(QStringLiteral("synchro.action.terminal"), {},
                             QStringLiteral("trash://")));
  QVERIFY(actions.lastError().contains(QStringLiteral("virtual")));
  QVERIFY(!launched);
  QVERIFY(!actions.runAction(QStringLiteral("synchro.action.terminal"), {},
                             QStringLiteral("recent://")));
  QVERIFY(actions.lastError().contains(QStringLiteral("virtual")));
  QVERIFY(!launched);
}

void HandlerActionsTest::runActionAgentDisabledOnVirtual() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.action.agent")));

  HandlerExec exec;
  bool launched = false;
  exec.setLaunchHook(
      [&](const QString &, const QStringList &, const QProcessEnvironment &) {
        launched = true;
        return true;
      });
  HandlerActions actions(&reg, &exec);
  QVERIFY(!actions.runAction(QStringLiteral("synchro.action.agent"), {},
                             QStringLiteral("trash://")));
  QVERIFY(actions.lastError().contains(QStringLiteral("virtual")));
  QVERIFY(!launched);
  QVERIFY(!actions.runAction(QStringLiteral("synchro.action.agent"), {},
                             QStringLiteral("recent://")));
  QVERIFY(!launched);
}

void HandlerActionsTest::agentUsesOmarchyPromptAndSelectionContext() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();

  const QString project = tmp.filePath(QStringLiteral("project"));
  QVERIFY(QDir().mkpath(project));
  HandlerExec exec;
  QStringList launch;
  QProcessEnvironment environment;
  exec.setLaunchHook([&](const QString &program, const QStringList &args,
                         const QProcessEnvironment &env) {
    launch = QStringList{program} + args;
    environment = env;
    return true;
  });
  HandlerActions actions(&reg, &exec);
  QVERIFY2(actions.runAction(
               QStringLiteral("synchro.action.agent"),
               {item(project, QStringLiteral("inode/directory"), true)},
               tmp.path()),
           qPrintable(actions.lastError()));
  const QString command = launch.join(QLatin1Char(' '));
  QVERIFY(command.contains(QStringLiteral("omarchy")));
  QVERIFY(command.contains(QStringLiteral("agent")));
  QVERIFY(command.contains(QStringLiteral("prompt")));
  QVERIFY(command.contains(QStringLiteral("SYNCHRO_BIN")));
  QVERIFY(command.contains(QStringLiteral("agent context --compact")));
  QVERIFY(command.contains(QStringLiteral("MCP is optional")));
  QCOMPARE(environment.value(QStringLiteral("SYNCHRO_CWD")), project);
  QVERIFY(!environment.value(QStringLiteral("SYNCHRO_BIN")).isEmpty());
  const QString manifest =
      environment.value(QStringLiteral("SYNCHRO_SELECTION"));
  QVERIFY(!manifest.isEmpty());
  QFile selection(manifest);
  QVERIFY(selection.open(QIODevice::ReadOnly));
  QVERIFY(selection.readAll().contains(project.toUtf8()));
}

void HandlerActionsTest::trashMovesToXdgTrash() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString trash = tmp.filePath(QStringLiteral("Trash"));
  CoreVerbs::setTrashRootOverride(trash);
  const QString file = tmp.filePath(QStringLiteral("gone.txt"));
  QVERIFY(writeText(file, QByteArrayLiteral("x")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();
  const auto rec = reg.handler(QStringLiteral("synchro.action.trash"));
  QCOMPARE(rec.manifest.runtime(QStringLiteral("action")),
           QStringLiteral("core"));

  HandlerExec exec;
  HandlerActions actions(&reg, &exec);
  QVERIFY2(actions.runTrash({item(file, QStringLiteral("text/plain"))}),
           qPrintable(actions.lastError()));
  QVERIFY(!QFileInfo::exists(file));
  QVERIFY(QFileInfo::exists(
      QDir(trash).filePath(QStringLiteral("files/gone.txt"))));
  QVERIFY(QFileInfo::exists(
      QDir(trash).filePath(QStringLiteral("info/gone.txt.trashinfo"))));
  QFile info(QDir(trash).filePath(QStringLiteral("info/gone.txt.trashinfo")));
  QVERIFY(info.open(QIODevice::ReadOnly));
  const QByteArray body = info.readAll();
  QVERIFY(body.contains("[Trash Info]"));
  QVERIFY(body.contains("Path="));
  QVERIFY(body.contains("DeletionDate="));
  CoreVerbs::setTrashRootOverride(QString());
}

void HandlerActionsTest::trashRefusesHome() {
  CoreVerbs::setTrashRootOverride(
      QDir::temp().filePath(QStringLiteral("synchro-trash-test")));
  QString err;
  QVERIFY(CoreVerbs::isForbiddenTrashPath(QDir::homePath()));
  QVERIFY(!CoreVerbs::trash({QDir::homePath()}, &err));
  QVERIFY(err.contains(QStringLiteral("refusing")));
  CoreVerbs::setTrashRootOverride(QString());
}

void HandlerActionsTest::runActionById() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();

  HandlerExec exec;
  QString program;
  exec.setLaunchHook(
      [&](const QString &p, const QStringList &, const QProcessEnvironment &) {
        program = p;
        return true;
      });
  HandlerActions actions(&reg, &exec);
  QVERIFY2(actions.runAction(
               QStringLiteral("synchro.action.terminal"),
               {item(tmp.path(), QStringLiteral("inode/directory"), true)},
               tmp.path()),
           qPrintable(actions.lastError()));
  QVERIFY(program.contains(QStringLiteral("xdg-terminal-exec")) ||
          program.endsWith(QStringLiteral("xdg-terminal-exec")) ||
          !program.isEmpty());

  QVERIFY(
      !actions.runAction(QStringLiteral("synchro.open.xdg"), {}, tmp.path()));
  QVERIFY(actions.lastError().contains(QStringLiteral("not an action")));
}

void HandlerActionsTest::actionMatchesIncludesCopyAs() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.action.copy-as")));

  HandlerExec exec;
  HandlerActions actions(&reg, &exec);
  const QString file = tmp.filePath(QStringLiteral("notes.md"));
  QVERIFY(writeText(file, QByteArrayLiteral("hi\n")));
  const auto hits =
      actions.actionMatches({item(file, QStringLiteral("text/markdown"))});
  QStringList ids;
  for (const auto &m : hits)
    ids.append(m.id);
  QVERIFY(ids.contains(QStringLiteral("synchro.action.copy-as")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.open-with")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.trash")));
  QVERIFY(!ids.contains(QStringLiteral("synchro.action.eject")));
}

void HandlerActionsTest::ejectRefusesSystemAndRunsHook() {
  VolumeStore::Volume sys;
  sys.label = QStringLiteral("system");
  sys.mountPoint = QStringLiteral("/");
  sys.extra = false;
  VolumeStore::Volume usb;
  usb.label = QStringLiteral("stick");
  usb.mountPoint = QStringLiteral("/run/media/ryan/stick");
  usb.extra = true;
  usb.removable = true;
  VolumeStore::instance().setInventoryForTest({sys, usb});

  HandlerRegistry reg;
  HandlerExec exec;
  HandlerActions actions(&reg, &exec);
  QVERIFY(!actions.runEject(
      {item(QStringLiteral("/"), QStringLiteral("inode/directory"), true)},
      QStringLiteral("/")));
  QVERIFY(actions.lastError().contains(QStringLiteral("system")));

  bool saw = false;
  VolumeStore::instance().setEjectHook(
      [&](const VolumeStore::Volume &v, QString *) {
        saw = v.mountPoint == usb.mountPoint;
        return true;
      });
  QVERIFY(actions.runEject(
      {item(usb.mountPoint, QStringLiteral("inode/directory"), true)},
      usb.mountPoint));
  QVERIFY(saw);
  VolumeStore::instance().setEjectHook({});
  VolumeStore::instance().setInventoryForTest({});
}

void HandlerActionsTest::ejectOnlyMatchesRemovableVolume() {
  VolumeStore::Volume sys;
  sys.label = QStringLiteral("/");
  sys.mountPoint = QStringLiteral("/");
  VolumeStore::Volume usb;
  usb.label = QStringLiteral("stick");
  usb.mountPoint = QStringLiteral("/run/media/ryan/stick");
  usb.extra = true;
  usb.removable = true;
  VolumeStore::instance().setInventoryForTest({sys, usb});

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.filePath(QStringLiteral("none")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.setScanEnv(false);
  reg.scan();
  HandlerExec exec;
  HandlerActions actions(&reg, &exec);

  const QString file = tmp.filePath(QStringLiteral("notes.md"));
  QVERIFY(writeText(file, QByteArrayLiteral("hi\n")));
  QVERIFY(
      !HandlerActions::canEject({item(file, QStringLiteral("text/markdown"))}));
  QVERIFY(!HandlerActions::canEject(
      {item(QStringLiteral("/"), QStringLiteral("inode/directory"), true)}));
  QVERIFY(HandlerActions::canEject(
      {item(usb.mountPoint, QStringLiteral("inode/directory"), true)}));

  QStringList onFile;
  for (const auto &m :
       actions.actionMatches({item(file, QStringLiteral("text/markdown"))}))
    onFile.append(m.id);
  QVERIFY(!onFile.contains(QStringLiteral("synchro.action.eject")));

  QStringList onUsb;
  for (const auto &m : actions.actionMatches(
           {item(usb.mountPoint, QStringLiteral("inode/directory"), true)}))
    onUsb.append(m.id);
  QVERIFY(onUsb.contains(QStringLiteral("synchro.action.eject")));

  VolumeStore::instance().setInventoryForTest({});
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  HandlerActionsTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "handler_actions_test.moc"
