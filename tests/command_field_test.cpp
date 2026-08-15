#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "PeekHost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
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
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("qxy_only"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zzz.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(findProxy(proxy, QStringLiteral("qxy_only")) >= 0);

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QCOMPARE(proxy.currentName(), QStringLiteral("qxy_only"));
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

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  CommandFieldTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "command_field_test.moc"
