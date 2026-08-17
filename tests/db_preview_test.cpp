#include "DbPreview.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

#include <sqlite3.h>

namespace {

bool writeSqlite(const QString &path, const char *sql) {
  sqlite3 *db = nullptr;
  if (sqlite3_open(QFile::encodeName(path).constData(), &db) != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return false;
  }
  char *err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  sqlite3_free(err);
  sqlite3_close(db);
  return rc == SQLITE_OK;
}

} // namespace

class DbPreviewTest : public QObject {
  Q_OBJECT

private slots:
  void rejectsPlainText();
  void inspectSqliteTablesAndSample();
  void quotedTableName();
  void inspectDuckDb();
};

void DbPreviewTest::rejectsPlainText() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("notes.txt"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello\n");
  }
  QVERIFY(!DbPreview::looksLikeSqlite(path));
  QVERIFY(!DbPreview::looksLikeDuckDb(path));
  const QVariantMap info =
      DbPreview::inspect(path, QStringLiteral("sqlite"));
  QVERIFY(!info.value(QStringLiteral("ok")).toBool());
}

void DbPreviewTest::inspectSqliteTablesAndSample() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("app.db"));
  QVERIFY(writeSqlite(
      path, "CREATE TABLE tweets(id INTEGER PRIMARY KEY, body TEXT);"
            "INSERT INTO tweets VALUES (1, 'hi');"
            "INSERT INTO tweets VALUES (2, 'yo');"
            "CREATE TABLE users(name TEXT);"
            "INSERT INTO users VALUES ('ada');"));
  QVERIFY(DbPreview::looksLikeSqlite(path));
  const QVariantMap info =
      DbPreview::inspect(path, QStringLiteral("sqlite"));
  QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
           qPrintable(info.value(QStringLiteral("error")).toString()));
  QCOMPARE(info.value(QStringLiteral("engine")).toString(),
           QStringLiteral("sqlite"));
  const QVariantList tables = info.value(QStringLiteral("tables")).toList();
  QCOMPARE(tables.size(), 2);
  QCOMPARE(info.value(QStringLiteral("table")).toString(),
           QStringLiteral("tweets"));
  const QVariantList sample = info.value(QStringLiteral("sample")).toList();
  QCOMPARE(sample.size(), 2);
  QCOMPARE(sample.at(0).toMap().value(QStringLiteral("body")).toString(),
           QStringLiteral("hi"));

  const QVariantMap users = DbPreview::inspect(
      path, QStringLiteral("sqlite"), QStringLiteral("users"));
  QVERIFY(users.value(QStringLiteral("ok")).toBool());
  QCOMPARE(users.value(QStringLiteral("table")).toString(),
           QStringLiteral("users"));
  const QVariantList urows = users.value(QStringLiteral("sample")).toList();
  QCOMPARE(urows.size(), 1);
  QCOMPARE(urows.at(0).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("ada"));
}

void DbPreviewTest::quotedTableName() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("odd.sqlite"));
  QVERIFY(writeSqlite(path, "CREATE TABLE \"order\"(n INTEGER);"
                            "INSERT INTO \"order\" VALUES (7);"));
  const QVariantMap info = DbPreview::inspect(
      path, QStringLiteral("sqlite"), QStringLiteral("order"));
  QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
           qPrintable(info.value(QStringLiteral("error")).toString()));
  QCOMPARE(info.value(QStringLiteral("table")).toString(),
           QStringLiteral("order"));
  QCOMPARE(info.value(QStringLiteral("sample")).toList().size(), 1);
  QCOMPARE(info.value(QStringLiteral("sample"))
               .toList()
               .at(0)
               .toMap()
               .value(QStringLiteral("n"))
               .toString(),
           QStringLiteral("7"));
}

void DbPreviewTest::inspectDuckDb() {
  if (QStandardPaths::findExecutable(QStringLiteral("duckdb")).isEmpty())
    QSKIP("duckdb not installed");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("t.duckdb"));
  QProcess proc;
  proc.start(QStringLiteral("duckdb"),
             {path, QStringLiteral("-c"),
              QStringLiteral("CREATE TABLE t(id INTEGER, name VARCHAR); "
                             "INSERT INTO t VALUES (7, 'hi');")});
  QVERIFY(proc.waitForFinished(8000));
  QCOMPARE(proc.exitCode(), 0);
  QVERIFY(QFileInfo::exists(path));
  QVERIFY(DbPreview::looksLikeDuckDb(path));
  const QVariantMap info =
      DbPreview::inspect(path, QStringLiteral("duckdb"));
  QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
           qPrintable(info.value(QStringLiteral("error")).toString()));
  QCOMPARE(info.value(QStringLiteral("table")).toString(),
           QStringLiteral("t"));
  const QVariantList sample = info.value(QStringLiteral("sample")).toList();
  QVERIFY(!sample.isEmpty());
  QCOMPARE(sample.at(0).toMap().value(QStringLiteral("name")).toString(),
           QStringLiteral("hi"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  DbPreviewTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "db_preview_test.moc"
