#include "CommandPalette.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "PeekHost.h"
#include "RecentStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QPoint>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)

namespace {

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

QString nameAt(const FilterProxy &proxy, int row) {
  return proxy.data(proxy.index(row, 0), DirectoryModel::NameRole).toString();
}

int findProxy(const FilterProxy &proxy, const QString &name) {
  for (int i = 0; i < proxy.rowCount(); ++i) {
    if (nameAt(proxy, i) == name)
      return i;
  }
  return -1;
}

bool allNamesContain(const FilterProxy &proxy, const QString &needle) {
  if (proxy.rowCount() == 0)
    return false;
  for (int i = 0; i < proxy.rowCount(); ++i) {
    if (!nameAt(proxy, i).contains(needle, Qt::CaseInsensitive))
      return false;
  }
  return true;
}

QString canon(const QString &path) {
  const QString c = QFileInfo(path).canonicalFilePath();
  return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

bool writeFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.write("x", 1);
  return true;
}

} // namespace

class StubPeek : public PeekHost {
public:
  bool isOpen() const override { return m_open; }
  bool toggle() override {
    m_open = !m_open;
    emit openChanged();
    return m_open;
  }
  bool openCurrent() override {
    m_open = true;
    emit openChanged();
    return true;
  }
  void close() override {
    if (!m_open)
      return;
    m_open = false;
    emit openChanged();
  }
  void step(int delta) override { lastStep = delta; }
  bool actionOpen() const override { return m_action; }
  void closeAction() override {
    if (!m_action)
      return;
    m_action = false;
    emit actionOpenChanged();
  }
  void openAction() {
    m_action = true;
    emit actionOpenChanged();
  }
  int lastStep = 0;

private:
  bool m_open = false;
  bool m_action = false;
};

class CommandFieldTest : public QObject {
  Q_OBJECT

private slots:
  void launchIsListFocused();
  void bareSrcFiltersDoesNotNavigate();
  void slashAndCtrlKEnterFieldFilter();
  void ctrlLEntersFieldJumpWithPathSelected();
  void enterFilterKeepsFilterReturnsToList();
  void enterOnSrcSlashNavigates();
  void pathSigilRules();
  void missingPrefixWithSlashStillFilters();
  void escSingleStepPop();
  void typeToSeekHighlightsFirstMatch();
  void verbsDoNotTypeToSeek();
  void fieldFilterTypesVerbsAsText();
  void filterIsCaseInsensitiveSubstring();
  void failedJumpStaysInField();
  void goBackRestoresFilter();
  void emptyFilterKeepsSourceCursor();
  void enterOnFileActivatesDoesNotNavigate();
  void spaceTogglesPeekAndJkStep();
  void mainQmlSlashThenSrcFilters();
  void tRequestsTerminal();
  void ctrlReturnRequestsOpenWith();
  void focusFilterClosesActionOverlay();
  void paletteResolvePrefersBuiltins();
  void colonFromListEntersFieldCommand();
  void leadingColonPromotesAndDoesNotFilter();
  void enterHomeNavigates();
  void enterHiddenToggles();
  void enterGridListTogglesView();
  void enterFsnAndEscReturns();
  void mainQmlFsnMounts();
  void enterTrashNoopsWithStatus();
  void enterTrashOpensTrashUrl();
  void enterEmptyNoopsWithStatus();
  void enterRecentEmptyStatus();
  void enterRecentJumpsToLast();
  void enterHelpOpensOverlay();
  void tabTogglesSearchFromList();
  void enterSortChangesRoleAndFlips();
  void naturalSortOrdersDirsFirst();
  void typeLabelsForColumns();
  void kindFilterFilesFoldersAll();
  void termPanelCommands();
  void escCommandSingleStep();
  void colonDoesNotReplaceListVerbs();
  void unknownAndAmbiguousStayInField();
  void actionHandlerByIdAndTitle();
  void vTogglesGridFromList();
  void successfulCommandClearsStatus();
  void mainQmlColonEntersCommand();
  void mainQmlGridClickAfterColonPops();
  void leadingQuestionPromotesToFieldSearch();
  void questionQuestionIsContentSearch();
};

void CommandFieldTest::launchIsListFocused() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.listFocused());
  QVERIFY(!keys.fieldFocused());
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(proxy.filter().isEmpty());
}

void CommandFieldTest::bareSrcFiltersDoesNotNavigate() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src_other")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("src_notes.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("LICENSE"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();
  QVERIFY(model.rowCount() >= 5);
  QCOMPARE(findProxy(proxy, QStringLiteral("src")) >= 0, true);
  QCOMPARE(findProxy(proxy, QStringLiteral("README.md")) >= 0, true);

  keys.focusFilter();
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  keys.setFieldText(QStringLiteral("src"));

  QCOMPARE(canon(model.path()), canon(root));
  QVERIFY2(!model.path().endsWith(QStringLiteral("/src")) ||
               canon(model.path()) == canon(root),
           "bare src must not navigate into src/");
  QCOMPARE(keys.fieldText(), QStringLiteral("src"));
  QCOMPARE(proxy.filter(), QStringLiteral("src"));
  QVERIFY(proxy.rowCount() >= 1);
  QVERIFY(proxy.rowCount() < model.rowCount());
  QVERIFY(allNamesContain(proxy, QStringLiteral("src")));
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("src_other")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("src_notes.txt")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) < 0);
  QVERIFY(findProxy(proxy, QStringLiteral("LICENSE")) < 0);
  QVERIFY(!KeyMachine::isJumpText(QStringLiteral("src"), root));
}

void CommandFieldTest::slashAndCtrlKEnterFieldFilter() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(
      keys.handleListKey(Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/")));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QVERIFY(keys.fieldText().isEmpty());

  keys.focusList();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.handleListKey(Qt::Key_K, Qt::ControlModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
}

void CommandFieldTest::ctrlLEntersFieldJumpWithPathSelected() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int before = keys.jumpEpoch();
  QVERIFY(keys.handleListKey(Qt::Key_L, Qt::ControlModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("field-jump"));
  QCOMPARE(keys.fieldText(), model.path());
  QVERIFY(KeyMachine::isJumpText(keys.fieldText(), model.path()));
  QVERIFY(keys.jumpEpoch() > before);
  QVERIFY(proxy.filter().isEmpty());
}

void CommandFieldTest::enterFilterKeepsFilterReturnsToList() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("src"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Return, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(keys.fieldText(), QStringLiteral("src"));
  QCOMPARE(proxy.filter(), QStringLiteral("src"));
  QCOMPARE(canon(model.path()), canon(root));
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) < 0);
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
}

void CommandFieldTest::enterOnSrcSlashNavigates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("src/inside.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("src/"));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("src/"), root));
  keys.acceptField();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(tmp.filePath(QStringLiteral("src"))));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(findProxy(proxy, QStringLiteral("inside.txt")) >= 0);
}

void CommandFieldTest::pathSigilRules() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("src/foo")));

  const QString cwd = tmp.path();
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("/usr"), cwd));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("/"), cwd));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("~/"), cwd));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("~/Downloads"), cwd));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("src/"), cwd));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("src/foo"), cwd));
  QVERIFY(!KeyMachine::isJumpText(QStringLiteral("src"), cwd));
  QVERIFY(!KeyMachine::isJumpText(QStringLiteral("README"), cwd));
  QVERIFY(!KeyMachine::isJumpText(QStringLiteral("~"), cwd));
  QVERIFY(!KeyMachine::isJumpText(QString(), cwd));

  const QString home = KeyMachine::resolveJump(QStringLiteral("~/"), cwd);
  QCOMPARE(canon(home), canon(QDir::homePath()));
  QCOMPARE(canon(KeyMachine::resolveJump(QStringLiteral("src/"), cwd)),
           canon(tmp.filePath(QStringLiteral("src"))));
}

void CommandFieldTest::missingPrefixWithSlashStillFilters() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();

  QVERIFY(!KeyMachine::isJumpText(QStringLiteral("nope/foo"), root));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("nope/foo"));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(proxy.filter(), QStringLiteral("nope/foo"));
  QCOMPARE(proxy.rowCount(), 0);
  keys.acceptField();
  QCOMPARE(canon(model.path()), canon(root));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::escSingleStepPop() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("src"));
  QVERIFY(proxy.rowCount() < model.rowCount());
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(keys.fieldText().isEmpty());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) >= 0);

  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("src"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(!proxy.filter().isEmpty());
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(proxy.filter().isEmpty());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::typeToSeekHighlightsFirstMatch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("mxy_only"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zzz.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(findProxy(proxy, QStringLiteral("mxy_only")) >= 0);

  QVERIFY(keys.handleListKey(Qt::Key_M, Qt::NoModifier, QStringLiteral("m")));
  QCOMPARE(proxy.currentName(), QStringLiteral("mxy_only"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::verbsDoNotTypeToSeek() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("bbb.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("jjj.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zzz.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(proxy.rowCount() >= 4);
  proxy.setCurrentIndex(0);
  const int before = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(proxy.currentIndex(), qMin(before + 1, proxy.rowCount() - 1));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::fieldFilterTypesVerbsAsText() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("jacket"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("j"));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(proxy.filter(), QStringLiteral("j"));
  QVERIFY(findProxy(proxy, QStringLiteral("jacket")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("keep")) < 0);
  keys.setFieldText(QStringLiteral("."));
  QCOMPARE(proxy.filter(), QStringLiteral("."));
}

void CommandFieldTest::filterIsCaseInsensitiveSubstring() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("SrcNotes.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("SRC"));
  QVERIFY(allNamesContain(proxy, QStringLiteral("src")));
  QVERIFY(findProxy(proxy, QStringLiteral("SrcNotes.txt")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) < 0);
}

void CommandFieldTest::failedJumpStaysInField() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("/definitely-missing"));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("/definitely-missing"), root));
  QVERIFY(KeyMachine::resolveJump(QStringLiteral("/definitely-missing"), root)
              .isEmpty());
  keys.acceptField();
  QCOMPARE(canon(model.path()), canon(root));
  QVERIFY(keys.fieldFocused());
  QCOMPARE(keys.fieldText(), QStringLiteral("/definitely-missing"));
  QVERIFY(proxy.filter().isEmpty());

  keys.setFieldText(QStringLiteral("src/missing"));
  QVERIFY(KeyMachine::isJumpText(QStringLiteral("src/missing"), root));
  keys.acceptField();
  QCOMPARE(canon(model.path()), canon(root));
  QVERIFY(keys.fieldFocused());
}

void CommandFieldTest::goBackRestoresFilter() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("src/inside.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("src"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(proxy.filter(), QStringLiteral("src"));
  const int row = findProxy(proxy, QStringLiteral("src"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  proxy.activateCurrent();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(tmp.filePath(QStringLiteral("src"))));
  QVERIFY(proxy.filter().isEmpty());

  QVERIFY(nav.canGoBack());
  nav.goBack();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(root));
  QCOMPARE(proxy.filter(), QStringLiteral("src"));
  QCOMPARE(keys.fieldText(), QStringLiteral("src"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) < 0);
}

void CommandFieldTest::emptyFilterKeepsSourceCursor() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("bbb.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zzz.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  Q_UNUSED(keys);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int z = findProxy(proxy, QStringLiteral("zzz.txt"));
  QVERIFY(z >= 0);
  proxy.setCurrentIndex(z);
  QCOMPARE(model.currentName(), QStringLiteral("zzz.txt"));

  proxy.setFilter(QStringLiteral("___none___"));
  QCOMPARE(proxy.rowCount(), 0);
  QCOMPARE(proxy.currentIndex(), -1);
  QCOMPARE(model.currentName(), QStringLiteral("zzz.txt"));

  proxy.setFilter(QString());
  QCOMPARE(proxy.currentName(), QStringLiteral("zzz.txt"));
  QCOMPARE(model.currentName(), QStringLiteral("zzz.txt"));
}

void CommandFieldTest::enterOnFileActivatesDoesNotNavigate() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy spy(&model, &DirectoryModel::fileActivated);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString root = model.path();
  const int row = findProxy(proxy, QStringLiteral("README.md"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QCOMPARE(canon(model.path()), canon(root));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(QFileInfo(spy.at(0).at(0).toString()).fileName(),
           QStringLiteral("README.md"));
}

void CommandFieldTest::spaceTogglesPeekAndJkStep() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b.png"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  StubPeek peek;
  keys.setPeekHost(&peek);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int fileRow = findProxy(proxy, QStringLiteral("a.png"));
  QVERIFY(fileRow >= 0);
  proxy.setCurrentIndex(fileRow);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("peek-open"));
  QVERIFY(peek.isOpen());
  QVERIFY(keys.listFocused());
  QVERIFY(!keys.fieldFocused());

  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(peek.lastStep, 1);
  QCOMPARE(keys.mode(), QStringLiteral("peek-open"));

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(!peek.isOpen());

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QVERIFY(peek.isOpen());
  keys.focusFilter();
  QVERIFY(!peek.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
}

void CommandFieldTest::mainQmlSlashThenSrcFilters() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("src_notes.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  QString errors;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                   &engine,
                   [&]() { errors = QStringLiteral("create failed"); });
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY2(!engine.rootObjects().isEmpty(),
           qPrintable(errors.isEmpty() ? QStringLiteral("Main.qml produced no "
                                                        "root object")
                                       : errors));

  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY2(list, "FileList objectName fileList");
  QVERIFY2(list->property("keyMachine").value<QObject *>() == &keys,
           "FileList.keyMachine must be the context KeyMachine, not undefined");
  QVERIFY2(list->property("filterProxy").value<QObject *>() == &proxy,
           "FileList.filterProxy must be the context FilterProxy");
  QVERIFY2(list->property("navStack").value<QObject *>() == &nav,
           "FileList.navStack must be the context NavStack");

  auto *field = window->findChild<QQuickItem *>(QStringLiteral("commandField"));
  QVERIFY2(field, "CommandField objectName commandField");
  QVERIFY2(field->property("keyMachine").value<QObject *>() == &keys,
           "CommandField.keyMachine must be the context KeyMachine");

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));
  QTest::keyClick(window, Qt::Key_Slash);
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.mode() == QStringLiteral("field-filter"); }, 1000));

  auto *input = window->findChild<QQuickItem *>(QStringLiteral("commandInput"));
  QVERIFY(input);
  QVERIFY(QTest::qWaitFor([&] { return input->hasActiveFocus(); }, 1000));
  for (const QChar c : QStringLiteral("src"))
    QTest::keyClick(window, c.toLatin1());
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.fieldText() == QStringLiteral("src"); }, 1000));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(proxy.filter(), QStringLiteral("src"));
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) < 0);
}

void CommandFieldTest::tRequestsTerminal() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy spy(&keys, &KeyMachine::terminalRequested);
  QVERIFY(keys.handleListKey(Qt::Key_T, Qt::NoModifier, QStringLiteral("t")));
  QCOMPARE(spy.count(), 1);
}

void CommandFieldTest::ctrlReturnRequestsOpenWith() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy spy(&keys, &KeyMachine::openWithRequested);
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::ControlModifier, QString()));
  QCOMPARE(spy.count(), 1);
}

void CommandFieldTest::focusFilterClosesActionOverlay() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  StubPeek peek;
  keys.setPeekHost(&peek);

  peek.openAction();
  QVERIFY(keys.actionOpen());
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!peek.actionOpen());
  QVERIFY(!keys.actionOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  peek.openAction();
  keys.focusFilter();
  QVERIFY(!peek.actionOpen());
  QVERIFY(!keys.actionOpen());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
}

void CommandFieldTest::paletteResolvePrefersBuiltins() {
  CommandPalette pal;
  pal.registerAction(QStringLiteral("synchro.action.trash"),
                     QStringLiteral("Move to Trash"));
  pal.registerAction(QStringLiteral("synchro.action.terminal"),
                     QStringLiteral("Terminal"));
  CommandSpec spec;
  QString err;
  QVERIFY(pal.resolve(QStringLiteral(":trash"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("trash"));
  QVERIFY(spec.builtin);
  QVERIFY(pal.resolve(QStringLiteral(":synchro.action.trash"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("synchro.action.trash"));
  QVERIFY(!spec.builtin);
  QVERIFY(pal.resolve(QStringLiteral(":terminal"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("synchro.action.terminal"));
  QVERIFY(pal.resolve(QStringLiteral(":Terminal"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("synchro.action.terminal"));
  QVERIFY(pal.resolve(QStringLiteral(":?"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("?"));
  QVERIFY(pal.resolve(QStringLiteral(":volumes"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("volumes"));
  QVERIFY(spec.builtin);
  QVERIFY(!pal.resolve(QStringLiteral(":h"), &spec, &err));
  QCOMPARE(err, QStringLiteral("ambiguous command"));
  QVERIFY(pal.resolve(QStringLiteral(":ho"), &spec, &err));
  QCOMPARE(spec.id, QStringLiteral("home"));
  QVERIFY(!pal.resolve(QStringLiteral(":nope"), &spec, &err));
  QCOMPARE(err, QStringLiteral("unknown command"));
}

void CommandFieldTest::colonFromListEntersFieldCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(keys.handleListKey(Qt::Key_Colon, Qt::ShiftModifier,
                             QStringLiteral(":")));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QVERIFY(keys.fieldFocused());
  QCOMPARE(keys.fieldText(), QStringLiteral(":"));
  QVERIFY(proxy.filter().isEmpty());
}

void CommandFieldTest::leadingColonPromotesAndDoesNotFilter() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("src")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral(":home"));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("src")) >= 0);
}

void CommandFieldTest::enterHomeNavigates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":home"));
  keys.acceptField();
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(QDir::homePath()));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.fieldText().isEmpty());
}

void CommandFieldTest::enterHiddenToggles() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral(".secret"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("visible.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(!model.showHidden());
  QVERIFY(findProxy(proxy, QStringLiteral(".secret")) < 0);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":hidden"));
  keys.acceptField();
  QVERIFY(model.showHidden());
  QVERIFY(findProxy(proxy, QStringLiteral(".secret")) >= 0);
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterGridListTogglesView() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(!keys.gridMode());
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":grid"));
  keys.acceptField();
  QVERIFY(keys.gridMode());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":list"));
  keys.acceptField();
  QVERIFY(!keys.gridMode());
}

void CommandFieldTest::enterFsnAndEscReturns() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(!keys.fsnMode());
  keys.setGridMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":fsn"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());
  QVERIFY(keys.gridMode());
  QCOMPARE(keys.statusMessage(), QStringLiteral("it's a unix system"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":park"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());

  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::NoModifier, QStringLiteral("v")));
  QVERIFY(!keys.fsnMode());
  QVERIFY(keys.gridMode());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":nedry"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!keys.fsnMode());
  QVERIFY(keys.gridMode());

  keys.setFsnMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":list"));
  keys.acceptField();
  QVERIFY(!keys.fsnMode());
  QVERIFY(!keys.gridMode());

  // :fsv alias, with tree|map view arguments.
  QVERIFY(keys.fsnTreeView());
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":fsv map"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());
  QVERIFY(!keys.fsnTreeView());
  QCOMPARE(keys.statusMessage(), QStringLiteral("it's a unix system"));
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!keys.fsnMode());
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":fsv treev"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());
  QVERIFY(keys.fsnTreeView());
  // Bad argument leaves the mode alone and hints instead.
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":fsv dinosaur"));
  keys.acceptField();
  QVERIFY(!keys.fsnMode());
  QCOMPARE(keys.statusMessage(), QStringLiteral(":fsv tree|map"));
  // Bad command left the field focused (first Esc clears the query, the
  // second refocuses the listing).
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(keys.listFocused());
  // M passes through to the 3D view for the map/tree toggle.
  keys.setFsnMode(true);
  QVERIFY(!keys.handleListKey(Qt::Key_M, Qt::NoModifier, QStringLiteral("m")));
  keys.setFsnMode(false);

  // Ctrl+M toggles fsv mode from the listing and from the field.
  QVERIFY(keys.handleListKey(Qt::Key_M, Qt::ControlModifier, QString()));
  QVERIFY(keys.fsnMode());
  QCOMPARE(keys.statusMessage(), QStringLiteral("it's a unix system"));
  QVERIFY(keys.handleListKey(Qt::Key_M, Qt::ControlModifier, QString()));
  QVERIFY(!keys.fsnMode());
  keys.focusCommand();
  QVERIFY(keys.handleFieldKey(Qt::Key_M, Qt::ControlModifier));
  QVERIFY(keys.fsnMode());
  QVERIFY(keys.handleFieldKey(Qt::Key_M, Qt::ControlModifier));
  QVERIFY(!keys.fsnMode());

  QVERIFY(keys.helpText().contains(QStringLiteral(":fsn")));
  QVERIFY(keys.helpText().contains(QStringLiteral(":fsv")));
}

void CommandFieldTest::enterTrashNoopsWithStatus() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setTrashAvailable(false);
  const QString before = model.path();

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":trash"));
  keys.acceptField();
  QCOMPARE(model.path(), before);
  QCOMPARE(keys.statusMessage(), QStringLiteral("trash is not available"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterTrashOpensTrashUrl() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":trash"));
  keys.acceptField();
  QCOMPARE(model.path(), QStringLiteral("trash://"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterEmptyNoopsWithStatus() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":empty"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("empty is only available in trash"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterRecentEmptyStatus() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setRecentStore(&recents);
  model.setRecentStore(&recents);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":recent"));
  keys.acceptField();
  QCOMPARE(model.path(), QStringLiteral("recent://"));
  QCOMPARE(model.count(), 0);
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterRecentJumpsToLast() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("foo.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("other")));
  RecentStore recents(tmp.filePath(QStringLiteral("recent.jsonl")), 50, 100);
  recents.record(tmp.filePath(QStringLiteral("foo.txt")),
                 QStringLiteral("text/plain"));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setRecentStore(&recents);
  model.setRecentStore(&recents);

  model.setPath(tmp.filePath(QStringLiteral("other")));
  QVERIFY(waitListingDone(model));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":recent"));
  keys.acceptField();
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.path(), QStringLiteral("recent://"));
  QCOMPARE(model.currentName(), QStringLiteral("foo.txt"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::enterHelpOpensOverlay() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":help"));
  keys.acceptField();
  QVERIFY(keys.helpOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.helpText().contains(QStringLiteral(":trash")));
  QVERIFY(keys.helpText().contains(QStringLiteral(":sort")));
  QVERIFY(keys.helpText().contains(QStringLiteral(":pin")));
  QVERIFY(keys.helpText().contains(QStringLiteral("Tab")));

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!keys.helpOpen());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":?"));
  keys.acceptField();
  QVERIFY(keys.helpOpen());
}

void CommandFieldTest::tabTogglesSearchFromList() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(keys.handleListKey(Qt::Key_Tab, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Tab, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.fieldText().isEmpty());
}

void CommandFieldTest::escCommandSingleStep() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  QCOMPARE(keys.fieldText(), QStringLiteral(":"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.fieldText().isEmpty());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":home"));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(keys.fieldText().isEmpty());
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::colonDoesNotReplaceListVerbs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("bbb.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(0);
  const int before = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(proxy.currentIndex(), qMin(before + 1, proxy.rowCount() - 1));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  const int mid = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_Colon, Qt::ShiftModifier,
                             QStringLiteral(":")));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QCOMPARE(proxy.currentIndex(), mid);
}

void CommandFieldTest::unknownAndAmbiguousStayInField() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":nope"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QCOMPARE(keys.fieldText(), QStringLiteral(":nope"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("unknown command"));

  keys.setFieldText(QStringLiteral(":h"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("ambiguous command"));
}

void CommandFieldTest::actionHandlerByIdAndTitle() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QString ran;
  keys.registerAction(QStringLiteral("synchro.action.terminal"),
                      QStringLiteral("Terminal"));
  keys.registerAction(QStringLiteral("synchro.action.trash"),
                      QStringLiteral("Move to Trash"));
  keys.setActionRunner([&](const QString &id, QString *) {
    ran = id;
    return true;
  });

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":terminal"));
  keys.acceptField();
  QCOMPARE(ran, QStringLiteral("synchro.action.terminal"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  ran.clear();
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":synchro.action.terminal"));
  keys.acceptField();
  QCOMPARE(ran, QStringLiteral("synchro.action.terminal"));

  ran.clear();
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":Terminal"));
  keys.acceptField();
  QCOMPARE(ran, QStringLiteral("synchro.action.terminal"));

  ran.clear();
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":trash"));
  keys.acceptField();
  QVERIFY(ran.isEmpty());
  QCOMPARE(model.path(), QStringLiteral("trash://"));
}

void CommandFieldTest::vTogglesGridFromList() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(!keys.gridMode());
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::NoModifier, QStringLiteral("v")));
  QVERIFY(keys.gridMode());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::NoModifier, QStringLiteral("v")));
  QVERIFY(!keys.gridMode());
}

void CommandFieldTest::successfulCommandClearsStatus() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":nope"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(), QStringLiteral("unknown command"));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));

  keys.setFieldText(QStringLiteral(":list"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.statusMessage().isEmpty());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":trash"));
  keys.acceptField();
  QCOMPARE(model.path(), QStringLiteral("trash://"));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":hidden"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(), QStringLiteral("hidden on"));
}

// :term toggles the terminal panel; arguments pick the side or close it.
void CommandFieldTest::termPanelCommands() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.panelSide(), QStringLiteral("bottom"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.terminal"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term right"));
  keys.acceptField();
  QCOMPARE(keys.panelSide(), QStringLiteral("right"));
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.terminal"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term top"));
  keys.acceptField();
  QCOMPARE(keys.panelSide(), QStringLiteral("top"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term off"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.panelSide(), QStringLiteral("top"));

  // bare :term toggles
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.terminal"));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());

  // bad argument hints without changing state
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term sideways"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.statusMessage(), QStringLiteral(":term [top|bottom|left|right|off]"));

  // chooser windows refuse the panel
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  keys.setChooserMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":term"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("no terminal in picker windows"));
  keys.setChooserMode(false);
}

// :files / :folders / :all hide the kind you are not hunting for; the
// chips drive the same proxy property.
void CommandFieldTest::kindFilterFilesFoldersAll() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("adir")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("bdir")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("one.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("two.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QTRY_COMPARE(proxy.rowCount(), 4);
  QCOMPARE(proxy.kindFilter(), QStringLiteral("all"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":files"));
  keys.acceptField();
  QCOMPARE(proxy.kindFilter(), QStringLiteral("files"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("files only"));
  QCOMPARE(proxy.rowCount(), 2);
  for (int i = 0; i < proxy.rowCount(); ++i)
    QVERIFY(!proxy.data(proxy.index(i, 0), DirectoryModel::IsDirRole).toBool());
  QVERIFY(proxy.currentIndex() >= 0); // cursor snapped to a surviving row

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":folders"));
  keys.acceptField();
  QCOMPARE(proxy.kindFilter(), QStringLiteral("folders"));
  QCOMPARE(proxy.rowCount(), 2);
  for (int i = 0; i < proxy.rowCount(); ++i)
    QVERIFY(proxy.data(proxy.index(i, 0), DirectoryModel::IsDirRole).toBool());

  // Kind filter composes with the name filter.
  proxy.setFilter(QStringLiteral("adir"));
  QCOMPARE(proxy.rowCount(), 1);
  proxy.setFilter(QString());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":all"));
  keys.acceptField();
  QCOMPARE(proxy.kindFilter(), QStringLiteral("all"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("showing all"));
  QCOMPARE(proxy.rowCount(), 4);

  // Direct property writes (the chip path) behave identically.
  proxy.setKindFilter(QStringLiteral("folders"));
  QCOMPARE(proxy.rowCount(), 2);
  proxy.setKindFilter(QStringLiteral("bogus"));
  QCOMPARE(proxy.kindFilter(), QStringLiteral("all"));
  QCOMPARE(proxy.rowCount(), 4);
}

// The Type column shows friendly labels: Folder for dirs, the mime
// description for suffixed files, Program/File for suffix-less ones.
void CommandFieldTest::typeLabelsForColumns() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("subdir")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("LICENSE"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QTRY_COMPARE(proxy.rowCount(), 3);

  auto labelOf = [&](const QString &name) {
    for (int i = 0; i < proxy.rowCount(); ++i) {
      if (proxy.data(proxy.index(i, 0), DirectoryModel::NameRole).toString() ==
          name)
        return proxy.data(proxy.index(i, 0), DirectoryModel::TypeLabelRole)
            .toString();
    }
    return QString();
  };
  QCOMPARE(labelOf(QStringLiteral("subdir")), QStringLiteral("Folder"));
  QTRY_VERIFY(!labelOf(QStringLiteral("notes.txt")).isEmpty());
  QVERIFY(labelOf(QStringLiteral("notes.txt")) != QLatin1String("text/plain"));
  QTRY_COMPARE(labelOf(QStringLiteral("LICENSE")), QStringLiteral("File"));
}

// Name sort is collation-based: case-insensitive, numeric-aware (file2
// before file10), directories ahead of files.
void CommandFieldTest::naturalSortOrdersDirsFirst() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("zeta-dir")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("file10.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("file2.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("Alpha.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("beta.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QTRY_COMPARE(proxy.rowCount(), 5);

  QStringList order;
  for (int i = 0; i < proxy.rowCount(); ++i)
    order << proxy.data(proxy.index(i, 0), DirectoryModel::NameRole).toString();
  const QStringList expected{
      QStringLiteral("zeta-dir"), QStringLiteral("Alpha.txt"),
      QStringLiteral("beta.txt"), QStringLiteral("file2.txt"),
      QStringLiteral("file10.txt")};
  QCOMPARE(order, expected);

  // Descending flips names but keeps the dir pinned first.
  proxy.setSortOrder(QStringLiteral("desc"));
  QTRY_COMPARE(proxy.data(proxy.index(0, 0), DirectoryModel::NameRole)
                   .toString(),
               QStringLiteral("zeta-dir"));
  QCOMPARE(proxy.data(proxy.index(1, 0), DirectoryModel::NameRole).toString(),
           QStringLiteral("file10.txt"));
}

void CommandFieldTest::enterSortChangesRoleAndFlips() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("z.bin"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(proxy.sortRoleName(), QStringLiteral("name"));
  QCOMPARE(proxy.sortOrder(), QStringLiteral("asc"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sort size"));
  keys.acceptField();
  QCOMPARE(proxy.sortRoleName(), QStringLiteral("size"));
  QCOMPARE(proxy.sortOrder(), QStringLiteral("asc"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("sort size asc"));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sort"));
  keys.acceptField();
  QCOMPARE(proxy.sortRoleName(), QStringLiteral("size"));
  QCOMPARE(proxy.sortOrder(), QStringLiteral("desc"));
  QCOMPARE(keys.statusMessage(), QStringLiteral("sort size desc"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sort mtime asc"));
  keys.acceptField();
  QCOMPARE(proxy.sortRoleName(), QStringLiteral("mtime"));
  QCOMPARE(proxy.sortOrder(), QStringLiteral("asc"));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sort nope"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("sort name|size|mtime|type [asc|desc]"));
  QCOMPARE(proxy.sortRoleName(), QStringLiteral("mtime"));
}

void CommandFieldTest::mainQmlColonEntersCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  QString errors;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                   &engine,
                   [&]() { errors = QStringLiteral("create failed"); });
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY2(!engine.rootObjects().isEmpty(),
           qPrintable(errors.isEmpty() ? QStringLiteral("Main.qml produced no "
                                                        "root object")
                                       : errors));

  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));
  QTest::keyClick(window, Qt::Key_Colon, Qt::ShiftModifier);
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.mode() == QStringLiteral("field-command"); }, 1000));
  QCOMPARE(keys.fieldText(), QStringLiteral(":"));

  auto *input = window->findChild<QQuickItem *>(QStringLiteral("commandInput"));
  QVERIFY(input);
  QVERIFY(QTest::qWaitFor([&] { return input->hasActiveFocus(); }, 1000));
}

void CommandFieldTest::mainQmlGridClickAfterColonPops() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("bbb.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  QString errors;
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                   &engine,
                   [&]() { errors = QStringLiteral("create failed"); });
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY2(!engine.rootObjects().isEmpty(),
           qPrintable(errors.isEmpty() ? QStringLiteral("Main.qml produced no "
                                                        "root object")
                                       : errors));

  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  keys.setGridMode(true);
  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY2(grid, "FileGrid objectName fileGrid");
  QVERIFY(QTest::qWaitFor(
      [&] {
        return grid->isVisible() && grid->width() > 0 && grid->height() > 0;
      },
      1000));
  grid->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return grid->hasActiveFocus(); }, 1000));

  QTest::keyClick(window, Qt::Key_Colon, Qt::ShiftModifier);
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.mode() == QStringLiteral("field-command"); }, 1000));

  const QPoint pos = grid->mapToScene(QPointF(24, 24)).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pos);
  if (keys.fieldFocused())
    grid->forceActiveFocus();
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.mode() == QStringLiteral("list-focused"); }, 1000));

  QTest::keyClick(window, Qt::Key_Escape);
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::mainQmlFsnMounts() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aaa.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("bbb.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->resize(800, 600);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":fsn"));
  keys.acceptField();
  QVERIFY(keys.fsnMode());
  auto *city = window->findChild<QQuickItem *>(QStringLiteral("fileFsn"));
  QVERIFY(QTest::qWaitFor(
      [&] {
        city = window->findChild<QQuickItem *>(QStringLiteral("fileFsn"));
        auto *list =
            window->findChild<QQuickItem *>(QStringLiteral("fileList"));
        return city && city->isVisible() && city->width() > 0 &&
               (!list || !list->isVisible());
      },
      2000));
  QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("fileFsnView")));
}

void CommandFieldTest::leadingQuestionPromotesToFieldSearch() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("README.md"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("?foo.bar"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) >= 0);
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("?foo.bar")),
           QStringLiteral("foo.bar"));
}

void CommandFieldTest::questionQuestionIsContentSearch() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("??todo"));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(KeyMachine::isSearchText(QStringLiteral("??todo")));
  QVERIFY(KeyMachine::isContentSearchText(QStringLiteral("??todo")));
  QCOMPARE(KeyMachine::searchQuery(QStringLiteral("??todo")),
           QStringLiteral("todo"));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  CommandFieldTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "command_field_test.moc"
