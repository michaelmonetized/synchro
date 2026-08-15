#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HandlerExec.h"
#include "KeyMachine.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "RecentStore.h"
#include "XdgOpen.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <thread>

namespace {

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

bool writeFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.write("hello", 5);
  return true;
}

int findRow(const DirectoryModel &model, const QString &name) {
  for (int i = 0; i < model.rowCount(); ++i) {
    if (model.data(model.index(i, 0), DirectoryModel::NameRole).toString() ==
        name)
      return i;
  }
  return -1;
}

QString canon(const QString &path) {
  const QString c = QFileInfo(path).canonicalFilePath();
  return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

HandlerExec::Request singleFile(const QString &path,
                                const QString &exec = QStringLiteral(
                                    "xdg-open %f")) {
  HandlerExec::Request req;
  req.exec = exec;
  req.handlerId = QStringLiteral("synchro.open.xdg");
  req.handlerDir = QStringLiteral("/tmp/handlers/synchro.open.xdg");
  req.cwd = QFileInfo(path).absolutePath();
  HandlerExec::Item item;
  item.path = path;
  item.uri = QUrl::fromLocalFile(path);
  item.mime = QStringLiteral("text/plain");
  req.items.append(item);
  return req;
}

void dropSelection(const QProcessEnvironment &env) {
  const QString sel = env.value(QStringLiteral("SYNCHRO_SELECTION"));
  if (!sel.isEmpty())
    QFile::remove(sel);
}

bool writeXdgManifest(const QString &root, const QByteArray &openBlock) {
  if (!QDir(root).mkpath(QStringLiteral("synchro.open.xdg")))
    return false;
  QFile man(QDir(root).filePath(QStringLiteral("synchro.open.xdg/manifest.json")));
  if (!man.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  man.write("{\n"
            "  \"schemaVersion\": 1,\n"
            "  \"id\": \"synchro.open.xdg\",\n"
            "  \"name\": \"Open\",\n"
            "  \"version\": \"1.0.0\",\n"
            "  \"kinds\": [\"open\"],\n"
            "  \"entryPoints\": {},\n"
            "  \"open\": ");
  man.write(openBlock);
  man.write("\n}\n");
  return true;
}

} // namespace

class HandlerExecTest : public QObject {
  Q_OBJECT

private slots:
  void substituteFAndD();
  void substituteAllAndUris();
  void substituteHandlerDirAndPercent();
  void quotedTokensAndNoShell();
  void wrapPrefersUwsmThenSetsidFallback();
  void runUsesHookNotRealExec();
  void runSetsHandlerEnv();
  void hookFailureSetsError();
  void loadFirstPartyManifest();
  void loadCustomManifestExec();
  void mimeForTextAndDir();
  void recentAppendAndCompact();
  void recentDefaultPathIsXdgDataHome();
  void recentTwoStoresSerialize();
  void enterFileOpensViaManifestAndRecords();
  void enterDirDoesNotOpen();
};

void HandlerExecTest::substituteFAndD() {
  const QString path = QStringLiteral("/tmp/foo/README.md");
  const auto argv =
      HandlerExec::substitute(QStringLiteral("xdg-open %f"), singleFile(path));
  QCOMPARE(argv, QStringList({QStringLiteral("xdg-open"), path}));

  const auto dirArgv =
      HandlerExec::substitute(QStringLiteral("term --dir=%d"), singleFile(path));
  QCOMPARE(dirArgv, QStringList({QStringLiteral("term"),
                                 QStringLiteral("--dir=/tmp/foo")}));
}

void HandlerExecTest::substituteAllAndUris() {
  HandlerExec::Request req;
  req.exec = QStringLiteral("open %F");
  req.cwd = QStringLiteral("/tmp");
  HandlerExec::Item a;
  a.path = QStringLiteral("/tmp/a.txt");
  HandlerExec::Item b;
  b.path = QStringLiteral("/tmp/b.txt");
  req.items << a << b;
  QCOMPARE(HandlerExec::substitute(req.exec, req),
           QStringList({QStringLiteral("open"), a.path, b.path}));

  req.exec = QStringLiteral("open %U");
  const QStringList uris = HandlerExec::substitute(req.exec, req);
  QCOMPARE(uris.size(), 3);
  QCOMPARE(uris.at(0), QStringLiteral("open"));
  QVERIFY(uris.at(1).startsWith(QStringLiteral("file://")));
  QVERIFY(uris.at(1).contains(QStringLiteral("a.txt")));

  req.exec = QStringLiteral("open %u");
  req.items = {a};
  const QStringList one = HandlerExec::substitute(req.exec, req);
  QCOMPARE(one.size(), 2);
  QCOMPARE(one.at(1), QUrl::fromLocalFile(a.path).toString(QUrl::FullyEncoded));
}

void HandlerExecTest::substituteHandlerDirAndPercent() {
  auto req = singleFile(QStringLiteral("/tmp/x"),
                        QStringLiteral("run ${handlerDir}/tool %%f"));
  const auto argv = HandlerExec::substitute(req.exec, req);
  QCOMPARE(argv, QStringList({QStringLiteral("run"),
                              QStringLiteral("/tmp/handlers/synchro.open.xdg/"
                                             "tool"),
                              QStringLiteral("%f")}));
}

void HandlerExecTest::quotedTokensAndNoShell() {
  const QString evil = QStringLiteral("/tmp/foo; rm -rf /");
  const auto argv =
      HandlerExec::substitute(QStringLiteral("xdg-open %f"), singleFile(evil));
  QCOMPARE(argv.size(), 2);
  QCOMPARE(argv.at(0), QStringLiteral("xdg-open"));
  QCOMPARE(argv.at(1), evil);
  QVERIFY(!argv.join(QLatin1Char(' ')).contains(QStringLiteral("sh -c")));

  auto req = singleFile(QStringLiteral("/tmp/spaced file.txt"),
                        QStringLiteral("\"xdg-open\" \"%f\""));
  QCOMPARE(HandlerExec::substitute(req.exec, req),
           QStringList({QStringLiteral("xdg-open"),
                        QStringLiteral("/tmp/spaced file.txt")}));
}

void HandlerExecTest::wrapPrefersUwsmThenSetsidFallback() {
  const QStringList inner{QStringLiteral("xdg-open"),
                          QStringLiteral("/tmp/a")};
  QCOMPARE(HandlerExec::wrapWithSession(inner, QStringLiteral("/bin/setsid"),
                                        QStringLiteral("/bin/uwsm-app")),
           QStringList({QStringLiteral("/bin/setsid"),
                        QStringLiteral("/bin/uwsm-app"), QStringLiteral("--"),
                        QStringLiteral("xdg-open"), QStringLiteral("/tmp/a")}));
  QCOMPARE(HandlerExec::wrapWithSession(inner, QStringLiteral("/bin/setsid"),
                                        QString()),
           QStringList({QStringLiteral("/bin/setsid"),
                        QStringLiteral("xdg-open"), QStringLiteral("/tmp/a")}));
}

void HandlerExecTest::runUsesHookNotRealExec() {
  HandlerExec exec;
  QString program;
  QStringList args;
  QProcessEnvironment env;
  exec.setLaunchHook([&](const QString &prog, const QStringList &a,
                         const QProcessEnvironment &e) {
    program = prog;
    args = a;
    env = e;
    return true;
  });
  QVERIFY(exec.run(singleFile(QStringLiteral("/tmp/only-in-test.txt"))));
  dropSelection(env);
  QVERIFY(!program.isEmpty());
  QVERIFY(args.contains(QStringLiteral("xdg-open")) ||
          program.endsWith(QStringLiteral("xdg-open")) ||
          args.contains(QStringLiteral("/tmp/only-in-test.txt")));
  QVERIFY(args.contains(QStringLiteral("/tmp/only-in-test.txt")) ||
          program == QStringLiteral("xdg-open"));
  QVERIFY(args.contains(QStringLiteral("--")) ||
          QStandardPaths::findExecutable(QStringLiteral("uwsm-app")).isEmpty());
}

void HandlerExecTest::runSetsHandlerEnv() {
  HandlerExec exec;
  QProcessEnvironment env;
  exec.setLaunchHook([&](const QString &, const QStringList &,
                         const QProcessEnvironment &e) {
    env = e;
    return true;
  });
  auto req = singleFile(QStringLiteral("/tmp/env.txt"));
  req.cwd = QStringLiteral("/tmp");
  QVERIFY(exec.run(req));
  QCOMPARE(env.value(QStringLiteral("SYNCHRO_HANDLER_ID")),
           QStringLiteral("synchro.open.xdg"));
  QCOMPARE(env.value(QStringLiteral("SYNCHRO_CWD")), QStringLiteral("/tmp"));
  QVERIFY(env.value(QStringLiteral("SYNCHRO_THEME_DIR"))
              .endsWith(QStringLiteral("omarchy/current/theme")));
  const QString sel = env.value(QStringLiteral("SYNCHRO_SELECTION"));
  QVERIFY(QFileInfo::exists(sel));
  QFile f(sel);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
  QCOMPARE(obj.value(QStringLiteral("cwd")).toString(), QStringLiteral("/tmp"));
  dropSelection(env);
}

void HandlerExecTest::hookFailureSetsError() {
  HandlerExec exec;
  QProcessEnvironment env;
  exec.setLaunchHook([&](const QString &, const QStringList &,
                         const QProcessEnvironment &e) {
    env = e;
    return false;
  });
  QVERIFY(!exec.run(singleFile(QStringLiteral("/tmp/hook-fail.txt"))));
  dropSelection(env);
  QCOMPARE(exec.lastError(), QStringLiteral("launch hook rejected"));
}

void HandlerExecTest::loadFirstPartyManifest() {
  XdgOpen xdg;
  QVERIFY2(xdg.load(), qPrintable(xdg.lastError()));
  QCOMPARE(xdg.handlerId(), QStringLiteral("synchro.open.xdg"));
  QCOMPARE(xdg.execLine(), QStringLiteral("xdg-open %f"));
  QVERIFY(xdg.sourceDir().endsWith(QStringLiteral("synchro.open.xdg")));
  QVERIFY(QFileInfo::exists(xdg.sourceDir() +
                            QStringLiteral("/manifest.json")));
}

void HandlerExecTest::loadCustomManifestExec() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeXdgManifest(
      tmp.path(), QByteArrayLiteral("{ \"runtime\": \"exec\", \"exec\": \"true %f\" }")));

  XdgOpen xdg;
  QVERIFY(xdg.load(tmp.path()));
  QCOMPARE(xdg.execLine(), QStringLiteral("true %f"));

  QStringList args;
  QString program;
  QProcessEnvironment env;
  xdg.exec().setLaunchHook([&](const QString &p, const QStringList &a,
                               const QProcessEnvironment &e) {
    program = p;
    args = a;
    env = e;
    return true;
  });
  QVERIFY(xdg.open(tmp.filePath(QStringLiteral("note.txt")),
                   QStringLiteral("text/plain"), tmp.path()));
  dropSelection(env);
  const QStringList all = QStringList{program} + args;
  QVERIFY(all.contains(QStringLiteral("true")));
  QVERIFY(all.contains(tmp.filePath(QStringLiteral("note.txt"))));
}

void HandlerExecTest::mimeForTextAndDir() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("readme.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("subdir")));

  MimeMap map;
  QCOMPARE(map.mimeForFile(tmp.filePath(QStringLiteral("readme.txt"))),
           QStringLiteral("text/plain"));
  QCOMPARE(map.mimeForFile(tmp.filePath(QStringLiteral("subdir"))),
           QStringLiteral("inode/directory"));
  QCOMPARE(map.iconNameForMime(QStringLiteral("inode/directory")),
           QStringLiteral("folder"));
  QVERIFY(!map.iconNameForFile(tmp.filePath(QStringLiteral("readme.txt")))
               .isEmpty());
  QVERIFY(!map.resolveIcon(QStringLiteral("text-x-generic")).isEmpty());
}

void HandlerExecTest::recentAppendAndCompact() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("recent.jsonl"));
  RecentStore store(path, 3, 5);
  store.record(QStringLiteral("/a"), QStringLiteral("text/plain"));
  store.record(QStringLiteral("/b"), QStringLiteral("text/plain"));
  store.record(QStringLiteral("/a"), QStringLiteral("text/plain"));
  QCOMPARE(store.entries().size(), 3);

  store.record(QStringLiteral("/c"), QStringLiteral("text/plain"));
  store.record(QStringLiteral("/d"), QStringLiteral("text/plain"));
  const auto after = store.entries();
  QVERIFY(after.size() <= 3);
  QStringList paths;
  for (const auto &e : after)
    paths.append(e.path);
  QVERIFY(paths.contains(QStringLiteral("/d")));
  QVERIFY(paths.contains(QStringLiteral("/c")));
  QVERIFY(!paths.contains(QStringLiteral("/b")));

  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QList<QByteArray> lines = f.readAll().split('\n');
  int nonempty = 0;
  QJsonObject last;
  for (const QByteArray &line : lines) {
    if (line.trimmed().isEmpty())
      continue;
    ++nonempty;
    last = QJsonDocument::fromJson(line).object();
    QVERIFY(last.contains(QStringLiteral("ts")));
    QVERIFY(last.contains(QStringLiteral("path")));
    QVERIFY(last.contains(QStringLiteral("ws")));
    QVERIFY(last.value(QStringLiteral("ws")).isNull());
  }
  QCOMPARE(nonempty, after.size());
  QCOMPARE(last.value(QStringLiteral("path")).toString(),
           QStringLiteral("/d"));
}

void HandlerExecTest::recentDefaultPathIsXdgDataHome() {
  const QString path = RecentStore::defaultPath();
  QVERIFY(path.endsWith(QStringLiteral("synchro/recent.jsonl")));
  QVERIFY(!path.contains(QStringLiteral("/omarchy/")));
  QCOMPARE(path, QDir(QStandardPaths::writableLocation(
                           QStandardPaths::GenericDataLocation))
                     .filePath(QStringLiteral("synchro/recent.jsonl")));
}

void HandlerExecTest::recentTwoStoresSerialize() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("recent.jsonl"));

  auto worker = [&](const QString &prefix) {
    RecentStore store(path, 500, 10000);
    for (int i = 0; i < 25; ++i)
      store.record(prefix + QString::number(i), QStringLiteral("text/plain"));
  };
  std::thread t1([&] { worker(QStringLiteral("a")); });
  std::thread t2([&] { worker(QStringLiteral("b")); });
  t1.join();
  t2.join();

  RecentStore reader(path, 500, 10000);
  QCOMPARE(reader.entries().size(), 50);

  RecentStore a(path, 500, 10000);
  RecentStore b(path, 500, 10000);
  a.record(QStringLiteral("/from-a"), QStringLiteral("text/plain"));
  b.record(QStringLiteral("/from-b"), QStringLiteral("text/plain"));
  QStringList paths;
  for (const auto &e : a.entries())
    paths.append(e.path);
  QVERIFY(paths.contains(QStringLiteral("/from-a")));
  QVERIFY(paths.contains(QStringLiteral("/from-b")));

  RecentStore compacting(path, 5, 53);
  compacting.record(QStringLiteral("/compact"), QStringLiteral("text/plain"));
  const auto after = compacting.entries();
  QVERIFY(!after.isEmpty());
  QVERIFY(after.size() <= 5);
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray raw = f.readAll();
  for (const QByteArray &line : raw.split('\n')) {
    if (line.trimmed().isEmpty())
      continue;
    QVERIFY(QJsonDocument::fromJson(line).isObject());
  }
}

void HandlerExecTest::enterFileOpensViaManifestAndRecords() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString filePath = tmp.filePath(QStringLiteral("README.md"));
  QVERIFY(writeFile(filePath));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeXdgManifest(
      tmp.path(), QByteArrayLiteral("{ \"runtime\": \"exec\", \"exec\": \"true %f\" }")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  MimeMap mimeMap;
  XdgOpen xdg;
  QVERIFY(xdg.load(tmp.path()));
  QCOMPARE(xdg.execLine(), QStringLiteral("true %f"));

  QString program;
  QStringList args;
  QProcessEnvironment env;
  xdg.exec().setLaunchHook([&](const QString &p, const QStringList &a,
                               const QProcessEnvironment &e) {
    program = p;
    args = a;
    env = e;
    return true;
  });

  QObject::connect(&model, &DirectoryModel::fileActivated, &model,
                   [&](const QString &path, const QString &mime) {
                     QString resolved = mime;
                     if (resolved.isEmpty())
                       resolved = mimeMap.mimeForFile(path);
                     QVERIFY(xdg.open(path, resolved, model.path()));
                     recents.record(path, resolved);
                   });

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findRow(model, QStringLiteral("README.md"));
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));

  QCOMPARE(canon(model.path()), canon(tmp.path()));
  const QStringList launched = QStringList{program} + args;
  QVERIFY(launched.contains(QStringLiteral("true")));
  QVERIFY(launched.contains(filePath) ||
          launched.contains(QFileInfo(filePath).absoluteFilePath()));
  const QString setsid =
      QStandardPaths::findExecutable(QStringLiteral("setsid"));
  if (!setsid.isEmpty())
    QCOMPARE(program, setsid);
  const QString uwsm =
      QStandardPaths::findExecutable(QStringLiteral("uwsm-app"));
  if (!uwsm.isEmpty())
    QVERIFY(args.contains(QStringLiteral("uwsm-app")) ||
            args.contains(uwsm) || program == uwsm);
  QCOMPARE(env.value(QStringLiteral("SYNCHRO_HANDLER_ID")),
           QStringLiteral("synchro.open.xdg"));

  const auto rec = recents.entries();
  QCOMPARE(rec.size(), 1);
  QCOMPARE(QFileInfo(rec.at(0).path).fileName(),
           QStringLiteral("README.md"));
  dropSelection(env);
}

void HandlerExecTest::enterDirDoesNotOpen() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("src/inside.txt"))));

  DirectoryModel model;
  int opens = 0;
  QObject::connect(&model, &DirectoryModel::fileActivated, &model,
                   [&](const QString &, const QString &) { ++opens; });
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findRow(model, QStringLiteral("src"));
  QVERIFY(row >= 0);
  model.setCurrentIndex(row);
  model.activateCurrent();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(tmp.filePath(QStringLiteral("src"))));
  QCOMPARE(opens, 0);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  HandlerExecTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "handler_exec_test.moc"
