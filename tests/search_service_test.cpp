#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "SearchModel.h"
#include "SearchService.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {

bool writeFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write("x", 1) == 1;
}

bool waitSearch(SearchModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

bool waitListing(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

int findName(const QAbstractItemModel &model, const QString &name) {
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

} // namespace

class SearchServiceTest : public QObject {
  Q_OBJECT

private slots:
  void argvFixedStringOneQuery();
  void argvHiddenOnlyWhenOn();
  void firstHitUnder200ms();
  void fixedStringIsNotRegex();
  void queryWithSpaceIsOneArgv();
  void hiddenToggle();
  void cancelStopsProcess();
  void fieldSearchModeAndEnterKeepsResults();
  void escCancels();
  void contentPrefixIsNotNameSearch();
  void virtualLocationDisabled();
  void revealLeavesSearch();
};

void SearchServiceTest::argvFixedStringOneQuery() {
  const QStringList args = SearchService::arguments(
      QStringLiteral("foo.bar"), QStringLiteral("/tmp"), false);
  QVERIFY(args.contains(QStringLiteral("--color=never")));
  QVERIFY(args.contains(QStringLiteral("--exclude")));
  QVERIFY(args.contains(QStringLiteral(".git")));
  QVERIFY(args.contains(QStringLiteral("-F")));
  QVERIFY(args.contains(QStringLiteral("-a")));
  QVERIFY(args.contains(QStringLiteral("--max-results")));
  QVERIFY(args.contains(QStringLiteral("5000")));
  QVERIFY(!args.contains(QStringLiteral("--hidden")));
  const int q = args.indexOf(QStringLiteral("foo.bar"));
  QVERIFY(q >= 0);
  QCOMPARE(args.at(q + 1), QStringLiteral("/tmp"));
  QCOMPARE(args.filter(QStringLiteral("foo.bar")).size(), 1);
}

void SearchServiceTest::argvHiddenOnlyWhenOn() {
  const QStringList off = SearchService::arguments(
      QStringLiteral("x"), QStringLiteral("/tmp"), false);
  const QStringList on = SearchService::arguments(QStringLiteral("x"),
                                                  QStringLiteral("/tmp"), true);
  QVERIFY(!off.contains(QStringLiteral("--hidden")));
  QVERIFY(on.contains(QStringLiteral("--hidden")));
}

void SearchServiceTest::firstHitUnder200ms() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString name = QStringLiteral("synchro_fd_first_hit.txt");
  QVERIFY(writeFile(tmp.filePath(name)));

  SearchService svc;
  QSignalSpy first(&svc, &SearchService::firstHit);
  QSignalSpy hit(&svc, &SearchService::hit);
  svc.start(name, tmp.path(), false);
  QVERIFY(QTest::qWaitFor([&] { return first.count() > 0; }, 2000));
  QVERIFY2(svc.firstLineMs() >= 0 && svc.firstLineMs() < 200,
           qPrintable(QStringLiteral("first line %1ms").arg(svc.firstLineMs())));
  QVERIFY(hit.count() >= 1);
  QVERIFY(hit.at(0).at(0).toString().endsWith(name));
  QVERIFY(QTest::qWaitFor([&] { return !svc.running(); }, 2000));
}

void SearchServiceTest::fixedStringIsNotRegex() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("foo.bar"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("fooXbar"))));

  SearchModel model;
  model.start(QStringLiteral("foo.bar"), tmp.path(), false);
  QVERIFY(waitSearch(model));
  QVERIFY(findName(model, QStringLiteral("foo.bar")) >= 0);
  QVERIFY(findName(model, QStringLiteral("fooXbar")) < 0);
}

void SearchServiceTest::queryWithSpaceIsOneArgv() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("hello world.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("hello"))));

  SearchModel model;
  model.start(QStringLiteral("hello world"), tmp.path(), false);
  QVERIFY(waitSearch(model));
  QVERIFY(findName(model, QStringLiteral("hello world.txt")) >= 0);
  QVERIFY(findName(model, QStringLiteral("hello")) < 0);
}

void SearchServiceTest::hiddenToggle() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral(".synchro_hidden_hit"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_visible_hit"))));

  SearchModel model;
  model.start(QStringLiteral("synchro_"), tmp.path(), false);
  QVERIFY(waitSearch(model));
  QVERIFY(findName(model, QStringLiteral("synchro_visible_hit")) >= 0);
  QVERIFY(findName(model, QStringLiteral(".synchro_hidden_hit")) < 0);

  model.setShowHidden(true);
  QVERIFY(waitSearch(model));
  QVERIFY(findName(model, QStringLiteral(".synchro_hidden_hit")) >= 0);
}

void SearchServiceTest::cancelStopsProcess() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  SearchService svc;
  QSignalSpy hits(&svc, &SearchService::hit);
  svc.start(QStringLiteral("a"), QStringLiteral("/usr"), false);
  QVERIFY(QTest::qWaitFor([&] { return svc.running() || hits.count() > 0; },
                          1000));
  svc.cancel();
  QVERIFY(!svc.running());
  const int after = hits.count();
  QTest::qWait(80);
  QCOMPARE(hits.count(), after);
}

void SearchServiceTest::fieldSearchModeAndEnterKeepsResults() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_keep_me.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("other.txt"))));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(tmp.path());
  QVERIFY(waitListing(dir));
  const QString root = dir.path();

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?synchro_keep_me"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(!KeyMachine::isSearchText(QStringLiteral("??foo")));
  QVERIFY(KeyMachine::isSearchText(QStringLiteral("?foo.bar")));
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("?synchro_keep_me")),
           QStringLiteral("synchro_keep_me"));

  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?synchro_keep_me"));
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  QVERIFY(findName(dir, QStringLiteral("synchro_keep_me.txt")) >= 0);
  QVERIFY(findName(dir, QStringLiteral("other.txt")) < 0);

  nav.goUp();
  QVERIFY(waitListing(dir));
  QCOMPARE(canon(dir.path()), canon(root));
}

void SearchServiceTest::escCancels() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_esc_hit.txt"))));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(tmp.path());
  QVERIFY(waitListing(dir));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?synchro_esc_hit"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  keys.acceptField();
  QVERIFY(QTest::qWaitFor([&] { return search.running() || search.count() > 0; },
                          2000));
  keys.focusFilter();
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(!search.running());
  QVERIFY(keys.fieldText().isEmpty());
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
}

void SearchServiceTest::contentPrefixIsNotNameSearch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(tmp.path());
  QVERIFY(waitListing(dir));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??README"));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QVERIFY(!DirectoryModel::isSearchPath(dir.path()));
  QVERIFY(!search.running());
  QCOMPARE(proxy.filter(), QStringLiteral("??README"));
}

void SearchServiceTest::virtualLocationDisabled() {
  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(QStringLiteral("trash://"));
  QCOMPARE(dir.path(), QStringLiteral("trash://"));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?foo"));
  keys.acceptField();
  QCOMPARE(dir.path(), QStringLiteral("trash://"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("search is not available"));
  QVERIFY(!search.running());
}

void SearchServiceTest::revealLeavesSearch() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("sub/synchro_reveal.txt"))));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(tmp.path());
  QVERIFY(waitListing(dir));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?synchro_reveal"));
  keys.acceptField();
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  const int row = findName(dir, QStringLiteral("synchro_reveal.txt"));
  QVERIFY(row >= 0);
  dir.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_G, Qt::NoModifier, QStringLiteral("g")));
  QVERIFY(waitListing(dir));
  QCOMPARE(canon(dir.path()), canon(tmp.filePath(QStringLiteral("sub"))));
  QCOMPARE(dir.currentName(), QStringLiteral("synchro_reveal.txt"));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  SearchServiceTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "search_service_test.moc"
