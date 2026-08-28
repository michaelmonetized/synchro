#include "AgentIntegration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <sqlite3.h>

namespace {

bool writeFile(const QString &path, const QByteArray &contents) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return file.write(contents) == contents.size();
}

} // namespace

class AgentIntegrationTest : public QObject {
  Q_OBJECT

private slots:
  void installsSharedSkillIdempotently();
  void preservesConflictingSkill();
  void contextIsSelfDescribingWithoutMcp();
  void embeddedSearchPromptAndResponse();
  void embeddedSearchCompletesAsynchronously();
  void embeddedSearchClosesAgentStdin();
  void embeddedSearchSamplesOnlyFileThumbnails();
};

void AgentIntegrationTest::installsSharedSkillIdempotently() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString source = temp.filePath(QStringLiteral("source/synchro"));
  QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("SKILL.md")),
                    QByteArrayLiteral("---\nname: synchro\n---\n")));
  const QString home = temp.filePath(QStringLiteral("home"));

  const AgentSkillStatus first = AgentIntegration::installSkill(home, source);
  QVERIFY2(first.ok(), qPrintable(first.errors.join(QLatin1Char('\n'))));
  QCOMPARE(first.installed.size(), 4);
  QCOMPARE(first.present.size(), 0);

  const AgentSkillStatus second = AgentIntegration::installSkill(home, source);
  QVERIFY(second.ok());
  QCOMPARE(second.installed.size(), 0);
  QCOMPARE(second.present.size(), 4);
  for (const QString &path : second.present)
    QVERIFY(QFileInfo(path).isSymLink());
}

void AgentIntegrationTest::preservesConflictingSkill() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString source = temp.filePath(QStringLiteral("source/synchro"));
  QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("SKILL.md")),
                    QByteArrayLiteral("source\n")));
  const QString home = temp.filePath(QStringLiteral("home"));
  const QString conflict =
      QDir(home).filePath(QStringLiteral(".codex/skills/synchro/SKILL.md"));
  QVERIFY(writeFile(conflict, QByteArrayLiteral("user owned\n")));

  const AgentSkillStatus status = AgentIntegration::installSkill(home, source);
  QVERIFY(!status.ok());
  QCOMPARE(status.conflicts, QStringList{QFileInfo(conflict).absolutePath()});
  QFile file(conflict);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(file.readAll(), QByteArrayLiteral("user owned\n"));
}

void AgentIntegrationTest::contextIsSelfDescribingWithoutMcp() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString selected = temp.filePath(QStringLiteral("photo.png"));
  const QString manifest = temp.filePath(QStringLiteral("selection.json"));
  const QJsonObject item{{QStringLiteral("path"), selected},
                         {QStringLiteral("mime"), QStringLiteral("image/png")},
                         {QStringLiteral("isDir"), false}};
  const QJsonObject input{{QStringLiteral("cwd"), temp.path()},
                          {QStringLiteral("items"), QJsonArray{item}}};
  QVERIFY(writeFile(manifest, QJsonDocument(input).toJson()));

  const QJsonObject context = AgentIntegration::context(manifest);
  QVERIFY(context.value(QStringLiteral("ok")).toBool());
  QCOMPARE(context.value(QStringLiteral("selectionCount")).toInt(), 1);
  QCOMPARE(context.value(QStringLiteral("cwd")).toString(), temp.path());
  QVERIFY(!context.value(QStringLiteral("binary")).toString().isEmpty());
  const QJsonObject capabilities =
      context.value(QStringLiteral("capabilities")).toObject();
  const QJsonObject catalog =
      capabilities.value(QStringLiteral("catalog")).toObject();
  QVERIFY(catalog.value(QStringLiteral("required")).toBool());
  QVERIFY(catalog.value(QStringLiteral("queryCommand"))
              .toString()
              .contains(QStringLiteral(" agent query")));
  QVERIFY(catalog.value(QStringLiteral("relationFields"))
              .toObject()
              .value(QStringLiteral("image_facts"))
              .toArray()
              .contains(QStringLiteral("dominant_color")));
  const QString rules = QString::fromUtf8(
      QJsonDocument(catalog.value(QStringLiteral("resultRules")).toArray())
          .toJson(QJsonDocument::Compact));
  QVERIFY(rules.contains(QStringLiteral("do not probe schemas")));
  QVERIFY(rules.contains(QStringLiteral("filter images by extension")));
  QVERIFY(!capabilities.value(QStringLiteral("mcp"))
               .toObject()
               .value(QStringLiteral("required"))
               .toBool(true));
  QVERIFY(capabilities.value(QStringLiteral("catalog"))
              .toObject()
              .value(QStringLiteral("showCommand"))
              .toString()
              .contains(QStringLiteral(" agent show")));
}

void AgentIntegrationTest::embeddedSearchPromptAndResponse() {
  const QString prompt = AgentSearchBridge::promptFor(
      QStringLiteral("large blue images from this month"),
      QStringLiteral("/tmp/example"), QStringLiteral("/usr/bin/synchro"));
  QVERIFY(prompt.contains(QStringLiteral("Synchro's embedded file-query translator")));
  QVERIFY(prompt.contains(QStringLiteral("embedded file-query translator")));
  QVERIFY(prompt.contains(QStringLiteral("do not run shell commands")));
  QVERIFY(prompt.contains(QStringLiteral("Synchro will validate and execute")));
  QVERIFY(prompt.contains(QStringLiteral("large blue images from this month")));
  QVERIFY(prompt.contains(QStringLiteral("SYNCHRO_RESULT")));

  QString error;
  const QByteArray response = QByteArrayLiteral(
      "I checked catalog coverage.\nSYNCHRO_RESULT\n"
      "{\"label\":\"Blue images\",\"sql\":\"select name, path, is_dir "
      "from image_facts where color_family = 'blue'\",\"cwd\":\"/tmp/example\"}");
  const QVariantMap result = AgentSearchBridge::parseResponse(response, &error);
  QVERIFY2(!result.isEmpty(), qPrintable(error));
  QCOMPARE(result.value(QStringLiteral("label")).toString(),
           QStringLiteral("Blue images"));
  QCOMPARE(result.value(QStringLiteral("cwd")).toString(),
           QStringLiteral("/tmp/example"));

  const QVariantMap unsafe = AgentSearchBridge::parseResponse(
      QByteArrayLiteral("SYNCHRO_RESULT\n{\"sql\":\"delete from files\"}"),
      &error);
  QVERIFY(unsafe.isEmpty());
  QVERIFY(error.contains(QStringLiteral("read-only")));
}

void AgentIntegrationTest::embeddedSearchCompletesAsynchronously() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QByteArray fake = QByteArrayLiteral(
      "SYNCHRO_RESULT\n{\"label\":\"Recent code\","
      "\"sql\":\"select name, path, is_dir from tree where kind = 'code'\","
      "\"cwd\":\"/tmp\"}");
  qputenv("SYNCHRO_AGENT_SEARCH_FAKE_RESPONSE", fake);

  AgentSearchBridge bridge;
  QSignalSpy resultSpy(&bridge, &AgentSearchBridge::resultReady);
  QVERIFY(bridge.start(QStringLiteral("recent code"), temp.path()));
  QVERIFY(bridge.running());
  QTRY_COMPARE(resultSpy.size(), 1);
  QVERIFY(!bridge.running());
  QCOMPARE(resultSpy.at(0).at(0).toString(), QStringLiteral("Recent code"));
  QCOMPARE(resultSpy.at(0).at(1).toString(),
           QStringLiteral("select name, path, is_dir from tree where kind = 'code'"));
  qunsetenv("SYNCHRO_AGENT_SEARCH_FAKE_RESPONSE");
}

void AgentIntegrationTest::embeddedSearchClosesAgentStdin() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString helper = temp.filePath(QStringLiteral("agent-helper"));
  QVERIFY(writeFile(
      helper,
      QByteArrayLiteral(
          "#!/bin/sh\n"
          "cat >/dev/null\n"
          "printf '%s\\n' 'SYNCHRO_RESULT' "
          "'{\"label\":\"EOF works\",\"sql\":\"select name,path,is_dir "
          "from here\",\"cwd\":\"/tmp\"}'\n")));
  QVERIFY(QFile::setPermissions(
      helper, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                  QFileDevice::ExeOwner));
  qputenv("SYNCHRO_AGENT_SEARCH_AGENT", QByteArrayLiteral("codex"));
  qputenv("SYNCHRO_AGENT_SEARCH_PROGRAM", QFile::encodeName(helper));

  AgentSearchBridge bridge;
  QSignalSpy resultSpy(&bridge, &AgentSearchBridge::resultReady);
  QVERIFY(bridge.start(QStringLiteral("anything"), temp.path()));
  QTRY_COMPARE_WITH_TIMEOUT(resultSpy.size(), 1, 3000);
  QVERIFY(!bridge.running());
  QCOMPARE(resultSpy.first().first().toString(), QStringLiteral("EOF works"));

  qunsetenv("SYNCHRO_AGENT_SEARCH_AGENT");
  qunsetenv("SYNCHRO_AGENT_SEARCH_PROGRAM");
}

void AgentIntegrationTest::embeddedSearchSamplesOnlyFileThumbnails() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(temp.path()));
  const QString source = temp.filePath(QStringLiteral("report.txt"));
  const QString folder = temp.filePath(QStringLiteral("folder"));
  QVERIFY(writeFile(source, QByteArrayLiteral("report\n")));
  QVERIFY(QDir().mkpath(folder));

  sqlite3 *db = nullptr;
  QVERIFY(sqlite3_open_v2(
              QFile::encodeName(temp.filePath(QStringLiteral("thumbs.sqlite")))
                  .constData(),
              &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) ==
          SQLITE_OK);
  QVERIFY(sqlite3_exec(
              db,
              "CREATE TABLE thumbs(key TEXT,path TEXT,mtime INTEGER,size_px "
              "INTEGER,png BLOB,bytes INTEGER,accessed INTEGER);",
              nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_stmt *insert = nullptr;
  QVERIFY(sqlite3_prepare_v2(
              db,
              "INSERT INTO thumbs(key,path,mtime,size_px,png,bytes,accessed) "
              "VALUES(?,?,1,256,X'',0,10);",
              -1, &insert, nullptr) == SQLITE_OK);
  auto add = [&](const QString &key, const QString &path) {
    sqlite3_reset(insert);
    sqlite3_clear_bindings(insert);
    const QByteArray keyBytes = key.toUtf8();
    const QByteArray pathBytes = path.toUtf8();
    sqlite3_bind_text(insert, 1, keyBytes.constData(), keyBytes.size(),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(insert, 2, pathBytes.constData(), pathBytes.size(),
                      SQLITE_TRANSIENT);
    return sqlite3_step(insert) == SQLITE_DONE;
  };
  QVERIFY(add(QStringLiteral("file-key"),
              source + QStringLiteral("#card-v5-theme")));
  QVERIFY(add(QStringLiteral("folder-key"),
              folder + QStringLiteral("#mosaic-v8-theme")));
  sqlite3_finalize(insert);
  sqlite3_close(db);

  qputenv("SYNCHRO_AGENT_SEARCH_FAKE_RESPONSE",
          QByteArrayLiteral("SYNCHRO_RESULT\n{\"label\":\"x\",\"sql\":"
                            "\"select name,path,is_dir from here\"}"));
  AgentSearchBridge bridge;
  QVERIFY(bridge.start(QStringLiteral("anything"), temp.path()));
  QTRY_COMPARE_WITH_TIMEOUT(bridge.thumbnails().size(), 1, 3000);
  QCOMPARE(bridge.thumbnails().first(),
           QStringLiteral("image://synchrothumb/file-key"));

  qunsetenv("SYNCHRO_AGENT_SEARCH_FAKE_RESPONSE");
  qunsetenv("SYNCHRO_HOME");
}

QTEST_MAIN(AgentIntegrationTest)
#include "agent_integration_test.moc"
