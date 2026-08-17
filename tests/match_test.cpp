#include "HandlerRegistry.h"
#include "Manifest.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
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

QString writePreview(const QString &root, const QString &id, int priority,
                     const QByteArray &mimeJson,
                     const QByteArray &extraMatch = QByteArray()) {
  const QString dir = QDir(root).filePath(id);
  QDir().mkpath(dir);
  QByteArray body = "{\n  \"schemaVersion\": 1,\n  \"id\": \"";
  body += id.toUtf8();
  body += "\",\n  \"name\": \"";
  body += id.toUtf8();
  body += "\",\n  \"version\": \"1\",\n  \"kinds\": [\"preview\"],\n"
          "  \"priority\": ";
  body += QByteArray::number(priority);
  body += ",\n  \"match\": {\n    \"mime\": ";
  body += mimeJson;
  if (!extraMatch.isEmpty()) {
    body += ",\n    ";
    body += extraMatch;
  }
  body += "\n  },\n  \"entryPoints\": { \"preview\": \"Preview.qml\" }\n}\n";
  writeText(dir + QStringLiteral("/manifest.json"), body);
  writeText(dir + QStringLiteral("/Preview.qml"),
            QByteArrayLiteral("import QtQuick\nItem {}\n"));
  return dir;
}

} // namespace

class MatchTest : public QObject {
  Q_OBJECT

private slots:
  void pathGlobBasename();
  void pathGlobDoubleStar();
  void pathGlobSegmentWildcards();
  void mimeExactAndGlob();
  void mimeModeDefaultAll();
  void mimeModeAny();
  void suffixAndHost();
  void matchModeAnyORsClauses();
  void minMaxItems();
  void resolveOrder();
  void scanFirstPartyAndUser();
  void reservedNotOverridden();
  void thirdPartyDisabledByDefault();
  void folderContains();
  void omawriteBeatsXdgWhenPresent();
  void omacutBeatsXdgWhenPresent();
  void enableDisablePersist();
};

void MatchTest::pathGlobBasename() {
  const QString path = QStringLiteral("/home/ryanr/Projects/foo/README.md");
  QVERIFY(Manifest::matchPathGlob(path, QStringLiteral("*.md")));
  QVERIFY(Manifest::matchPathGlob(path, QStringLiteral("README.md")));
  QVERIFY(!Manifest::matchPathGlob(path, QStringLiteral("*.txt")));
  QVERIFY(!Manifest::matchPathGlob(path, QStringLiteral("foo")));
}

void MatchTest::pathGlobDoubleStar() {
  const QString path = QStringLiteral("/home/ryanr/Projects/foo/README.md");
  QVERIFY(Manifest::matchPathGlob(path, QStringLiteral("**/*.md")));
  QVERIFY(Manifest::matchPathGlob(path, QStringLiteral("**/README.md")));
  QVERIFY(Manifest::matchPathGlob(path, QStringLiteral("**/foo/**")));
  QVERIFY(Manifest::matchPathGlob(QStringLiteral("/a/b/c.cpp"),
                                  QStringLiteral("**/*.cpp")));
  QVERIFY(Manifest::matchPathGlob(QStringLiteral("/c.cpp"),
                                  QStringLiteral("**/*.cpp")));
  QVERIFY(!Manifest::matchPathGlob(path, QStringLiteral("**/*.cpp")));
}

void MatchTest::pathGlobSegmentWildcards() {
  QVERIFY(Manifest::matchPathGlob(QStringLiteral("/tmp/a/b.txt"),
                                  QStringLiteral("/tmp/*/b.txt")));
  QVERIFY(!Manifest::matchPathGlob(QStringLiteral("/tmp/a/x/b.txt"),
                                   QStringLiteral("/tmp/*/b.txt")));
  QVERIFY(Manifest::matchPathGlob(QStringLiteral("/tmp/a/x/b.txt"),
                                  QStringLiteral("/tmp/**/b.txt")));
  QVERIFY(Manifest::matchPathGlob(QStringLiteral("/tmp/ab"),
                                  QStringLiteral("/tmp/a?")));
  QVERIFY(!Manifest::matchPathGlob(QStringLiteral("/tmp/a/b"),
                                   QStringLiteral("/tmp/a?")));
}

void MatchTest::mimeExactAndGlob() {
  QVERIFY(Manifest::matchMime(QStringLiteral("image/png"),
                              QStringLiteral("image/png")));
  QVERIFY(Manifest::matchMime(QStringLiteral("image/png"),
                              QStringLiteral("image/*")));
  QVERIFY(Manifest::matchMime(QStringLiteral("image/svg+xml"),
                              QStringLiteral("image/*")));
  QVERIFY(!Manifest::matchMime(QStringLiteral("text/plain"),
                               QStringLiteral("image/*")));
  QVERIFY(Manifest::matchMime(QStringLiteral("text/plain"),
                              QStringLiteral("*/*")));
}

void MatchTest::mimeModeDefaultAll() {
  Manifest m;
  m.kinds = {QStringLiteral("preview")};
  m.match.mime = {QStringLiteral("image/*")};
  m.match.mimeMode = QStringLiteral("all");
  QVERIFY(manifestMatches(m, QStringLiteral("preview"),
                          {item(QStringLiteral("/a.png"),
                                QStringLiteral("image/png"))}));
  QVERIFY(!manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/a.png"), QStringLiteral("image/png")),
       item(QStringLiteral("/b.txt"), QStringLiteral("text/plain"))}));
}

void MatchTest::mimeModeAny() {
  Manifest m;
  m.kinds = {QStringLiteral("preview")};
  m.match.mime = {QStringLiteral("image/*")};
  m.match.mimeMode = QStringLiteral("any");
  m.match.minItems = 1;
  m.match.maxItems = 2;
  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/a.png"), QStringLiteral("image/png")),
       item(QStringLiteral("/b.txt"), QStringLiteral("text/plain"))}));
}

void MatchTest::suffixAndHost() {
  Manifest m;
  m.kinds = {QStringLiteral("open")};
  m.match.suffix = {QStringLiteral(".md"), QStringLiteral(".markdown")};
  QVERIFY(manifestMatches(m, QStringLiteral("open"),
                          {item(QStringLiteral("/tmp/README.md"),
                                QStringLiteral("text/plain"))}));
  QVERIFY(!manifestMatches(m, QStringLiteral("open"),
                           {item(QStringLiteral("/tmp/README.txt"),
                                 QStringLiteral("text/plain"))}));
  m.match.host = QStringLiteral("remote");
  QVERIFY(!manifestMatches(m, QStringLiteral("open"),
                           {item(QStringLiteral("/tmp/README.md"),
                                 QStringLiteral("text/plain"))}));
}

void MatchTest::matchModeAnyORsClauses() {
  Manifest m;
  m.kinds = {QStringLiteral("preview")};
  m.match.matchMode = QStringLiteral("any");
  m.match.mime = {QStringLiteral("text/*")};
  m.match.suffix = {QStringLiteral(".sql"), QStringLiteral(".yaml"),
                    QStringLiteral(".ts")};
  m.match.pathGlob = {QStringLiteral("Dockerfile"),
                      QStringLiteral("Dockerfile.*")};

  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/schema.sql"),
            QStringLiteral("application/sql"))}));
  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/app.yaml"),
            QStringLiteral("application/yaml"))}));
  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/Dockerfile"),
            QStringLiteral("application/octet-stream"))}));
  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/Dockerfile.dev"),
            QStringLiteral("application/octet-stream"))}));
  QVERIFY(manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/notes.ts"),
            QStringLiteral("video/mp2t"))}));
  QVERIFY(!manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/photo.png"),
            QStringLiteral("image/png"))}));
}

void MatchTest::minMaxItems() {
  Manifest m;
  m.kinds = {QStringLiteral("preview")};
  QVERIFY(!manifestMatches(m, QStringLiteral("preview"), {}));
  QVERIFY(manifestMatches(m, QStringLiteral("preview"),
                          {item(QStringLiteral("/a"), QStringLiteral("x"))}));
  QVERIFY(!manifestMatches(
      m, QStringLiteral("preview"),
      {item(QStringLiteral("/a"), QStringLiteral("x")),
       item(QStringLiteral("/b"), QStringLiteral("x"))}));
}

void MatchTest::resolveOrder() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writePreview(tmp.path(), QStringLiteral("acme.exact"), 50,
               QByteArrayLiteral("[\"image/png\"]"));
  writePreview(tmp.path(), QStringLiteral("acme.glob"), 50,
               QByteArrayLiteral("[\"image/*\"]"));
  writePreview(tmp.path(), QStringLiteral("acme.high"), 90,
               QByteArrayLiteral("[\"image/png\"]"));

  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  const QString cfg = tmp.filePath(QStringLiteral("handlers.json"));
  writeText(cfg, QByteArrayLiteral(
                     "{ \"version\": 1, \"enabled\": "
                     "[\"acme.exact\", \"acme.glob\", \"acme.high\"] }\n"));
  reg.setConfigPath(cfg);
  reg.scan();

  const auto matches = reg.resolve(
      QStringLiteral("preview"),
      {item(QStringLiteral("/tmp/a.png"), QStringLiteral("image/png"))});
  QCOMPARE(matches.size(), 3);
  // priority first, then exact MIME over glob
  QCOMPARE(matches.at(0).id, QStringLiteral("acme.high"));
  QCOMPARE(matches.at(1).id, QStringLiteral("acme.exact"));
  QCOMPARE(matches.at(2).id, QStringLiteral("acme.glob"));
}

void MatchTest::scanFirstPartyAndUser() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.preview.image")));
  QVERIFY(reg.contains(QStringLiteral("synchro.open.xdg")));
  QCOMPARE(reg.handler(QStringLiteral("synchro.preview.image")).enabled, true);
}

void MatchTest::reservedNotOverridden() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writePreview(tmp.path(), QStringLiteral("synchro.preview.image"), 99,
               QByteArrayLiteral("[\"image/png\"]"));
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.scan();
  const auto rec = reg.handler(QStringLiteral("synchro.preview.image"));
  QVERIFY(rec.firstParty);
  QVERIFY(rec.sourceDir.contains(QStringLiteral("handlers/synchro.preview.image")));
}

void MatchTest::thirdPartyDisabledByDefault() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writePreview(tmp.path(), QStringLiteral("acme.photos"), 70,
               QByteArrayLiteral("[\"image/*\"]"));
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("acme.photos")));
  QVERIFY(!reg.handler(QStringLiteral("acme.photos")).enabled);
  QCOMPARE(reg.resolve(QStringLiteral("preview"),
                       {item(QStringLiteral("/a.png"),
                             QStringLiteral("image/png"))})
               .size(),
           0);
}

void MatchTest::omawriteBeatsXdgWhenPresent() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.open.omawrite")));
  QVERIFY(reg.contains(QStringLiteral("synchro.open.xdg")));
  QVERIFY(reg.handler(QStringLiteral("synchro.open.omawrite")).enabled);

  const auto matches = reg.resolve(
      QStringLiteral("open"),
      {item(QStringLiteral("/tmp/README.md"), QStringLiteral("text/markdown"))});
  QVERIFY(!matches.isEmpty());
  const bool hasOma =
      !QStandardPaths::findExecutable(QStringLiteral("omawrite")).isEmpty();
  QStringList ids;
  for (const auto &m : matches)
    ids.append(m.id);
  if (hasOma) {
    QCOMPARE(matches.constFirst().id, QStringLiteral("synchro.open.omawrite"));
  } else {
    QVERIFY(!ids.contains(QStringLiteral("synchro.open.omawrite")));
    QVERIFY(ids.contains(QStringLiteral("synchro.open.xdg")));
  }
}

void MatchTest::omacutBeatsXdgWhenPresent() {
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(tmp.filePath(QStringLiteral("none.json")));
  reg.scan();
  QVERIFY(reg.contains(QStringLiteral("synchro.open.omacut")));
  QVERIFY(reg.contains(QStringLiteral("synchro.action.agent")));

  const auto matches = reg.resolve(
      QStringLiteral("open"),
      {item(QStringLiteral("/tmp/clip.mp4"), QStringLiteral("video/mp4"))});
  QVERIFY(!matches.isEmpty());
  const bool hasCut =
      !QStandardPaths::findExecutable(QStringLiteral("omacut")).isEmpty();
  QStringList ids;
  for (const auto &m : matches)
    ids.append(m.id);
  if (hasCut) {
    QCOMPARE(matches.constFirst().id, QStringLiteral("synchro.open.omacut"));
  } else {
    QVERIFY(!ids.contains(QStringLiteral("synchro.open.omacut")));
    QVERIFY(ids.contains(QStringLiteral("synchro.open.xdg")));
  }
}

void MatchTest::enableDisablePersist() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  writePreview(tmp.path(), QStringLiteral("acme.photos"), 70,
               QByteArrayLiteral("[\"image/*\"]"));
  const QString cfg = tmp.filePath(QStringLiteral("handlers.json"));
  HandlerRegistry reg;
  reg.setFirstPartyDir(QStringLiteral("/nonexistent"));
  reg.setUserDir(tmp.path());
  reg.setScanEnv(false);
  reg.setConfigPath(cfg);
  reg.scan();
  QVERIFY(!reg.handler(QStringLiteral("acme.photos")).enabled);
  QString err;
  QVERIFY(reg.setEnabled(QStringLiteral("acme.photos"), true, &err));
  QVERIFY(reg.handler(QStringLiteral("acme.photos")).enabled);
  QVERIFY(QFileInfo::exists(cfg));

  HandlerRegistry again;
  again.setFirstPartyDir(QStringLiteral("/nonexistent"));
  again.setUserDir(tmp.path());
  again.setScanEnv(false);
  again.setConfigPath(cfg);
  again.scan();
  QVERIFY(again.handler(QStringLiteral("acme.photos")).enabled);
  QVERIFY(again.setEnabled(QStringLiteral("acme.photos"), false, &err));
  QVERIFY(!again.handler(QStringLiteral("acme.photos")).enabled);
}

void MatchTest::folderContains() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("repo")));
  QVERIFY(writeText(tmp.filePath(QStringLiteral("repo/.git")),
                    QByteArrayLiteral("gitdir")));
  Manifest m;
  m.kinds = {QStringLiteral("folder")};
  m.match.folderContains = {QStringLiteral(".git")};
  QVERIFY(manifestMatches(m, QStringLiteral("folder"),
                          {item(tmp.filePath(QStringLiteral("repo")),
                                QStringLiteral("inode/directory"), true)}));
  QVERIFY(!manifestMatches(m, QStringLiteral("folder"),
                           {item(tmp.path(), QStringLiteral("inode/directory"),
                                 true)}));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  MatchTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "match_test.moc"
