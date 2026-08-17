#include "ArchiveMeta.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>

namespace {

bool runPy(const QString &code) {
  QProcess proc;
  proc.start(QStringLiteral("python3"), {QStringLiteral("-c"), code});
  return proc.waitForFinished(8000) && proc.exitCode() == 0;
}

} // namespace

class ArchiveMetaTest : public QObject {
  Q_OBJECT

private slots:
  void rejectsPlainText();
  void inspectZipMembers();
  void inspectTarMembers();
  void inspectGzipMeta();
  void inspectTarGzMembers();
};

void ArchiveMetaTest::rejectsPlainText() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("notes.txt"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello\n");
  }
  QVERIFY(!ArchiveMeta::looksLike(path, QStringLiteral("text/plain")));
  const ArchiveInfo info = ArchiveMeta::inspect(path);
  QVERIFY(!info.ok);
}

void ArchiveMetaTest::inspectZipMembers() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("pack.zip"));
  QVERIFY(runPy(QStringLiteral(
      "import zipfile; z=zipfile.ZipFile(%1,'w');"
      "z.writestr('readme.txt','hi');"
      "z.writestr('src/a.c','int x;');"
      "z.writestr('src/','');"
      "z.close()")
                    .arg(QLatin1Char('\'') + path + QLatin1Char('\''))));
  QVERIFY(ArchiveMeta::looksLike(path, QStringLiteral("application/zip")));
  const ArchiveInfo info = ArchiveMeta::inspect(path);
  QVERIFY2(info.ok, qPrintable(info.error));
  QCOMPARE(info.format, QStringLiteral("zip"));
  QVERIFY(info.files >= 2);
  QStringList names;
  for (const ArchiveEntry &e : info.entries)
    names.append(e.name);
  QVERIFY(names.contains(QStringLiteral("readme.txt")));
  QVERIFY(names.filter(QStringLiteral("a.c")).size() >= 1);
}

void ArchiveMetaTest::inspectTarMembers() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("pack.tar"));
  QVERIFY(runPy(QStringLiteral(
      "import tarfile,io; t=tarfile.open(%1,'w');"
      "b=io.BytesIO(b'hello'); info=tarfile.TarInfo('hello.txt');"
      "info.size=5; t.addfile(info,b);"
      "d=tarfile.TarInfo('sub'); d.type=tarfile.DIRTYPE; t.addfile(d);"
      "t.close()")
                    .arg(QLatin1Char('\'') + path + QLatin1Char('\''))));
  QVERIFY(ArchiveMeta::looksLike(path, QString()));
  const ArchiveInfo info = ArchiveMeta::inspect(path);
  QVERIFY2(info.ok, qPrintable(info.error));
  QCOMPARE(info.format, QStringLiteral("tar"));
  QVERIFY(info.files >= 1);
  QStringList names;
  for (const ArchiveEntry &e : info.entries)
    names.append(e.name);
  QVERIFY(names.contains(QStringLiteral("hello.txt")));
}

void ArchiveMetaTest::inspectGzipMeta() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("log.gz"));
  QVERIFY(runPy(QStringLiteral(
      "import gzip; f=gzip.open(%1,'wt'); f.write('line\\n'*20); f.close()")
                    .arg(QLatin1Char('\'') + path + QLatin1Char('\''))));
  QVERIFY(ArchiveMeta::looksLike(path, QStringLiteral("application/gzip")));
  const ArchiveInfo info = ArchiveMeta::inspect(path);
  QVERIFY2(info.ok, qPrintable(info.error));
  QCOMPARE(info.format, QStringLiteral("gzip"));
  QVERIFY(info.uncompressed > 0);
}

void ArchiveMetaTest::inspectTarGzMembers() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("pack.tgz"));
  QVERIFY(runPy(QStringLiteral(
      "import tarfile,io; t=tarfile.open(%1,'w:gz');"
      "b=io.BytesIO(b'xyz'); info=tarfile.TarInfo('xyz.txt');"
      "info.size=3; t.addfile(info,b); t.close()")
                    .arg(QLatin1Char('\'') + path + QLatin1Char('\''))));
  const ArchiveInfo info = ArchiveMeta::inspect(path);
  QVERIFY2(info.ok, qPrintable(info.error));
#ifndef SYNCHRO_HAVE_ZLIB
  QSKIP("zlib not linked");
#endif
  QCOMPARE(info.format, QStringLiteral("tar.gz"));
  QStringList names;
  for (const ArchiveEntry &e : info.entries)
    names.append(e.name);
  QVERIFY2(names.contains(QStringLiteral("xyz.txt")),
           qPrintable(info.sampleNote + QLatin1Char(' ') + names.join(',')));
}

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  ArchiveMetaTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "archive_meta_test.moc"
