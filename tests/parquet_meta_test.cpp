#include "ParquetMeta.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class ParquetMetaTest : public QObject {
  Q_OBJECT

private slots:
  void rejectsPlainText();
  void parseDuckdbFile();
};

void ParquetMetaTest::rejectsPlainText() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("notes.txt"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello\n");
  }
  QVERIFY(!ParquetMeta::looksLike(path, QStringLiteral("text/plain")));
  const ParquetInfo info = ParquetMeta::parse(path);
  QVERIFY(!info.ok);
}

void ParquetMetaTest::parseDuckdbFile() {
  if (QStandardPaths::findExecutable(QStringLiteral("duckdb")).isEmpty())
    QSKIP("duckdb not installed");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("t.parquet"));
  QProcess proc;
  proc.start(QStringLiteral("duckdb"),
             {QStringLiteral("-c"),
              QStringLiteral("COPY (SELECT 7 AS id, 'hi' AS name) TO '%1' "
                             "(FORMAT PARQUET)")
                  .arg(path)});
  QVERIFY(proc.waitForFinished(8000));
  QCOMPARE(proc.exitCode(), 0);
  QVERIFY(QFileInfo::exists(path));
  QVERIFY(ParquetMeta::looksLike(path, QString()));
  const ParquetInfo info = ParquetMeta::parseWithSample(path, 8);
  QVERIFY2(info.ok, qPrintable(info.error));
  QCOMPARE(info.numRows, 1);
  QVERIFY(info.columns.size() >= 2);
  QStringList names;
  for (const ParquetColumn &c : info.columns)
    names.append(c.name);
  QVERIFY(names.contains(QStringLiteral("id")));
  QVERIFY(names.contains(QStringLiteral("name")));
  QVERIFY(!info.sample.isEmpty());
  const QVariantMap row = info.sample.at(0).toMap();
  QCOMPARE(row.value(QStringLiteral("id")).toInt(), 7);
  QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("hi"));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ParquetMetaTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "parquet_meta_test.moc"
