#include "Manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

QString writeHandler(const QTemporaryDir &tmp, const QString &id,
                     const QByteArray &manifest, bool qml = false) {
  const QString dir = tmp.filePath(id);
  QDir().mkpath(dir);
  writeText(dir + QStringLiteral("/manifest.json"), manifest);
  if (qml)
    writeText(dir + QStringLiteral("/Preview.qml"),
              QByteArrayLiteral("import QtQuick\nItem {}\n"));
  return dir;
}

} // namespace

class ManifestValidateTest : public QObject {
  Q_OBJECT

private slots:
  void firstPartyImageValid();
  void firstPartyXdgValid();
  void firstPartyOmawriteValid();
  void firstPartyActionHandlersValid();
  void firstPartyPreviewHandlersValid();
  void firstPartyPanelTerminalValid();
  void actionCoreRequiresVerb();
  void schemaVersionMustBeNumberOne();
  void requiredFields();
  void invalidId();
  void reservedIdRejectedForThirdParty();
  void reservedIdOkFirstParty();
  void unsafeEntryPoints();
  void missingEntryPointFile();
  void replaceListingRejected();
  void locationRuntimes();
  void previewRequiresEntryPoint();
  void openExecOnlyOk();
  void symlinkRejected();
  void isSafeEntryPointCopy();
  void gitSymlinkEntryPointRejected();
  void entryFileSymlinkRejected();
};

void ManifestValidateTest::firstPartyImageValid() {
  const QString dir = QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
                          .filePath(QStringLiteral("synchro.preview.image"));
  const auto v = validateManifestDir(dir, true);
  QVERIFY2(v.ok, qPrintable(v.errors.join(QLatin1Char(';'))));
  QCOMPARE(v.manifest.id, QStringLiteral("synchro.preview.image"));
  QVERIFY(v.manifest.hasKind(QStringLiteral("preview")));
  QCOMPARE(v.manifest.entryPoints.value(QStringLiteral("preview")),
           QStringLiteral("Preview.qml"));
}

void ManifestValidateTest::firstPartyXdgValid() {
  const QString dir = QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
                          .filePath(QStringLiteral("synchro.open.xdg"));
  const auto v = validateManifestDir(dir, true);
  QVERIFY2(v.ok, qPrintable(v.errors.join(QLatin1Char(';'))));
  QCOMPARE(v.manifest.id, QStringLiteral("synchro.open.xdg"));
}

void ManifestValidateTest::firstPartyOmawriteValid() {
  const QString dir = QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
                          .filePath(QStringLiteral("synchro.open.omawrite"));
  const auto v = validateManifestDir(dir, true);
  QVERIFY2(v.ok, qPrintable(v.errors.join(QLatin1Char(';'))));
  QCOMPARE(v.manifest.id, QStringLiteral("synchro.open.omawrite"));
  QCOMPARE(v.manifest.execLine(QStringLiteral("open")),
           QStringLiteral("omawrite %f"));
  QCOMPARE(v.manifest.tryExec(QStringLiteral("open")),
           QStringLiteral("omawrite"));
  QCOMPARE(v.manifest.priority, 80);
  QVERIFY(v.manifest.match.mime.contains(QStringLiteral("text/markdown")));
}

void ManifestValidateTest::firstPartyActionHandlersValid() {
  const QString root = QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR);
  const auto term = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.action.terminal")), true);
  QVERIFY2(term.ok, qPrintable(term.errors.join(QLatin1Char(';'))));
  QCOMPARE(term.manifest.execLine(QStringLiteral("action")),
           QStringLiteral("xdg-terminal-exec --dir=%d"));
  QVERIFY(!term.manifest.execLine(QStringLiteral("action"))
               .contains(QStringLiteral("omarchy-launch-terminal")));

  const auto trash = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.action.trash")), true);
  QVERIFY2(trash.ok, qPrintable(trash.errors.join(QLatin1Char(';'))));
  QCOMPARE(trash.manifest.runtime(QStringLiteral("action")),
           QStringLiteral("core"));
  QCOMPARE(trash.manifest.coreVerb(QStringLiteral("action")),
           QStringLiteral("trash"));
  QVERIFY(trash.manifest.execLine(QStringLiteral("action")).isEmpty());

  const auto rename = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.action.rename")), true);
  QVERIFY2(rename.ok, qPrintable(rename.errors.join(QLatin1Char(';'))));
  QCOMPARE(rename.manifest.entryPoints.value(QStringLiteral("action")),
           QStringLiteral("Params.qml"));

  const auto copyAs = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.action.copy-as")), true);
  QVERIFY2(copyAs.ok, qPrintable(copyAs.errors.join(QLatin1Char(';'))));
  QCOMPARE(copyAs.manifest.entryPoints.value(QStringLiteral("action")),
           QStringLiteral("Params.qml"));

  const auto home = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.location.home")), true);
  QVERIFY2(home.ok, qPrintable(home.errors.join(QLatin1Char(';'))));
  QCOMPARE(home.manifest.runtime(QStringLiteral("location")),
           QStringLiteral("path"));
  QCOMPARE(home.manifest.location.value(QStringLiteral("path")).toString(),
           QStringLiteral("$HOME"));

  const auto recent = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.location.recent")), true);
  QVERIFY2(recent.ok, qPrintable(recent.errors.join(QLatin1Char(';'))));
  QCOMPARE(recent.manifest.runtime(QStringLiteral("location")),
           QStringLiteral("core"));
  QCOMPARE(recent.manifest.coreVerb(QStringLiteral("location")),
           QStringLiteral("recent"));

  const auto trashLoc = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.location.trash")), true);
  QVERIFY2(trashLoc.ok, qPrintable(trashLoc.errors.join(QLatin1Char(';'))));
  QCOMPARE(trashLoc.manifest.runtime(QStringLiteral("location")),
           QStringLiteral("core"));
  QCOMPARE(trashLoc.manifest.coreVerb(QStringLiteral("location")),
           QStringLiteral("trash"));

  const auto volumes = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.location.volumes")), true);
  QVERIFY2(volumes.ok, qPrintable(volumes.errors.join(QLatin1Char(';'))));
  QCOMPARE(volumes.manifest.coreVerb(QStringLiteral("location")),
           QStringLiteral("volumes"));
  QCOMPARE(volumes.manifest.entryPoints.value(QStringLiteral("row")),
           QStringLiteral("Row.qml"));
  QCOMPARE(volumes.manifest.entryPoints.value(QStringLiteral("thumb")),
           QStringLiteral("Thumb.qml"));

  const auto eject = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.action.eject")), true);
  QVERIFY2(eject.ok, qPrintable(eject.errors.join(QLatin1Char(';'))));
  QCOMPARE(eject.manifest.coreVerb(QStringLiteral("action")),
           QStringLiteral("eject"));
}

void ManifestValidateTest::firstPartyPreviewHandlersValid() {
  const QString root = QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR);
  for (const char *id : {"synchro.preview.text", "synchro.preview.markdown",
                         "synchro.preview.pdf", "synchro.preview.image",
                         "synchro.preview.video", "synchro.preview.folder",
                         "synchro.preview.parquet", "synchro.preview.sqlite",
                         "synchro.preview.duckdb", "synchro.preview.archive"}) {
    const auto v =
        validateManifestDir(QDir(root).filePath(QLatin1String(id)), true);
    QVERIFY2(v.ok, qPrintable(v.errors.join(QLatin1Char(';'))));
    QVERIFY(v.manifest.hasKind(QStringLiteral("preview")));
  }
  const auto text = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.preview.text")), true);
  QVERIFY(text.manifest.hasKind(QStringLiteral("thumbnail")));
  QCOMPARE(text.manifest.runtime(QStringLiteral("thumbnail")),
           QStringLiteral("core"));
}

void ManifestValidateTest::firstPartyPanelTerminalValid() {
  const QString root = QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR);
  const auto panel = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.panel.terminal")), true);
  QVERIFY2(panel.ok, qPrintable(panel.errors.join(QLatin1Char(';'))));
  QVERIFY(panel.manifest.kinds.contains(QStringLiteral("panel")));
  QCOMPARE(panel.manifest.entryPoints.value(QStringLiteral("panel")),
           QStringLiteral("Panel.qml"));
  QCOMPARE(panel.manifest.panel.value(QStringLiteral("relevance")).toString(),
           QStringLiteral("always"));
  QCOMPARE(panel.manifest.panel.value(QStringLiteral("group")).toString(),
           QStringLiteral("workspace"));
  QCOMPARE(panel.manifest.panel.value(QStringLiteral("order")).toInt(), 10);

  const auto data = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.panel.duckdb")), true);
  QVERIFY2(data.ok, qPrintable(data.errors.join(QLatin1Char(';'))));
  QVERIFY(data.manifest.kinds.contains(QStringLiteral("panel")));
  QVERIFY(data.manifest.match.suffix.contains(QStringLiteral(".parquet")));
  QCOMPARE(data.manifest.panel.value(QStringLiteral("relevance")).toString(),
           QStringLiteral("match"));

  const auto sql = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.panel.sql")), true);
  QVERIFY2(sql.ok, qPrintable(sql.errors.join(QLatin1Char(';'))));
  QVERIFY(sql.manifest.kinds.contains(QStringLiteral("panel")));
  QCOMPARE(sql.manifest.entryPoints.value(QStringLiteral("panel")),
           QStringLiteral("Panel.qml"));
  QCOMPARE(sql.manifest.panel.value(QStringLiteral("relevance")).toString(),
           QStringLiteral("manual"));
  QCOMPARE(sql.manifest.panel.value(QStringLiteral("group")).toString(),
           QStringLiteral("workspace"));
  QCOMPARE(sql.manifest.panel.value(QStringLiteral("order")).toInt(), 20);

  const auto flow = validateManifestDir(
      QDir(root).filePath(QStringLiteral("synchro.panel.omaflow")), true);
  QVERIFY2(flow.ok, qPrintable(flow.errors.join(QLatin1Char(';'))));
  QVERIFY(flow.manifest.kinds.contains(QStringLiteral("panel")));
  QCOMPARE(flow.manifest.entryPoints.value(QStringLiteral("panel")),
           QStringLiteral("Panel.qml"));
  QCOMPARE(flow.manifest.panel.value(QStringLiteral("relevance")).toString(),
           QStringLiteral("manual"));
  QCOMPARE(flow.manifest.panel.value(QStringLiteral("group")).toString(),
           QStringLiteral("workspace"));
  QCOMPARE(flow.manifest.panel.value(QStringLiteral("order")).toInt(), 30);
}

void ManifestValidateTest::actionCoreRequiresVerb() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir =
      writeHandler(tmp, QStringLiteral("acme.core"),
                   QByteArrayLiteral("{\n"
                                     "  \"schemaVersion\": 1,\n"
                                     "  \"id\": \"acme.core\",\n"
                                     "  \"name\": \"C\",\n"
                                     "  \"version\": \"1\",\n"
                                     "  \"kinds\": [\"action\"],\n"
                                     "  \"entryPoints\": {},\n"
                                     "  \"action\": { \"runtime\": \"core\" }\n"
                                     "}\n"));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("verb")));
}

void ManifestValidateTest::schemaVersionMustBeNumberOne() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir =
      writeHandler(tmp, QStringLiteral("acme.one"),
                   QByteArrayLiteral("{\n"
                                     "  \"schemaVersion\": \"1\",\n"
                                     "  \"id\": \"acme.one\",\n"
                                     "  \"name\": \"One\",\n"
                                     "  \"version\": \"1\",\n"
                                     "  \"kinds\": [\"open\"],\n"
                                     "  \"entryPoints\": {},\n"
                                     "  \"open\": { \"exec\": \"true %f\" }\n"
                                     "}\n"));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' '))
              .contains(QStringLiteral("schemaVersion")));
}

void ManifestValidateTest::requiredFields() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir =
      writeHandler(tmp, QStringLiteral("acme.missing"),
                   QByteArrayLiteral(
                       "{ \"schemaVersion\": 1, \"id\": \"acme.missing\" }\n"));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("name")));
}

void ManifestValidateTest::invalidId() {
  QVERIFY(!Manifest::isValidId(QStringLiteral("../x")));
  QVERIFY(!Manifest::isValidId(QStringLiteral("a/b")));
  QVERIFY(!Manifest::isValidId(QString()));
  QVERIFY(Manifest::isValidId(QStringLiteral("acme.photos")));
}

void ManifestValidateTest::reservedIdRejectedForThirdParty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("synchro.preview.image"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"synchro.preview.image\",\n"
                        "  \"name\": \"Steal\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"preview\"],\n"
                        "  \"entryPoints\": { \"preview\": \"Preview.qml\" }\n"
                        "}\n"),
      true);
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("reserved")));
}

void ManifestValidateTest::reservedIdOkFirstParty() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("synchro.preview.image"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"synchro.preview.image\",\n"
                        "  \"name\": \"Image\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"preview\"],\n"
                        "  \"entryPoints\": { \"preview\": \"Preview.qml\" }\n"
                        "}\n"),
      true);
  const auto v = validateManifestDir(dir, true);
  QVERIFY2(v.ok, qPrintable(v.errors.join(QLatin1Char(';'))));
}

void ManifestValidateTest::unsafeEntryPoints() {
  QVERIFY(!Manifest::isSafeEntryPoint(QString()));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("/etc/passwd")));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("../Peek.qml")));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("foo/../bar.qml")));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("a\nb.qml")));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("a\rb.qml")));
  QVERIFY(Manifest::isSafeEntryPoint(QStringLiteral("Preview.qml")));
  QVERIFY(Manifest::isSafeEntryPoint(QStringLiteral("ui/Preview.qml")));

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.bad"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"acme.bad\",\n"
                        "  \"name\": \"Bad\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"preview\"],\n"
                        "  \"entryPoints\": { \"preview\": \"../Peek.qml\" }\n"
                        "}\n"));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
}

void ManifestValidateTest::missingEntryPointFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.missingqml"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"acme.missingqml\",\n"
                        "  \"name\": \"M\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"preview\"],\n"
                        "  \"entryPoints\": { \"preview\": \"Preview.qml\" }\n"
                        "}\n"));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(
      v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("not found")));
}

void ManifestValidateTest::replaceListingRejected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.folder"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"acme.folder\",\n"
                        "  \"name\": \"Folder\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"folder\"],\n"
                        "  \"entryPoints\": { \"folder\": \"Preview.qml\" },\n"
                        "  \"folder\": { \"replaceListing\": true }\n"
                        "}\n"),
      true);
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' '))
              .contains(QStringLiteral("replaceListing")));
}

void ManifestValidateTest::locationRuntimes() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString pathOk = writeHandler(
      tmp, QStringLiteral("acme.home"),
      QByteArrayLiteral(
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"id\": \"acme.home\",\n"
          "  \"name\": \"Home\",\n"
          "  \"version\": \"1\",\n"
          "  \"kinds\": [\"location\"],\n"
          "  \"entryPoints\": {},\n"
          "  \"location\": { \"runtime\": \"path\", \"path\": \"$HOME\" }\n"
          "}\n"));
  QVERIFY2(validateManifestDir(pathOk, false).ok,
           qPrintable(validateManifestDir(pathOk, false).errors.join(';')));

  const QString coreOk = writeHandler(
      tmp, QStringLiteral("acme.trash"),
      QByteArrayLiteral(
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"id\": \"acme.trash\",\n"
          "  \"name\": \"Trash\",\n"
          "  \"version\": \"1\",\n"
          "  \"kinds\": [\"location\"],\n"
          "  \"entryPoints\": {},\n"
          "  \"location\": { \"runtime\": \"core\", \"adapter\": \"trash\" }\n"
          "}\n"));
  QVERIFY(validateManifestDir(coreOk, false).ok);

  const QString badCore = writeHandler(
      tmp, QStringLiteral("acme.badcore"),
      QByteArrayLiteral(
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"id\": \"acme.badcore\",\n"
          "  \"name\": \"X\",\n"
          "  \"version\": \"1\",\n"
          "  \"kinds\": [\"location\"],\n"
          "  \"entryPoints\": {},\n"
          "  \"location\": { \"runtime\": \"core\", \"adapter\": \"nope\" }\n"
          "}\n"));
  QVERIFY(!validateManifestDir(badCore, false).ok);

  const QString chrome = writeHandler(
      tmp, QStringLiteral("acme.chrome"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"acme.chrome\",\n"
                        "  \"name\": \"C\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"location\"],\n"
                        "  \"entryPoints\": {},\n"
                        "  \"location\": { \"runtime\": \"chrome\" }\n"
                        "}\n"));
  QVERIFY(!validateManifestDir(chrome, false).ok);
}

void ManifestValidateTest::previewRequiresEntryPoint() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir =
      writeHandler(tmp, QStringLiteral("acme.prev"),
                   QByteArrayLiteral("{\n"
                                     "  \"schemaVersion\": 1,\n"
                                     "  \"id\": \"acme.prev\",\n"
                                     "  \"name\": \"P\",\n"
                                     "  \"version\": \"1\",\n"
                                     "  \"kinds\": [\"preview\"],\n"
                                     "  \"entryPoints\": {}\n"
                                     "}\n"));
  QVERIFY(!validateManifestDir(dir, false).ok);
}

void ManifestValidateTest::openExecOnlyOk() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.open"),
      QByteArrayLiteral(
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"id\": \"acme.open\",\n"
          "  \"name\": \"O\",\n"
          "  \"version\": \"1\",\n"
          "  \"kinds\": [\"open\"],\n"
          "  \"entryPoints\": {},\n"
          "  \"open\": { \"runtime\": \"exec\", \"exec\": \"true %f\" }\n"
          "}\n"));
  QVERIFY2(validateManifestDir(dir, false).ok,
           qPrintable(validateManifestDir(dir, false).errors.join(';')));
}

void ManifestValidateTest::symlinkRejected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString dir =
      writeHandler(tmp, QStringLiteral("acme.link"),
                   QByteArrayLiteral("{\n"
                                     "  \"schemaVersion\": 1,\n"
                                     "  \"id\": \"acme.link\",\n"
                                     "  \"name\": \"L\",\n"
                                     "  \"version\": \"1\",\n"
                                     "  \"kinds\": [\"open\"],\n"
                                     "  \"entryPoints\": {},\n"
                                     "  \"open\": { \"exec\": \"true %f\" }\n"
                                     "}\n"));
  QVERIFY(QFile::link(QStringLiteral("/tmp"), dir + QStringLiteral("/oops")));
  QVERIFY(!validateManifestDir(dir, false).ok);
}

void ManifestValidateTest::isSafeEntryPointCopy() {
  QVERIFY(Manifest::isSafeEntryPoint(QStringLiteral("Preview.qml")));
  QVERIFY(!Manifest::isSafeEntryPoint(QStringLiteral("Preview.qml\n")));
}

void ManifestValidateTest::gitSymlinkEntryPointRejected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString outside = tmp.filePath(QStringLiteral("outside"));
  QVERIFY(QDir().mkpath(outside));
  QVERIFY(writeText(outside + QStringLiteral("/Preview.qml"),
                    QByteArrayLiteral("import QtQuick\nItem {}\n")));
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.gitpeek"),
      QByteArrayLiteral(
          "{\n"
          "  \"schemaVersion\": 1,\n"
          "  \"id\": \"acme.gitpeek\",\n"
          "  \"name\": \"G\",\n"
          "  \"version\": \"1\",\n"
          "  \"kinds\": [\"preview\"],\n"
          "  \"entryPoints\": { \"preview\": \".git/Preview.qml\" }\n"
          "}\n"));
  QVERIFY(QFile::link(outside, dir + QStringLiteral("/.git")));
  const auto v = validateManifestDir(dir, false);
  QVERIFY(!v.ok);
  QVERIFY(v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("escapes")) ||
          v.errors.join(QLatin1Char(' ')).contains(QStringLiteral("symlink")));
}

void ManifestValidateTest::entryFileSymlinkRejected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString outside = tmp.filePath(QStringLiteral("outside.qml"));
  QVERIFY(writeText(outside, QByteArrayLiteral("import QtQuick\nItem {}\n")));
  const QString dir = writeHandler(
      tmp, QStringLiteral("acme.linkep"),
      QByteArrayLiteral("{\n"
                        "  \"schemaVersion\": 1,\n"
                        "  \"id\": \"acme.linkep\",\n"
                        "  \"name\": \"L\",\n"
                        "  \"version\": \"1\",\n"
                        "  \"kinds\": [\"preview\"],\n"
                        "  \"entryPoints\": { \"preview\": \"Preview.qml\" }\n"
                        "}\n"));
  QVERIFY(QFile::link(outside, dir + QStringLiteral("/Preview.qml")));
  QString err;
  QVERIFY(!Manifest::confineEntryPoint(dir, QStringLiteral("Preview.qml"),
                                       nullptr, &err));
  QVERIFY(err.contains(QStringLiteral("symlink")) ||
          err.contains(QStringLiteral("escapes")));
  QVERIFY(!validateManifestDir(dir, false).ok);
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ManifestValidateTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "manifest_validate_test.moc"
