#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "SearchModel.h"
#include "SearchService.h"
#include "ThumbnailService.h"

#include <QAbstractItemModel>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

namespace {

bool writeFile(const QString &path, const QByteArray &data = QByteArray("x")) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write(data) == data.size();
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
  void tabHopsSearchAndListing();
  void spaceSeparatedQueryIsFuzzy();
  void escCancels();
  void contentPrefixIsNotNameSearch();
  void contentArgvFixedString();
  void underRootRejectsSiblings();
  void contentSearchHitsBodyNotName();
  void contentNoMatchIsSuccess();
  void emptyContentPrefixDoesNotSearch();
  void shortContentQueryDoesNotStart();
  void shortNameQueryDoesNotStart();
  void cancelDoesNotBlockUi();
  void retypeClearsOldHits();
  void eraseSearchReturnsToFolder();
  void searchHitsGroupByFolder();
  void folderGroupsTrackSortedInserts();
  void searchThumbUpdateReachesRowMap();
  void contentSearchStaysInFolder();
  void virtualLocationDisabled();
  void revealLeavesSearch();
  void retypeCancelsRunningFd();
  void colonAfterSearchCancels();
  void searchThumbsUseHits();
  void fastBurstKeepsAllHits();
  void searchGridDownFollowsGroups();
};

void SearchServiceTest::argvFixedStringOneQuery() {
  const QStringList args = SearchService::arguments(
      QStringLiteral("foo.bar"), QStringLiteral("/tmp"), false);
  QVERIFY(args.contains(QStringLiteral("--color=never")));
  QVERIFY(args.contains(QStringLiteral("--exclude")));
  QVERIFY(args.contains(QStringLiteral(".git")));
  QVERIFY(!args.contains(QStringLiteral("-F")));
  QVERIFY(args.contains(QStringLiteral("-a")));
  QVERIFY(args.contains(QStringLiteral("--max-results")));
  QVERIFY(args.contains(QStringLiteral("5000")));
  QVERIFY(!args.contains(QStringLiteral("--hidden")));
  QCOMPARE(SearchService::fuzzyPattern(QStringLiteral("foo.bar")),
           QStringLiteral("foo\\.bar"));
  QCOMPARE(SearchService::fuzzyPattern(QStringLiteral("bax jpg")),
           QStringLiteral("bax.*jpg"));
  const int q = args.indexOf(QStringLiteral("foo\\.bar"));
  QVERIFY(q >= 0);
  QCOMPARE(args.at(q - 1), QStringLiteral("--"));
  QCOMPARE(args.at(q + 1), QStringLiteral("/tmp"));
  QCOMPARE(args.filter(QStringLiteral("foo\\.bar")).size(), 1);

  const QString dashPat = SearchService::fuzzyPattern(QStringLiteral("-x"));
  const QStringList dash = SearchService::arguments(
      QStringLiteral("-x"), QStringLiteral("/tmp"), false);
  const int dx = dash.indexOf(dashPat);
  QVERIFY(dx >= 0);
  QCOMPARE(dash.at(dx - 1), QStringLiteral("--"));
  QVERIFY(dash.indexOf(dashPat) == dash.lastIndexOf(dashPat));

  const QString execPat = SearchService::fuzzyPattern(QStringLiteral("--exec"));
  const QStringList exec = SearchService::arguments(
      QStringLiteral("--exec"), QStringLiteral("/tmp"), false);
  const int ex = exec.indexOf(execPat);
  QVERIFY(ex >= 0);
  QCOMPARE(exec.at(ex - 1), QStringLiteral("--"));
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
  QVERIFY(KeyMachine::isSearchText(QStringLiteral("??foo")));
  QVERIFY(KeyMachine::isContentSearchText(QStringLiteral("??foo")));
  QVERIFY(!KeyMachine::isContentSearchText(QStringLiteral("?foo")));
  QVERIFY(KeyMachine::isSearchText(QStringLiteral("?foo.bar")));
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("?synchro_keep_me")),
           QStringLiteral("synchro_keep_me"));

  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?synchro_keep_me"));
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  QVERIFY(dir.isSearch());
  QCOMPARE(dir.searchQuery(), QStringLiteral("synchro_keep_me"));
  QVERIFY(findName(dir, QStringLiteral("synchro_keep_me.txt")) >= 0);
  QVERIFY(findName(dir, QStringLiteral("other.txt")) < 0);

  nav.goUp();
  QVERIFY(waitListing(dir));
  QCOMPARE(canon(dir.path()), canon(root));
}

void SearchServiceTest::tabHopsSearchAndListing() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_tab_hit.txt"))));
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

  QVERIFY(keys.handleListKey(Qt::Key_Tab, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?"));

  keys.setFieldText(QStringLiteral("?synchro_tab_hit"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Tab, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  QVERIFY(findName(dir, QStringLiteral("synchro_tab_hit.txt")) >= 0);
  QVERIFY(findName(dir, QStringLiteral("other.txt")) < 0);

  QVERIFY(keys.handleListKey(Qt::Key_Tab, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?synchro_tab_hit"));
  QCOMPARE(dir.path(), QStringLiteral("search://"));

  QSignalSpy reset(&search, &QAbstractItemModel::modelAboutToBeReset);
  QVERIFY(keys.handleFieldKey(Qt::Key_Tab, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(reset.count(), 0);
  QVERIFY(findName(dir, QStringLiteral("synchro_tab_hit.txt")) >= 0);
}

void SearchServiceTest::spaceSeparatedQueryIsFuzzy() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("Baxter.jpg"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("other.txt"))));

  SearchModel model;
  model.start(QStringLiteral("bax jpg"), tmp.path(), false);
  QVERIFY(waitSearch(model));
  QVERIFY(findName(model, QStringLiteral("Baxter.jpg")) >= 0);
  QVERIFY(findName(model, QStringLiteral("other.txt")) < 0);
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
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes.txt")),
                    "synchro_rg_needle in the body"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_rg_needle.txt")),
                    "name only"));

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
  keys.setFieldText(QStringLiteral("??synchro_rg_needle"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QVERIFY(proxy.filter().isEmpty());
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("??synchro_rg_needle")),
           QStringLiteral("synchro_rg_needle"));
  keys.acceptField();
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  QVERIFY(dir.isContentSearch());
  QVERIFY(findName(dir, QStringLiteral("notes.txt")) >= 0);
  QVERIFY(findName(dir, QStringLiteral("synchro_rg_needle.txt")) < 0);
}

void SearchServiceTest::contentArgvFixedString() {
  const QStringList args = SearchService::arguments(
      QStringLiteral("foo.bar"), QStringLiteral("/tmp"), false,
      SearchService::Kind::Content);
  QVERIFY(args.contains(QStringLiteral("--color=never")));
  QVERIFY(args.contains(QStringLiteral("--files-with-matches")));
  QVERIFY(args.contains(QStringLiteral("-F")));
  QVERIFY(args.contains(QStringLiteral("--glob")));
  QVERIFY(args.contains(QStringLiteral("!.git/**")));
  QVERIFY(!args.contains(QStringLiteral("--hidden")));
  const int q = args.indexOf(QStringLiteral("foo.bar"));
  QVERIFY(q >= 0);
  QCOMPARE(args.at(q - 1), QStringLiteral("--"));
  QCOMPARE(args.at(q + 1), QStringLiteral("/tmp"));

  const QStringList dash = SearchService::arguments(
      QStringLiteral("-x"), QStringLiteral("/tmp"), false,
      SearchService::Kind::Content);
  const int dx = dash.indexOf(QStringLiteral("-x"));
  QVERIFY(dx >= 0);
  QCOMPARE(dash.at(dx - 1), QStringLiteral("--"));

  const QStringList hid = SearchService::arguments(
      QStringLiteral("x"), QStringLiteral("/tmp"), true,
      SearchService::Kind::Content);
  QVERIFY(hid.contains(QStringLiteral("--hidden")));
}

void SearchServiceTest::underRootRejectsSiblings() {
  QVERIFY(SearchService::isUnderRoot(QStringLiteral("/tmp/here"),
                                     QStringLiteral("/tmp/here/a.txt")));
  QVERIFY(SearchService::isUnderRoot(QStringLiteral("/tmp/here"),
                                     QStringLiteral("/tmp/here")));
  QVERIFY(!SearchService::isUnderRoot(QStringLiteral("/tmp/here"),
                                      QStringLiteral("/tmp/outside.txt")));
  QVERIFY(!SearchService::isUnderRoot(QStringLiteral("/tmp/here"),
                                      QStringLiteral("/tmp/hereafter/x")));
  QVERIFY(SearchService::resolveRoot(QStringLiteral("search://")).isEmpty());
}

void SearchServiceTest::contentSearchHitsBodyNotName() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("hit.txt")),
                    "alpha foo.bar omega"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("regex.txt")), "fooXbar"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("foo.bar")), "name only"));

  SearchModel model;
  model.start(QStringLiteral("foo.bar"), tmp.path(), false, true);
  QVERIFY(waitSearch(model));
  QVERIFY(model.contentSearch());
  QVERIFY(findName(model, QStringLiteral("hit.txt")) >= 0);
  QVERIFY(findName(model, QStringLiteral("regex.txt")) < 0);
  QVERIFY(findName(model, QStringLiteral("foo.bar")) < 0);
}

void SearchServiceTest::contentNoMatchIsSuccess() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("empty.txt")), "nothing"));

  SearchModel model;
  model.start(QStringLiteral("synchro_rg_absent_token"), tmp.path(), false,
              true);
  QVERIFY(waitSearch(model));
  QVERIFY(model.errorString().isEmpty());
  QCOMPARE(model.count(), 0);
}

void SearchServiceTest::emptyContentPrefixDoesNotSearch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.txt")), "x"));

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
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("??")), QString());
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  keys.acceptField();
  QVERIFY(!search.running());
  QVERIFY(!DirectoryModel::isSearchPath(dir.path()));
  QCOMPARE(search.count(), 0);
}

void SearchServiceTest::shortNameQueryDoesNotStart() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("ab.txt"))));

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
  keys.setFieldText(QStringLiteral("?ab"));
  keys.acceptField();
  QVERIFY(!search.running());
  QVERIFY(!search.listing());
  QCOMPARE(search.count(), 0);
}

void SearchServiceTest::shortContentQueryDoesNotStart() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes.txt")), "ab in body"));

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
  keys.setFieldText(QStringLiteral("??ab"));
  keys.acceptField();
  QVERIFY(!search.running());
  QVERIFY(!search.listing());
  QCOMPARE(search.count(), 0);
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("??ab")).size(), 2);
  QVERIFY(KeyMachine::searchQuery(QStringLiteral("??ab")).size() <
          KeyMachine::kMinContentQueryChars);
}

void SearchServiceTest::cancelDoesNotBlockUi() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 40; ++i) {
    QVERIFY(writeFile(tmp.filePath(QStringLiteral("f%1.txt").arg(i)),
                      "synchro_cancel_token"));
  }

  SearchModel model;
  model.start(QStringLiteral("synchro_cancel_token"), tmp.path(), false, true);
  QVERIFY(QTest::qWaitFor(
      [&] { return model.running() || model.count() > 0 || !model.listing(); },
      2000));
  QElapsedTimer t;
  t.start();
  model.cancel();
  QVERIFY2(t.elapsed() < 40, qPrintable(QString::number(t.elapsed())));
  QVERIFY(!model.running());
}

void SearchServiceTest::retypeClearsOldHits() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("alpha.txt")),
                    "synchro_old_token"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("beta.txt")),
                    "synchro_new_token"));

  SearchModel model;
  model.start(QStringLiteral("synchro_old_token"), tmp.path(), false, true);
  QVERIFY(waitSearch(model));
  QCOMPARE(model.count(), 1);
  QCOMPARE(model.data(model.index(0, 0), SearchModel::NameRole).toString(),
           QStringLiteral("alpha.txt"));

  model.start(QStringLiteral("synchro_new_token"), tmp.path(), false, true);
  QCOMPARE(model.count(), 0);
  QVERIFY(waitSearch(model));
  QCOMPARE(model.count(), 1);
  QCOMPARE(model.data(model.index(0, 0), SearchModel::NameRole).toString(),
           QStringLiteral("beta.txt"));
}

void SearchServiceTest::eraseSearchReturnsToFolder() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes.txt")),
                    "synchro_leave_token"));

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
  const QString here = dir.path();
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??synchro_leave_token"));
  keys.acceptField();
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??"));
  keys.acceptField();
  QVERIFY(waitListing(dir));
  QCOMPARE(canon(dir.path()), canon(here));
  QCOMPARE(keys.fieldText(), QStringLiteral("??"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
}

void SearchServiceTest::searchHitsGroupByFolder() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("b")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b/zeta.txt")), "grp_token"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a/alpha.txt")), "grp_token"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a/beta.txt")), "grp_token"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("root.txt")), "grp_token"));

  SearchModel model;
  model.start(QStringLiteral("grp_token"), tmp.path(), false, true);
  QVERIFY(waitSearch(model));
  QCOMPARE(model.count(), 4);
  QCOMPARE(model.data(model.index(0, 0), SearchModel::ParentLabelRole)
               .toString(),
           QStringLiteral("this folder"));
  QCOMPARE(model.data(model.index(0, 0), SearchModel::NameRole).toString(),
           QStringLiteral("root.txt"));
  QCOMPARE(model.data(model.index(1, 0), SearchModel::ParentLabelRole)
               .toString(),
           QStringLiteral("a"));
  QCOMPARE(model.data(model.index(1, 0), SearchModel::NameRole).toString(),
           QStringLiteral("alpha.txt"));
  QCOMPARE(model.data(model.index(2, 0), SearchModel::NameRole).toString(),
           QStringLiteral("beta.txt"));
  QCOMPARE(model.data(model.index(3, 0), SearchModel::ParentLabelRole)
               .toString(),
           QStringLiteral("b"));
  QCOMPARE(model.folderGroups().size(), 3);
  QCOMPARE(model.folderGroups().at(0).toMap().value(QStringLiteral("label")),
           QStringLiteral("this folder"));
  QCOMPARE(model.folderGroups().at(0).toMap().value(QStringLiteral("count")),
           1);
  QCOMPARE(model.folderGroups().at(1).toMap().value(QStringLiteral("label")),
           QStringLiteral("a"));
  QCOMPARE(model.folderGroups().at(1).toMap().value(QStringLiteral("count")),
           2);
  QCOMPARE(model.folderGroups().at(2).toMap().value(QStringLiteral("label")),
           QStringLiteral("b"));
}

static void assertGroupsCoverRows(const SearchModel &model) {
  const QVariantList groups = model.folderGroups();
  int covered = 0;
  for (int g = 0; g < groups.size(); ++g) {
    const QVariantMap m = groups.at(g).toMap();
    const int first = m.value(QStringLiteral("first")).toInt();
    const int count = m.value(QStringLiteral("count")).toInt();
    const QString path = m.value(QStringLiteral("path")).toString();
    QCOMPARE(first, covered);
    QVERIFY(count > 0);
    const QAbstractItemModel *gm = model.folderGroupModel();
    QCOMPARE(gm->data(gm->index(g, 0), SearchFolderModel::FirstRole).toInt(),
             first);
    QCOMPARE(gm->data(gm->index(g, 0), SearchFolderModel::CountRole).toInt(),
             count);
    for (int i = 0; i < count; ++i) {
      const QVariantMap rec = model.rowMap(first + i);
      QCOMPARE(rec.value(QStringLiteral("parentPath")).toString(), path);
      QCOMPARE(rec.value(QStringLiteral("index")).toInt(), first + i);
      QVERIFY(!rec.value(QStringLiteral("path")).toString().isEmpty());
      QVERIFY(!rec.value(QStringLiteral("name")).toString().isEmpty());
    }
    covered += count;
  }
  QCOMPARE(covered, model.count());
}

void SearchServiceTest::folderGroupsTrackSortedInserts() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("z")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("m")));
  const QString zb = tmp.filePath(QStringLiteral("z/b.txt"));
  const QString aa = tmp.filePath(QStringLiteral("a/a.txt"));
  const QString za = tmp.filePath(QStringLiteral("z/a.txt"));
  const QString mm = tmp.filePath(QStringLiteral("m/m.txt"));
  const QString root = tmp.filePath(QStringLiteral("root.txt"));
  QVERIFY(writeFile(zb));
  QVERIFY(writeFile(aa));
  QVERIFY(writeFile(za));
  QVERIFY(writeFile(mm));
  QVERIFY(writeFile(root));

  SearchModel model;
  QAbstractItemModel *groups = model.folderGroupModel();
  QVERIFY(groups);
  QSignalSpy resetSpy(groups, &QAbstractItemModel::modelReset);
  QSignalSpy insertSpy(groups, &QAbstractItemModel::rowsInserted);

  // Arrive out of folder order so inserts land in the middle of the model.
  model.onHit(zb);
  model.onHit(aa);
  model.onHit(za);
  model.onHit(mm);
  model.onHit(root);

  QCOMPARE(model.count(), 5);
  QCOMPARE(resetSpy.count(), 0);
  QCOMPARE(insertSpy.count(), 4);
  assertGroupsCoverRows(model);
  QCOMPARE(model.folderGroups().size(), 4);
  QCOMPARE(model.data(model.index(0, 0), SearchModel::NameRole).toString(),
           QStringLiteral("root.txt"));
  QCOMPARE(model.data(model.index(1, 0), SearchModel::NameRole).toString(),
           QStringLiteral("a.txt"));
  QCOMPARE(model.data(model.index(2, 0), SearchModel::NameRole).toString(),
           QStringLiteral("m.txt"));
  QCOMPARE(model.data(model.index(3, 0), SearchModel::NameRole).toString(),
           QStringLiteral("a.txt"));
  QCOMPARE(model.data(model.index(4, 0), SearchModel::NameRole).toString(),
           QStringLiteral("b.txt"));

  DirectoryModel dir;
  dir.setSearchModel(&model);
  dir.setPath(QStringLiteral("search://"));
  QVERIFY(dir.folderGroupModel());
  QCOMPARE(dir.folderGroupModel()->rowCount(), 4);
  QCOMPARE(dir.rowMap(3).value(QStringLiteral("path")).toString(),
           QFileInfo(za).absoluteFilePath());
}

void SearchServiceTest::searchThumbUpdateReachesRowMap() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("pic.txt"));
  QVERIFY(writeFile(path));

  SearchModel model;
  model.onHit(path);
  QCOMPARE(model.count(), 1);
  QVERIFY(model.rowMap(0).value(QStringLiteral("thumbnail")).toString().isEmpty());

  const QString url = QStringLiteral("image://synchrothumb/pic");
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  model.setThumbnail(QFileInfo(path).absoluteFilePath(), url);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(model.rowMap(0).value(QStringLiteral("thumbnail")).toString(), url);
  QCOMPARE(model.data(model.index(0, 0), SearchModel::ThumbnailRole).toString(),
           url);

  DirectoryModel dir;
  dir.setSearchModel(&model);
  dir.setPath(QStringLiteral("search://"));
  QSignalSpy dirChanged(&dir, &QAbstractItemModel::dataChanged);
  const QString url2 = QStringLiteral("image://synchrothumb/pic2");
  model.setThumbnail(QFileInfo(path).absoluteFilePath(), url2);
  QVERIFY(dirChanged.count() >= 1);
  QCOMPARE(dir.rowMap(0).value(QStringLiteral("thumbnail")).toString(), url2);
}

void SearchServiceTest::contentSearchStaysInFolder() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("here")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("here/inside.txt")),
                    "synchro_scope_token"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("outside.txt")),
                    "synchro_scope_token"));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(tmp.filePath(QStringLiteral("here")));
  QVERIFY(waitListing(dir));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??synchro_scope_token"));
  keys.acceptField();
  QVERIFY(waitSearch(search));
  QCOMPARE(dir.path(), QStringLiteral("search://"));
  QCOMPARE(canon(search.root()),
           canon(tmp.filePath(QStringLiteral("here"))));
  QVERIFY(findName(dir, QStringLiteral("inside.txt")) >= 0);
  QVERIFY(findName(dir, QStringLiteral("outside.txt")) < 0);
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

void SearchServiceTest::retypeCancelsRunningFd() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(QStringLiteral("/usr"));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?lib"));
  keys.acceptField();
  QVERIFY(QTest::qWaitFor([&] { return search.running(); }, 1000));
  QSignalSpy hits(&search.service(), &SearchService::hit);
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?zzzz_synchro_unlikely"));
  QVERIFY(!search.running());
  const int after = hits.count();
  QTest::qWait(80);
  QCOMPARE(hits.count(), after);
}

void SearchServiceTest::colonAfterSearchCancels() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  keys.setSearchModel(&search);

  dir.setPath(QStringLiteral("/usr"));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?lib"));
  keys.acceptField();
  QVERIFY(QTest::qWaitFor([&] { return search.running(); }, 1000));
  keys.focusCommand();
  QVERIFY(!search.running());
}

void SearchServiceTest::searchThumbsUseHits() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("synchro_thumb_hit.txt"))));

  DirectoryModel dir;
  SearchModel search;
  dir.setSearchModel(&search);
  dir.setPath(tmp.path());
  QVERIFY(waitListing(dir));

  search.start(QStringLiteral("synchro_thumb_hit"), tmp.path(), false);
  QVERIFY(waitSearch(search));
  dir.setPath(QStringLiteral("search://"));
  QCOMPARE(dir.rowCount(), 1);

  auto *thumbs = dir.findChild<ThumbnailService *>();
  QVERIFY(thumbs);
  QSignalSpy submitted(thumbs, &ThumbnailService::submitted);
  dir.requestVisibleThumbs(0, 0, 128);
  QVERIFY(submitted.count() >= 1);
  const auto jobs = submitted.at(0).at(0).value<QVector<ThumbnailJob>>();
  QCOMPARE(jobs.size(), 1);
  QVERIFY(jobs.at(0).path.endsWith(QStringLiteral("synchro_thumb_hit.txt")));
}

void SearchServiceTest::fastBurstKeepsAllHits() {
  if (SearchService::executable().isEmpty())
    QSKIP("fd is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 40; ++i) {
    QVERIFY(writeFile(
        tmp.filePath(QStringLiteral("synchroburst%1.txt").arg(i, 2, 10, QChar('0')))));
  }

  SearchModel model;
  model.start(QStringLiteral("synchroburst"), tmp.path(), false);
  QVERIFY(waitSearch(model, 5000));
  QCOMPARE(model.count(), 40);
}

void SearchServiceTest::searchGridDownFollowsGroups() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QStringList hits;
  for (int i = 0; i < 8; ++i) {
    const QString dir =
        tmp.filePath(QStringLiteral("g%1").arg(i, 2, 10, QChar('0')));
    QVERIFY(QDir().mkpath(dir));
    const QString path = dir + QStringLiteral("/hit.txt");
    QVERIFY(writeFile(path));
    hits.append(path);
  }

  SearchModel search;
  for (const QString &path : hits)
    search.onHit(path);
  QCOMPARE(search.count(), 8);
  auto *groups = search.folderGroupModel();
  QVERIFY(groups);
  QCOMPARE(groups->rowCount(), 8);
  // Six columns, one file per folder: visual down is the next group, not +6.
  QCOMPARE(groups->stepVisual(0, 0, 1, 6), 1);
  QCOMPARE(groups->stepVisual(1, 0, 1, 6), 2);
  QCOMPARE(groups->stepVisual(2, 0, -1, 6), 1);
  QCOMPARE(groups->stepVisual(0, 0, 3, 6), 3);

  DirectoryModel dir;
  dir.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&dir);
  NavStack nav(&dir);
  KeyMachine keys(&dir, &proxy, &nav);
  dir.setPath(QStringLiteral("search://"));
  keys.setGridMode(true);
  keys.setGridStride(6);
  proxy.setCurrentIndex(0);
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(proxy.currentIndex(), 1);
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(proxy.currentIndex(), 2);
  QVERIFY(keys.handleListKey(Qt::Key_W, Qt::NoModifier, QStringLiteral("w")));
  QCOMPARE(proxy.currentIndex(), 1);

  const QString wide = tmp.filePath(QStringLiteral("wide"));
  QVERIFY(QDir().mkpath(wide));
  QStringList wideHits;
  for (int i = 0; i < 5; ++i) {
    const QString path =
        wide + QStringLiteral("/w%1.txt").arg(i, 2, 10, QChar('0'));
    QVERIFY(writeFile(path));
    wideHits.append(path);
  }
  SearchModel wideModel;
  for (const QString &path : wideHits)
    wideModel.onHit(path);
  auto *wg = wideModel.folderGroupModel();
  QCOMPARE(wg->rowCount(), 1);
  QCOMPARE(wg->stepVisual(0, 0, 1, 4), 4);
  QCOMPARE(wg->stepVisual(1, 0, 1, 4), 1);
  QCOMPARE(wg->stepVisual(4, 0, -1, 4), 0);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  SearchServiceTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "search_service_test.moc"
