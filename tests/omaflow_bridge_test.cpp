#include "OmaflowBridge.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace {

bool writeText(const QString &path, const QByteArray &text) {
  const QFileInfo info(path);
  if (!QDir().mkpath(info.absolutePath()))
    return false;
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return file.write(text) == text.size();
}

class EnvGuard {
public:
  explicit EnvGuard(const QByteArray &name) : m_name(name) {
    m_had = qEnvironmentVariableIsSet(name.constData());
    m_value = qgetenv(name.constData());
  }
  ~EnvGuard() {
    if (m_had)
      qputenv(m_name.constData(), m_value);
    else
      qunsetenv(m_name.constData());
  }

private:
  QByteArray m_name;
  QByteArray m_value;
  bool m_had = false;
};

} // namespace

class OmaflowBridgeTest : public QObject {
  Q_OBJECT

private slots:
  void readsStateAndRunsFixedCli();
};

void OmaflowBridgeTest::readsStateAndRunsFixedCli() {
  EnvGuard binGuard("SYNCHRO_OMAFLOW_BIN");
  EnvGuard configGuard("SYNCHRO_OMAFLOW_CONFIG_DIR");
  EnvGuard stateGuard("SYNCHRO_OMAFLOW_STATE_DIR");
  EnvGuard callsGuard("SYNCHRO_OMAFLOW_TEST_CALLS");
  QTemporaryDir temp;
  QVERIFY(temp.isValid());

  const QString config = temp.filePath(QStringLiteral("config"));
  const QString state = temp.filePath(QStringLiteral("state"));
  const QString calls = temp.filePath(QStringLiteral("calls"));
  const QString binary = temp.filePath(QStringLiteral("omaflow"));
  QVERIFY(writeText(
      binary,
      QByteArrayLiteral(
          "#!/bin/sh\n"
          "printf '%s\\n' \"$*\" >> \"$SYNCHRO_OMAFLOW_TEST_CALLS\"\n"
          "context=''\n"
          "previous=''\n"
          "for arg in \"$@\"; do\n"
          "  if [ \"$previous\" = context ]; then context=\"$arg\"; fi\n"
          "  if [ \"$arg\" = --context-file ]; then previous=context; else "
          "previous=''; fi\n"
          "done\n"
          "if [ -n \"$context\" ]; then cp \"$context\" "
          "\"$SYNCHRO_OMAFLOW_TEST_CALLS.context\"; "
          "result=$(jq -r '.resultFile // empty' \"$context\"); "
          "if [ -n \"$result\" ]; then "
          "printf '%s\\n' '{\"summary\":\"Created a test artifact\",\"artifacts\":[{\"path\":\"/tmp/result.txt\",\"kind\":\"text\",\"label\":\"result.txt\"}]}' >\"$result\"; fi; fi\n"
          "if [ \"$1\" = \"author\" ]; then\n"
          "  if [ \"$2\" = \"slow-draft\" ]; then\n"
          "    while :; do :; done\n"
          "  fi\n"
          "  echo 'draft compiled'\n"
          "elif [ \"$1\" = \"stage\" ]; then\n"
          "  echo \"stage $2 complete\"\n"
          "elif [ \"$3\" = \"--dry-run\" ]; then\n"
          "  echo 'would run: notify'\n"
          "else\n"
          "  echo 'ran from synchro'\n"
          "fi\n")));
  QVERIFY(QFile::setPermissions(
      binary, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                  QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                  QFileDevice::ExeGroup | QFileDevice::ReadOther |
                  QFileDevice::ExeOther));
  QVERIFY(writeText(
      QDir(state).filePath(QStringLiteral("index.json")),
      QByteArrayLiteral(
          "{\"generatedAt\":\"2026-08-24T12:00:00Z\",\"rules\":[{"
          "\"id\":\"quiet-build\",\"name\":\"Quiet build\","
          "\"enabled\":true,\"triggerSummary\":\"custom: build\","
          "\"actionsSummary\":\"dnd, notify\",\"actionCount\":2,"
          "\"conditionCount\":1,\"lastFired\":\"2026-08-24T12:01:00Z\"}]}")));
  QVERIFY(writeText(
      QDir(config).filePath(QStringLiteral("rules/quiet-build.json")),
      QByteArrayLiteral(
          "{\"schemaVersion\":1,\"id\":\"quiet-build\","
          "\"name\":\"Quiet build\",\"enabled\":true,"
          "\"trigger\":{\"type\":\"custom\",\"name\":\"build\"},"
          "\"accepts\":{\"mime\":[\"text/*\"],\"suffix\":[\".md\"],"
          "\"folderContains\":[\"missing.marker\",\"marker.txt\"],"
          "\"kind\":\"files\",\"minItems\":1,\"maxItems\":4},"
          "\"conditions\":[{\"type\":\"on-power\",\"source\":\"ac\"}],"
          "\"actions\":[{\"type\":\"dnd\",\"state\":\"on\"},"
          "{\"type\":\"notify\",\"message\":\"building\"}],"
          "\"source\":\"quiet the desktop while I build\"}")));
  QVERIFY(writeText(
      QDir(state).filePath(QStringLiteral("log.jsonl")),
      QByteArrayLiteral("{\"at\":\"2026-08-24T12:01:00Z\",\"kind\":\"run\","
                        "\"ruleId\":\"quiet-build\",\"status\":\"ok\"}\n")));
  QVERIFY(
      writeText(QDir(state).filePath(QStringLiteral("staging.json")),
                QByteArrayLiteral(
                    "{\"status\":\"ready\",\"request\":\"draft something\"}")));

  qputenv("SYNCHRO_OMAFLOW_BIN", binary.toUtf8());
  qputenv("SYNCHRO_OMAFLOW_CONFIG_DIR", config.toUtf8());
  qputenv("SYNCHRO_OMAFLOW_STATE_DIR", state.toUtf8());
  qputenv("SYNCHRO_OMAFLOW_TEST_CALLS", calls.toUtf8());

  OmaflowBridge bridge;
  QVERIFY(bridge.installed());
  QCOMPARE(bridge.executable(), QFileInfo(binary).absoluteFilePath());
  QCOMPARE(bridge.rules().size(), 1);
  const QVariantMap rule = bridge.rules().constFirst().toMap();
  QCOMPARE(rule.value(QStringLiteral("id")).toString(),
           QStringLiteral("quiet-build"));
  QCOMPARE(rule.value(QStringLiteral("source")).toString(),
           QStringLiteral("quiet the desktop while I build"));
  QCOMPARE(rule.value(QStringLiteral("conditions")).toList().size(), 1);
  QCOMPARE(rule.value(QStringLiteral("actions")).toList().size(), 2);
  QCOMPARE(bridge.activity().size(), 1);
  QCOMPARE(bridge.staging().value(QStringLiteral("status")).toString(),
           QStringLiteral("ready"));

  QVariantMap selected;
  const QString selectedPath = temp.filePath(QStringLiteral("README.md"));
  QVERIFY(writeText(selectedPath, QByteArrayLiteral("selected\n")));
  QVERIFY(writeText(temp.filePath(QStringLiteral("marker.txt")),
                    QByteArrayLiteral("project\n")));
  selected.insert(QStringLiteral("path"), selectedPath);
  selected.insert(QStringLiteral("uri"), QUrl::fromLocalFile(selectedPath));
  selected.insert(QStringLiteral("mime"), QStringLiteral("text/markdown"));
  selected.insert(QStringLiteral("isDir"), false);
  const QVariantList selection{selected};
  QCOMPARE(bridge.matchingRules(selection).size(), 1);
  QCOMPARE(bridge.matchingRules(selection)
               .constFirst()
               .toMap()
               .value(QStringLiteral("id"))
               .toString(),
           QStringLiteral("quiet-build"));
  QVariantMap wrong = selected;
  wrong.insert(QStringLiteral("mime"), QStringLiteral("image/png"));
  QCOMPARE(bridge.matchingRules({wrong}).size(), 0);

  QSignalSpy finished(&bridge, &OmaflowBridge::operationFinished);
  QVERIFY(bridge.author(QStringLiteral("When the projector appears, focus")));
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QCOMPARE(bridge.operationKind(), QStringLiteral("author"));
  QVERIFY(bridge.operationOutput().contains(QStringLiteral("draft compiled")));
  QVERIFY(!bridge.author(QString(2001, QLatin1Char('x'))));

  QVERIFY(bridge.acceptStage());
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QCOMPARE(bridge.operationKind(), QStringLiteral("stage-accept"));

  QVERIFY(bridge.rejectStage());
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QCOMPARE(bridge.operationKind(), QStringLiteral("stage-reject"));

  QVERIFY(bridge.author(QStringLiteral("slow-draft")));
  QVERIFY(bridge.busy());
  const auto callsContain = [&calls](const QByteArray &needle) {
    QFile file(calls);
    return file.open(QIODevice::ReadOnly) && file.readAll().contains(needle);
  };
  QTRY_VERIFY_WITH_TIMEOUT(callsContain("author slow-draft"), 1000);
  bridge.cancel();
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), false);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QCOMPARE(bridge.operationKind(), QStringLiteral("stage-reject"));
  QVERIFY(!bridge.busy());

  QVERIFY(bridge.dryRun(QStringLiteral("quiet-build")));
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QVERIFY(bridge.operationOutput().contains(QStringLiteral("would run")));
  QVERIFY(!bridge.dryRun(QStringLiteral("../not-a-rule")));

  QVERIFY(bridge.run(QStringLiteral("quiet-build")));
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QCOMPARE(bridge.operationKind(), QStringLiteral("run"));

  QVERIFY(bridge.runSelection(QStringLiteral("quiet-build"), selection,
                              temp.path(), true));
  QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 3000);
  QCOMPARE(finished.takeFirst().constFirst().toBool(), true);
  QFile contextCopy(calls + QStringLiteral(".context"));
  QVERIFY(contextCopy.open(QIODevice::ReadOnly));
  const QByteArray context = contextCopy.readAll();
  QVERIFY(context.contains(selectedPath.toUtf8()));
  QVERIFY(context.contains("\"count\":1"));
  QVERIFY(context.contains(temp.path().toUtf8()));
  QVERIFY(context.contains("\"resultFile\":"));
  QCOMPARE(bridge.operationResult().value(QStringLiteral("summary")).toString(),
           QStringLiteral("Created a test artifact"));
  QCOMPARE(bridge.operationResult()
               .value(QStringLiteral("artifacts"))
               .toList()
               .constFirst()
               .toMap()
               .value(QStringLiteral("label"))
               .toString(),
           QStringLiteral("result.txt"));

  QFile callFile(calls);
  QVERIFY(callFile.open(QIODevice::ReadOnly));
  const QByteArray invocations = callFile.readAll();
  QVERIFY(invocations.contains("author When the projector appears, focus"));
  QVERIFY(invocations.contains("stage accept"));
  QVERIFY(invocations.contains("stage reject"));
  QVERIFY(invocations.contains("author slow-draft"));
  QVERIFY(invocations.contains("run quiet-build --dry-run"));
  QVERIFY(invocations.contains("run quiet-build --trigger manual (synchro)"));
  QVERIFY(invocations.contains("run quiet-build --dry-run --trigger selection "
                               "(synchro) --context-file"));
}

QTEST_MAIN(OmaflowBridgeTest)
#include "omaflow_bridge_test.moc"
