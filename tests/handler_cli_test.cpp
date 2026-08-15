#include "HandlerInstall.h"
#include "HandlerRegistry.h"
#include "Manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

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

QString gitBin() {
  return QStandardPaths::findExecutable(QStringLiteral("git"));
}

bool runGit(const QStringList &args, const QString &cwd) {
  QProcess p;
  p.setProgram(gitBin());
  p.setArguments(args);
  if (!cwd.isEmpty())
    p.setWorkingDirectory(cwd);
  p.start();
  if (!p.waitForFinished(15000))
    return false;
  return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QByteArray thirdPartyManifest(const QString &id, const QString &version) {
  return "{\n"
         "  \"schemaVersion\": 1,\n"
         "  \"id\": \"" +
         id.toUtf8() +
         "\",\n"
         "  \"name\": \"Demo\",\n"
         "  \"version\": \"" +
         version.toUtf8() +
         "\",\n"
         "  \"kinds\": [\"open\"],\n"
         "  \"entryPoints\": {},\n"
         "  \"open\": { \"runtime\": \"exec\", \"exec\": \"true %f\" }\n"
         "}\n";
}

bool initRepo(const QString &dir, const QString &id, const QString &version) {
  if (!QDir().mkpath(dir))
    return false;
  if (!writeText(dir + QStringLiteral("/manifest.json"),
                 thirdPartyManifest(id, version)))
    return false;
  if (!runGit({QStringLiteral("init"), QStringLiteral("-b"),
               QStringLiteral("main")},
              dir))
    return false;
  if (!runGit({QStringLiteral("config"), QStringLiteral("user.email"),
               QStringLiteral("test@example.com")},
              dir))
    return false;
  if (!runGit({QStringLiteral("config"), QStringLiteral("user.name"),
               QStringLiteral("test")},
              dir))
    return false;
  if (!runGit({QStringLiteral("add"), QStringLiteral("manifest.json")}, dir))
    return false;
  return runGit({QStringLiteral("commit"), QStringLiteral("-m"),
                 QStringLiteral("init")},
                dir);
}

} // namespace

class HandlerCliTest : public QObject {
  Q_OBJECT

private slots:
  void addLandsDisabledAndEnablePersists();
  void addReservedIdRejected();
  void addAlreadyInstalled();
  void updateFastForward();
  void removeDropsEnabled();
};

void HandlerCliTest::addLandsDisabledAndEnablePersists() {
  if (gitBin().isEmpty())
    QSKIP("git not on PATH");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString origin = tmp.filePath(QStringLiteral("origin"));
  QVERIFY(initRepo(origin, QStringLiteral("acme.demo"), QStringLiteral("1.0.0")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  reg.setScanEnv(false);

  HandlerInstall install(&reg);
  install.setAssumeYes(true);
  QVERIFY2(install.add(origin), qPrintable(install.lastError()));
  QCOMPARE(install.lastId(), QStringLiteral("acme.demo"));
  QVERIFY(install.lastMessage().contains(QStringLiteral("disabled")));

  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("acme.demo")));
  QVERIFY(!reg.handler(QStringLiteral("acme.demo")).enabled);
  QVERIFY(!Manifest::isReservedId(QStringLiteral("acme.demo")));

  QString err;
  QVERIFY(reg.setEnabled(QStringLiteral("acme.demo"), true, &err));
  QVERIFY(reg.handler(QStringLiteral("acme.demo")).enabled);

  HandlerRegistry again;
  again.setFirstPartyDir(QStringLiteral("/nonexistent"));
  again.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  again.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  again.setScanEnv(false);
  again.scan();
  QVERIFY(again.handler(QStringLiteral("acme.demo")).enabled);
}

void HandlerCliTest::addReservedIdRejected() {
  if (gitBin().isEmpty())
    QSKIP("git not on PATH");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString origin = tmp.filePath(QStringLiteral("origin"));
  QVERIFY(initRepo(origin, QStringLiteral("synchro.open.evil"),
                   QStringLiteral("1.0.0")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  reg.setScanEnv(false);

  HandlerInstall install(&reg);
  install.setAssumeYes(true);
  QVERIFY(!install.add(origin));
  QVERIFY(install.lastError().contains(QStringLiteral("reserved")) ||
          install.lastError().contains(QStringLiteral("refusing")));
  QVERIFY(!QFileInfo::exists(
      QDir(reg.userDir()).filePath(QStringLiteral("synchro.open.evil"))));
}

void HandlerCliTest::addAlreadyInstalled() {
  if (gitBin().isEmpty())
    QSKIP("git not on PATH");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString origin = tmp.filePath(QStringLiteral("origin"));
  QVERIFY(initRepo(origin, QStringLiteral("acme.dup"), QStringLiteral("1.0.0")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  reg.setScanEnv(false);

  HandlerInstall install(&reg);
  install.setAssumeYes(true);
  QVERIFY(install.add(origin));
  QVERIFY(!install.add(origin));
  QVERIFY(install.lastError().contains(QStringLiteral("already")));
}

void HandlerCliTest::updateFastForward() {
  if (gitBin().isEmpty())
    QSKIP("git not on PATH");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString origin = tmp.filePath(QStringLiteral("origin"));
  QVERIFY(initRepo(origin, QStringLiteral("acme.up"), QStringLiteral("1.0.0")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  reg.setScanEnv(false);

  HandlerInstall install(&reg);
  install.setAssumeYes(true);
  QVERIFY(install.add(origin));

  QVERIFY(writeText(origin + QStringLiteral("/manifest.json"),
                    thirdPartyManifest(QStringLiteral("acme.up"),
                                       QStringLiteral("1.1.0"))));
  QVERIFY(runGit({QStringLiteral("add"), QStringLiteral("manifest.json")},
                 origin));
  QVERIFY(runGit({QStringLiteral("commit"), QStringLiteral("-m"),
                  QStringLiteral("bump")},
                 origin));

  QString surfaced;
  install.setReviewSink([&](const QString &diff) { surfaced = diff; });
  QVERIFY2(install.update(QStringLiteral("acme.up")),
           qPrintable(install.lastError()));
  QVERIFY2(!install.lastReviewDiff().isEmpty(),
           "update must surface a review diff when HEAD != FETCH_HEAD");
  QVERIFY(install.lastReviewDiff().contains(QStringLiteral("1.1.0")));
  QCOMPARE(surfaced, install.lastReviewDiff());
  QVERIFY(install.lastMessage().contains(QStringLiteral("Updated acme.up")));
  const QString landed =
      QDir(reg.userDir()).filePath(QStringLiteral("acme.up/manifest.json"));
  QFile f(landed);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
  QCOMPARE(obj.value(QStringLiteral("version")).toString(),
           QStringLiteral("1.1.0"));
}

void HandlerCliTest::removeDropsEnabled() {
  if (gitBin().isEmpty())
    QSKIP("git not on PATH");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString origin = tmp.filePath(QStringLiteral("origin"));
  QVERIFY(initRepo(origin, QStringLiteral("acme.rm"), QStringLiteral("1.0.0")));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.filePath(QStringLiteral("handlers")));
  reg.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  reg.setScanEnv(false);

  HandlerInstall install(&reg);
  install.setAssumeYes(true);
  install.setEnableAfterAdd(true);
  QVERIFY(install.add(origin));
  reg.scan();
  QVERIFY(reg.handler(QStringLiteral("acme.rm")).enabled);

  QVERIFY(install.remove(QStringLiteral("acme.rm")));
  QVERIFY(!QFileInfo::exists(
      QDir(reg.userDir()).filePath(QStringLiteral("acme.rm"))));
  reg.scan();
  QVERIFY(!reg.contains(QStringLiteral("acme.rm")));
  QVERIFY(!reg.enabledIds().contains(QStringLiteral("acme.rm")));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  HandlerCliTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "handler_cli_test.moc"
