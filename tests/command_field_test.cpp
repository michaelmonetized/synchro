#include "CommandPalette.h"
#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "PeekHost.h"
#include "RecentStore.h"
#include "SelectionModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
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

class FakeAgentSearch : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString agent READ agent CONSTANT)
  Q_PROPERTY(QString error READ error NOTIFY changed)
  Q_PROPERTY(QStringList thumbnails READ thumbnails CONSTANT)

public:
  using QObject::QObject;
  bool running() const { return false; }
  QString agent() const { return QStringLiteral("test-agent"); }
  QString error() const { return {}; }
  QStringList thumbnails() const { return {}; }
  Q_INVOKABLE bool start(const QString &, const QString &) { return true; }
  Q_INVOKABLE void cancel() {}
  void publish(const QString &label, const QString &sql, const QString &cwd) {
    emit resultReady(label, sql, cwd);
  }

signals:
  void changed();
  void resultReady(const QString &label, const QString &sql,
                   const QString &cwd);
};

class FakeSqlShell : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool mainSqlBusy READ mainSqlBusy WRITE setMainSqlBusy NOTIFY changed)

public:
  using QObject::QObject;
  bool mainSqlBusy() const { return m_busy; }
  void setMainSqlBusy(bool busy) {
    if (m_busy == busy)
      return;
    m_busy = busy;
    emit changed();
  }

signals:
  void changed();

private:
  bool m_busy = false;
};

class CommandFieldTest : public QObject {
  Q_OBJECT

private slots:
  void launchIsListFocused();
  void bareSrcFiltersDoesNotNavigate();
  void slashAndCtrlKEnterFieldFilter();
  void ctrlLEntersFieldJumpWithPathSelected();
  void enterFilterOpensSelectedResult();
  void enterOnSrcSlashNavigates();
  void pathSigilRules();
  void missingPrefixWithSlashStillFilters();
  void escSingleStepPop();
  void typingStartsLocalFilterAndEscapeRestoresSelection();
  void printableLettersAreNotModalVerbs();
  void gridNavigationPreservesVisualColumn();
  void fieldFilterTypesVerbsAsText();
  void filterIsCaseInsensitiveSubstring();
  void failedJumpStaysInField();
  void goBackRestoresFilter();
  void emptyFilterKeepsSourceCursor();
  void enterOnFileActivatesDoesNotNavigate();
  void spaceTogglesPeekAndJkStep();
  void spaceLookModePreservesShiftPeek();
  void mainQmlSlashThenSrcFilters();
  void mainQmlAgentResultUsesCurrentWindow();
  void plainTStartsFilter();
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
  void questionStartsSearchFromList();
  void enterSortChangesRoleAndFlips();
  void naturalSortOrdersDirsFirst();
  void typeLabelsForColumns();
  void kindFilterFilesFoldersAll();
  void termPanelCommands();
  void sqlPanelCommand();
  void sqlPanelPublishesBusyAndUsesProminentEditor();
  void agentSearchCommand();
  void semanticSearchCommand();
  void settingsCommand();
  void flowPanelCommand();
  void escCommandSingleStep();
  void colonTakesPriorityOverPrintableFilter();
  void unknownAndAmbiguousStayInField();
  void actionHandlerByIdAndTitle();
  void plainVStartsFilter();
  void ctrlNumberSwitchesViews();
  void successfulCommandClearsStatus();
  void mainQmlColonEntersCommand();
  void mainQmlGridClickAfterColonPops();
  void quickFilterStaysInCurrentFolder();
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

void CommandFieldTest::enterFilterOpensSelectedResult() {
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
  QVERIFY(keys.handleFieldKey(Qt::Key_Return, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(waitListingDone(model));
  QCOMPARE(canon(model.path()), canon(tmp.filePath(QStringLiteral("src"))));
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(proxy.filter().isEmpty());
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
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(proxy.filter().isEmpty());
  QVERIFY(findProxy(proxy, QStringLiteral("README.md")) >= 0);

  keys.focusFilter();
  keys.setFieldText(QStringLiteral("README"));
  keys.focusList();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(!proxy.filter().isEmpty());
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(proxy.filter().isEmpty());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void CommandFieldTest::typingStartsLocalFilterAndEscapeRestoresSelection() {
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
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("aaa.txt")));
  QCOMPARE(proxy.currentName(), QStringLiteral("aaa.txt"));

  QVERIFY(keys.handleListKey(Qt::Key_M, Qt::NoModifier, QStringLiteral("m")));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(keys.fieldText(), QStringLiteral("m"));
  QCOMPARE(proxy.rowCount(), 1);
  QCOMPARE(proxy.currentName(), QStringLiteral("mxy_only"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.fieldText().isEmpty());
  QCOMPARE(proxy.rowCount(), 3);
  QCOMPARE(proxy.currentName(), QStringLiteral("aaa.txt"));
}

void CommandFieldTest::quickFilterStaysInCurrentFolder() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("Downloads")));
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("aaa/Downloads-inside")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("random-download-note.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const QString home = model.path();
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("aaa")));

  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier,
                             QStringLiteral("d")));
  keys.setFieldText(QStringLiteral("downloads"));

  QCOMPARE(model.path(), home);
  QCOMPARE(proxy.rowCount(), 1);
  QCOMPARE(proxy.currentName(), QStringLiteral("Downloads"));
  QVERIFY(findProxy(proxy, QStringLiteral("Downloads-inside")) < 0);
}

void CommandFieldTest::printableLettersAreNotModalVerbs() {
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
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(keys.fieldText(), QStringLiteral("j"));
  QCOMPARE(proxy.rowCount(), 1);
  QCOMPARE(proxy.currentName(), QStringLiteral("jjj.txt"));
}

void CommandFieldTest::gridNavigationPreservesVisualColumn() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 10; ++i)
    QVERIFY(writeFile(tmp.filePath(QStringLiteral("item-%1").arg(i, 2, 10,
                                                                  QLatin1Char('0')))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(proxy.rowCount(), 10);
  keys.setGridMode(true);
  keys.setGridStride(4);

  // The final row contains only columns zero and one. Moving down from visual
  // column three stays on that row instead of clamping diagonally by index.
  proxy.setCurrentIndex(7);
  QVERIFY(keys.handleListKey(Qt::Key_Down, Qt::NoModifier, QString()));
  QCOMPARE(proxy.currentIndex(), 9);
  QVERIFY(keys.handleListKey(Qt::Key_Up, Qt::NoModifier, QString()));
  QCOMPARE(proxy.currentIndex(), 5);

  // A relayout immediately changes vertical adjacency.
  keys.setGridStride(3);
  proxy.setCurrentIndex(5);
  QVERIFY(keys.handleListKey(Qt::Key_Down, Qt::NoModifier, QString()));
  QCOMPARE(proxy.currentIndex(), 8);
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
  keys.focusList();
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

void CommandFieldTest::spaceLookModePreservesShiftPeek() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  StubPeek peek;
  keys.setPeekHost(&peek);
  keys.setLookKeyMode(true);
  QSignalSpy lookToggle(&keys, &KeyMachine::lookToggleRequested);

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QCOMPARE(lookToggle.count(), 1);
  QVERIFY(!peek.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier, QString()));
  QVERIFY(peek.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("peek-open"));

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QVERIFY(!peek.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QCOMPARE(lookToggle.count(), 1);
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
  SelectionModel selection(&proxy, &model);
  keys.setSelection(&selection);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  selection.click(findProxy(proxy, QStringLiteral("src")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("selectionModel"),
                                           &selection);
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
  QCOMPARE(list->property("outboundDragAction").toInt(),
           int(Qt::CopyAction));
  QVERIFY(window->property("lookWanted").toBool());

  auto *field = window->findChild<QQuickItem *>(QStringLiteral("commandField"));
  QVERIFY2(field, "CommandField objectName commandField");
  QVERIFY2(field->property("keyMachine").value<QObject *>() == &keys,
           "CommandField.keyMachine must be the context KeyMachine");

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));
  QTest::keyClick(window, Qt::Key_Slash);
  QVERIFY(QTest::qWaitFor(
      [&] { return keys.mode() == QStringLiteral("field-filter"); }, 1000));
  QTRY_VERIFY(!window->property("lookWanted").toBool());

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

void CommandFieldTest::mainQmlAgentResultUsesCurrentWindow() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  FakeAgentSearch agent;

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
  engine.rootContext()->setContextProperty(QStringLiteral("agentSearch"),
                                           &agent);
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  QObject *root = engine.rootObjects().constFirst();

  auto *busy = root->findChild<QObject *>(QStringLiteral("mainQueryBusy"));
  QVERIFY(busy);
  QVERIFY(!busy->property("visible").toBool());
  QVERIFY(root->setProperty("mainSqlBusy", true));
  QTRY_VERIFY(busy->property("visible").toBool());
  QVERIFY(root->setProperty("mainSqlBusy", false));

  const QString sql = QStringLiteral(
      "select name, path, is_dir from tree where kind = 'image'");
  agent.publish(QStringLiteral("Agent images"), sql, QStringLiteral("/tmp"));
  QTRY_COMPARE(keys.panelId(), QStringLiteral("synchro.panel.sql"));
  const QVariantMap pending = root->property("pendingSqlBookmark").toMap();
  QCOMPARE(pending.value(QStringLiteral("name")).toString(),
           QStringLiteral("Agent images"));
  QCOMPARE(pending.value(QStringLiteral("sql")).toString(), sql);
  QCOMPARE(pending.value(QStringLiteral("cwd")).toString(),
           QStringLiteral("/tmp"));
}

void CommandFieldTest::plainTStartsFilter() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QVERIFY(keys.handleListKey(Qt::Key_T, Qt::NoModifier, QStringLiteral("t")));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(keys.fieldText(), QStringLiteral("t"));
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

  QVERIFY(keys.handleListKey(Qt::Key_M, Qt::ControlModifier, QString()));
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
  QVERIFY(keys.helpText().contains(QStringLiteral("Arrow keys")));

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!keys.helpOpen());

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":?"));
  keys.acceptField();
  QVERIFY(keys.helpOpen());
}

void CommandFieldTest::questionStartsSearchFromList() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(keys.handleListKey(Qt::Key_Question, Qt::NoModifier,
                             QStringLiteral("?")));
  QCOMPARE(keys.mode(), QStringLiteral("field-search"));
  QCOMPARE(keys.fieldText(), QStringLiteral("?"));
  QVERIFY(keys.handleFieldKey(Qt::Key_Escape, Qt::NoModifier));
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

void CommandFieldTest::colonTakesPriorityOverPrintableFilter() {
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
  QVERIFY(keys.handleListKey(Qt::Key_Colon, Qt::ShiftModifier,
                             QStringLiteral(":")));
  QCOMPARE(keys.mode(), QStringLiteral("field-command"));
  QCOMPARE(keys.fieldText(), QStringLiteral(":"));
  QVERIFY(proxy.filter().isEmpty());
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

void CommandFieldTest::plainVStartsFilter() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(!keys.gridMode());
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::NoModifier, QStringLiteral("v")));
  QVERIFY(!keys.gridMode());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QCOMPARE(keys.fieldText(), QStringLiteral("v"));
}

void CommandFieldTest::ctrlNumberSwitchesViews() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  QVERIFY(!keys.gridMode());
  QVERIFY(keys.handleListKey(Qt::Key_2, Qt::ControlModifier, QString()));
  QVERIFY(keys.gridMode());
  QVERIFY(keys.handleListKey(Qt::Key_1, Qt::ControlModifier, QString()));
  QVERIFY(!keys.gridMode());

  keys.focusFilter();
  QVERIFY(keys.handleFieldKey(Qt::Key_2, Qt::ControlModifier));
  QVERIFY(keys.gridMode());
  QVERIFY(keys.handleFieldKey(Qt::Key_1, Qt::ControlModifier));
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

  // :panel <name> completes the id prefix and toggles
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":panel duckdb"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.duckdb"));
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":panel off"));
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

void CommandFieldTest::sqlPanelCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy focusSpy(&keys, &KeyMachine::panelFocusRequested);
  QSignalSpy scanSpy(&keys, &KeyMachine::sqlScanRequested);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sql"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.sql"));
  QCOMPARE(focusSpy.size(), 1);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sql scan"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.sql"));
  QCOMPARE(scanSpy.size(), 1);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":panel off"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());

  keys.setChooserMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":sql"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("no SQL workbench in picker windows"));
}

void CommandFieldTest::sqlPanelPublishesBusyAndUsesProminentEditor() {
  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  QQmlComponent component(
      &engine, QUrl::fromLocalFile(QStringLiteral(SYNCHRO_SQL_PANEL_QML)));
  QVERIFY2(component.isReady(), qPrintable(component.errorString()));
  std::unique_ptr<QObject> panel(component.create());
  QVERIFY2(panel, qPrintable(component.errorString()));

  FakeSqlShell shell;
  QVERIFY(panel->setProperty("shell", QVariant::fromValue<QObject *>(&shell)));
  auto *editor = panel->findChild<QObject *>(QStringLiteral("sqlQueryEdit"));
  auto *spinner = panel->findChild<QObject *>(QStringLiteral("sqlResultSpinner"));
  QVERIFY(editor);
  QVERIFY(spinner);
  const QFont font = editor->property("font").value<QFont>();
  QVERIFY(font.bold());
  QVERIFY(font.pixelSize() > 0);

  QVERIFY(panel->setProperty("running", true));
  QTRY_VERIFY(shell.mainSqlBusy());
  QTRY_VERIFY(spinner->property("visible").toBool());
  QVERIFY(panel->setProperty("running", false));
  QTRY_VERIFY(!shell.mainSqlBusy());
}

void CommandFieldTest::agentSearchCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy searchSpy(&keys, &KeyMachine::agentSearchRequested);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":ask"));
  keys.acceptField();
  QCOMPARE(searchSpy.size(), 1);
  QVERIFY(keys.fieldText().isEmpty());

  keys.setChooserMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":ask"));
  keys.acceptField();
  QCOMPARE(searchSpy.size(), 1);
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("no agent search in picker windows"));
}

void CommandFieldTest::semanticSearchCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy searchSpy(&keys, &KeyMachine::semanticSearchRequested);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":see blue industrial buildings"));
  keys.acceptField();
  QCOMPARE(searchSpy.size(), 1);
  QCOMPARE(searchSpy.takeFirst().at(0).toString(),
           QStringLiteral("blue industrial buildings"));
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(keys.helpText().contains(QStringLiteral(":see blue images")));

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":semantic"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(),
           QStringLiteral(":see <visual description>"));

  keys.setChooserMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":see cats"));
  keys.acceptField();
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("no semantic search in picker windows"));
}

void CommandFieldTest::settingsCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy settingsSpy(&keys, &KeyMachine::settingsRequested);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":settings"));
  keys.acceptField();
  QCOMPARE(settingsSpy.size(), 1);
  QVERIFY(keys.fieldText().isEmpty());
  QVERIFY(keys.helpText().contains(QStringLiteral("Ctrl+,")));
}

void CommandFieldTest::flowPanelCommand() {
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  QSignalSpy focusSpy(&keys, &KeyMachine::panelFocusRequested);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":flow"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.omaflow"));
  QCOMPARE(focusSpy.size(), 1);

  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":omaflow"));
  keys.acceptField();
  QCOMPARE(keys.panelId(), QStringLiteral("synchro.panel.omaflow"));
  QCOMPARE(focusSpy.size(), 2);

  keys.setPanelId(QString());
  keys.setChooserMode(true);
  keys.focusCommand();
  keys.setFieldText(QStringLiteral(":flow"));
  keys.acceptField();
  QVERIFY(keys.panelId().isEmpty());
  QCOMPARE(keys.statusMessage(),
           QStringLiteral("no automation panel in picker windows"));
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
  QCOMPARE(grid->property("outboundDragAction").toInt(),
           int(Qt::CopyAction));
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
  auto *rhiView =
      window->findChild<QQuickItem *>(QStringLiteral("fsnRhiView"));
  QVERIFY(rhiView);
  const QVariantMap scenePalette =
      rhiView->property("scenePalette").toMap();
  QVERIFY(scenePalette.value(QStringLiteral("image")).value<QColor>() !=
          scenePalette.value(QStringLiteral("video")).value<QColor>());
  QVERIFY(scenePalette.value(QStringLiteral("code")).value<QColor>() !=
          scenePalette.value(QStringLiteral("data")).value<QColor>());
  QVERIFY(window->findChild<QQuickItem *>(QStringLiteral("fsnTypeKey")));
  city->setProperty("camAnim", false);
  city->setProperty("yaw", 0.0);
  city->setProperty("pitch", 0.0);
  const qreal beforePanX = city->property("tx").toReal();
  const qreal beforePanY = city->property("ty").toReal();
  const qreal beforePanZ = city->property("tz").toReal();
  QVERIFY(QMetaObject::invokeMethod(city, "panByPixels",
                                    Q_ARG(QVariant, 48.0),
                                    Q_ARG(QVariant, 24.0)));
  QVERIFY(city->property("tx").toReal() > beforePanX);
  QVERIFY(city->property("ty").toReal() < beforePanY);
  QCOMPARE(city->property("tz").toReal(), beforePanZ);
  auto *viewControl =
      window->findChild<QQuickItem *>(QStringLiteral("viewControl"));
  auto *lookToggle =
      window->findChild<QQuickItem *>(QStringLiteral("browserLookToggle"));
  QVERIFY(viewControl);
  QVERIFY(lookToggle);
  QVERIFY(viewControl->isVisible());
  QVERIFY(lookToggle->isVisible());
  QCOMPARE(viewControl->property("currentId").toString(),
           QStringLiteral("tree"));
  keys.setFsnTreeView(false);
  QTRY_COMPARE_WITH_TIMEOUT(viewControl->property("currentId").toString(),
                            QStringLiteral("map"), 1000);
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
