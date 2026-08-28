#include "DirectoryModel.h"
#include "FileOpEngine.h"
#include "FilterProxy.h"
#include "KeyMachine.h"
#include "NavStack.h"
#include "SelectionModel.h"
#include "UndoStack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QUrl>
#include <QVariant>
#include <QTemporaryDir>
#include <QTest>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace {

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

bool waitIdle(FileOpEngine &ops, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !ops.busy(); }, timeoutMs);
}

bool writeFile(const QString &path, const QByteArray &data = QByteArray("x")) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return false;
  f.write(data);
  return true;
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

QString canon(const QString &path) {
  const QString c = QFileInfo(path).canonicalFilePath();
  return c.isEmpty() ? QFileInfo(path).absoluteFilePath() : c;
}

} // namespace

class FileOpsTest : public QObject {
  Q_OBJECT

private slots:
  void forbiddenRootAndDotDot();
  void collisionNameSuffix();
  void defaultSelectedIsCursor();
  void spaceDoesNotToggle();
  void ctrlSpaceTogglesCursor();
  void ctrlASelectsFiltered();
  void visualRangeAndEscKeepsSet();
  void escCollapsesMultiSetThenFilter();
  void leavingDirCollapsesSelection();
  void clickAndShiftClickAndCtrlClick();
  void recursivePathSelectionIsFirstClass();
  void previewSummaryIsBoundedAndUseful();
  void mkdirAndUndo();
  void mkdirCollisionSuffix();
  void renameAndUndo();
  void copyFileAndUndo();
  void copyDirTree();
  void copyCollisionSuffix();
  void cutDoesNotDelete();
  void copyPasteThenCutPaste();
  void moveAndUndo();
  void duplicateSuffixes();
  void refuseMkdirOnSlash();
  void refuseCopyOfSlash();
  void keysYankDeletePaste();
  void keysRenameMkdirUndo();
  void enterActivatesSelectedFiles();
  void undoStackCapsAt32();
  void refuseCopyIntoSelf();
  void refuseMoveIntoSelf();
  void renameSameNameIsNoop();
  void moveSameDirIsNoop();
  void refuseSpecialFileCopy();
  void slashAndPeriodExitVisual();
  void wasdMovesAndShiftLeaps();
  void arrowsWalkHierarchy();
  void visibleThumbsFollowSortedProxy();
  void partialCopyKeepsUndo();
  void pasteReadsOsClipboardFromOtherEngine();
  void pasteOsGnomeCutMoves();
  void pasteReadsBareUriList();
  void copyEmitsProgress();
  void dragMimeHasUriListAndSynchroMark();
  void pathsFromDropReadsGnomeAndUrls();
  void dropOnCopyKeepsSource();
  void dropOnAutoSameDeviceMoves();
  void dropOnRejectsVirtualDest();
  void dropOnFolderIntoSelfFails();
  void dropOnSameDirMoveIsAlreadyThere();
};

void FileOpsTest::forbiddenRootAndDotDot() {
  QVERIFY(FileOpEngine::isForbiddenPath(QStringLiteral("/")));
  QVERIFY(FileOpEngine::isForbiddenPath(QStringLiteral("/..")));
  QVERIFY(FileOpEngine::isForbiddenPath(QStringLiteral("..")));
  QVERIFY(FileOpEngine::isForbiddenPath(QStringLiteral("../foo")));
  QVERIFY(!FileOpEngine::isForbiddenPath(QDir::tempPath()));
  QVERIFY(FileOpEngine::isProtectedUnlink(QDir::homePath()));
  QVERIFY(FileOpEngine::isSameOrDescendant(QStringLiteral("/tmp/foo"),
                                          QStringLiteral("/tmp/foo")));
  QVERIFY(FileOpEngine::isSameOrDescendant(QStringLiteral("/tmp/foo"),
                                          QStringLiteral("/tmp/foo/bar")));
  QVERIFY(!FileOpEngine::isSameOrDescendant(QStringLiteral("/tmp/foo"),
                                           QStringLiteral("/tmp/foobar")));
}

void FileOpsTest::collisionNameSuffix() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("foo.md"))));
  QCOMPARE(FileOpEngine::collisionName(tmp.path(), QStringLiteral("foo.md")),
           QStringLiteral("foo (1).md"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("foo (1).md"))));
  QCOMPARE(FileOpEngine::collisionName(tmp.path(), QStringLiteral("foo.md")),
           QStringLiteral("foo (2).md"));
  QCOMPARE(FileOpEngine::collisionName(tmp.path(), QStringLiteral("missing")),
           QStringLiteral("missing"));
}

void FileOpsTest::defaultSelectedIsCursor() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("c"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  SelectionModel sel(&proxy, &model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(sel.selectedCount(), 1);
  QVERIFY(sel.isSelected(proxy.currentIndex()));
  QVERIFY(!sel.statusText().contains(QStringLiteral("selected")));
}

void FileOpsTest::spaceDoesNotToggle() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(sel.selectedCount(), 1);
  const int cur = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QCOMPARE(sel.selectedCount(), 1);
  QVERIFY(sel.isSelected(cur));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void FileOpsTest::ctrlSpaceTogglesCursor() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(sel.selectedCount(), 1);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::ControlModifier, QString()));
  QCOMPARE(sel.selectedCount(), 0);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::ControlModifier, QString()));
  QCOMPARE(sel.selectedCount(), 1);
}

void FileOpsTest::ctrlASelectsFiltered() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aa"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("ab"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zz"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setFilter(QStringLiteral("a"));
  QCOMPARE(proxy.count(), 2);
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::ControlModifier, QString()));
  QCOMPARE(sel.selectedCount(), 2);
  QVERIFY(sel.statusText().startsWith(QStringLiteral("2 selected")));
}

void FileOpsTest::visualRangeAndEscKeepsSet() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("c"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("d"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(0);
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::ShiftModifier, QStringLiteral("V")));
  QCOMPARE(keys.mode(), QStringLiteral("visual-select"));
  QVERIFY(sel.visual());
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(sel.selectedCount(), 3);
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(!sel.visual());
  QCOMPARE(sel.selectedCount(), 3);
}

void FileOpsTest::escCollapsesMultiSetThenFilter() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aa"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("ab"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zz"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  keys.focusFilter();
  keys.setFieldText(QStringLiteral("a"));
  keys.acceptField();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::ControlModifier, QString()));
  QCOMPARE(sel.selectedCount(), 2);
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QCOMPARE(sel.selectedCount(), 1);
  QCOMPARE(proxy.filter(), QStringLiteral("a"));
  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(proxy.filter().isEmpty());
}

void FileOpsTest::leavingDirCollapsesSelection() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("child/in"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  SelectionModel sel(&proxy, &model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  sel.selectAll();
  QVERIFY(sel.selectedCount() > 1);
  const int row = findProxy(proxy, QStringLiteral("child"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  proxy.activateCurrent();
  QVERIFY(waitListingDone(model));
  QVERIFY(QTest::qWaitFor([&] { return sel.selectedCount() == 1; }, 2000));
  QCOMPARE(sel.selectedCount(), 1);
}

void FileOpsTest::clickAndShiftClickAndCtrlClick() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("a"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("b"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("c"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  SelectionModel sel(&proxy, &model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(proxy.count() >= 3);
  sel.click(0);
  QCOMPARE(sel.selectedCount(), 1);
  QVERIFY(sel.isSelected(0));
  sel.shiftClick(2);
  QCOMPARE(sel.selectedCount(), 3);
  sel.ctrlClick(1);
  QCOMPARE(sel.selectedCount(), 2);
}

void FileOpsTest::recursivePathSelectionIsFirstClass() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("folder/deeper")));
  const QString nested =
      tmp.filePath(QStringLiteral("folder/deeper/notes.md"));
  QVERIFY(writeFile(nested, "# nested\n"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("top.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  SelectionModel sel(&proxy, &model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  sel.selectPath(nested, QStringLiteral("notes.md"), false, 9);
  QCOMPARE(sel.selectedCount(), 1);
  QCOMPARE(sel.selectedPaths(), QStringList{nested});
  QCOMPARE(sel.cursorPath(), nested);
  QCOMPARE(sel.cursorName(), QStringLiteral("notes.md"));
  QCOMPARE(sel.primaryItem().value(QStringLiteral("path")).toString(), nested);

  const int top = findProxy(proxy, QStringLiteral("top.txt"));
  QVERIFY(top >= 0);
  sel.click(top);
  QCOMPARE(sel.selectedPaths(),
           QStringList{tmp.filePath(QStringLiteral("top.txt"))});
}

void FileOpsTest::previewSummaryIsBoundedAndUseful() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("photo.png")), "pixels"));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("notes.md")), "notes"));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("folder")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  SelectionModel sel(&proxy, &model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  sel.selectAll();

  const QVariantMap exact = sel.previewSummary(2, 10);
  QCOMPARE(exact.value(QStringLiteral("count")).toInt(), 3);
  QCOMPARE(exact.value(QStringLiteral("items")).toList().size(), 2);
  QVERIFY(exact.value(QStringLiteral("aggregateComplete")).toBool());
  QCOMPARE(exact.value(QStringLiteral("files")).toInt(), 2);
  QCOMPARE(exact.value(QStringLiteral("folders")).toInt(), 1);
  QVERIFY(!exact.value(QStringLiteral("kindSummary")).toStringList().isEmpty());

  const QVariantMap sampled = sel.previewSummary(1, 1);
  QVERIFY(!sampled.value(QStringLiteral("aggregateComplete")).toBool());
  QCOMPARE(sampled.value(QStringLiteral("aggregateCount")).toInt(), 1);
}

void FileOpsTest::mkdirAndUndo() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  FileOpEngine ops;
  ops.makeDir(tmp.path(), QStringLiteral("New folder"));
  QVERIFY(waitIdle(ops));
  QVERIFY(ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("New folder"))).isDir());
  QVERIFY(ops.canUndo());
  ops.undo();
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("New folder"))).exists());
}

void FileOpsTest::mkdirCollisionSuffix() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("New folder")));
  FileOpEngine ops;
  ops.makeDir(tmp.path(), QStringLiteral("New folder"));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("New folder (1)"))).isDir());
}

void FileOpsTest::renameAndUndo() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("old.txt")), "hi"));
  FileOpEngine ops;
  ops.renamePath(tmp.filePath(QStringLiteral("old.txt")),
                 QStringLiteral("new.txt"));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("new.txt"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("old.txt"))).exists());
  ops.undo();
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("old.txt"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("new.txt"))).exists());
}

void FileOpsTest::copyFileAndUndo() {
  QTemporaryDir src;
  QTemporaryDir dest;
  QVERIFY(src.isValid() && dest.isValid());
  QVERIFY(writeFile(src.filePath(QStringLiteral("note.txt")), "hello"));
  FileOpEngine ops;
  ops.copyPaths({src.filePath(QStringLiteral("note.txt"))}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("note.txt"))).exists());
  QVERIFY(QFileInfo(src.filePath(QStringLiteral("note.txt"))).exists());
  ops.undo();
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(dest.filePath(QStringLiteral("note.txt"))).exists());
  QVERIFY(QFileInfo(src.filePath(QStringLiteral("note.txt"))).exists());
}

void FileOpsTest::copyDirTree() {
  QTemporaryDir src;
  QTemporaryDir dest;
  QVERIFY(src.isValid() && dest.isValid());
  QVERIFY(QDir(src.path()).mkdir(QStringLiteral("tree")));
  QVERIFY(writeFile(src.filePath(QStringLiteral("tree/leaf")), "z"));
  QVERIFY(QDir(src.path()).mkdir(QStringLiteral("tree/sub")));
  QVERIFY(writeFile(src.filePath(QStringLiteral("tree/sub/x")), "q"));
  FileOpEngine ops;
  ops.copyPaths({src.filePath(QStringLiteral("tree"))}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("tree/leaf"))).exists());
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("tree/sub/x"))).exists());
}

void FileOpsTest::copyCollisionSuffix() {
  QTemporaryDir dest;
  QVERIFY(dest.isValid());
  QVERIFY(writeFile(dest.filePath(QStringLiteral("foo.md")), "a"));
  QTemporaryDir src;
  QVERIFY(src.isValid());
  QVERIFY(writeFile(src.filePath(QStringLiteral("foo.md")), "b"));
  FileOpEngine ops;
  ops.copyPaths({src.filePath(QStringLiteral("foo.md"))}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("foo (1).md"))).exists());
}

void FileOpsTest::cutDoesNotDelete() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  FileOpEngine ops;
  ops.setSelection(&sel);
  ops.setDirectoryModel(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  keys.setFileOps(&ops);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("keep.txt"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_X, Qt::NoModifier, QStringLiteral("x")));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("keep.txt"))).exists());
  QCOMPARE(ops.clipboardMode(), QStringLiteral("cut"));
  QCOMPARE(ops.clipboardCount(), 1);
}

void FileOpsTest::copyPasteThenCutPaste() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  QVERIFY(writeFile(a.filePath(QStringLiteral("item")), "1"));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  FileOpEngine ops;
  ops.setSelection(&sel);
  ops.setDirectoryModel(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  keys.setFileOps(&ops);

  model.setPath(a.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("item"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(keys.handleListKey(Qt::Key_Y, Qt::NoModifier, QStringLiteral("y")));
  QCOMPARE(ops.clipboardMode(), QStringLiteral("copy"));
  model.setPath(b.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(keys.handleListKey(Qt::Key_P, Qt::NoModifier, QStringLiteral("p")));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(b.filePath(QStringLiteral("item"))).exists());
  QVERIFY(QFileInfo(a.filePath(QStringLiteral("item"))).exists());

  model.setPath(a.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("item")));
  QVERIFY(keys.handleListKey(Qt::Key_X, Qt::NoModifier, QStringLiteral("x")));
  model.setPath(b.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(keys.handleListKey(Qt::Key_P, Qt::NoModifier, QStringLiteral("p")));
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(a.filePath(QStringLiteral("item"))).exists());
  QVERIFY(QFileInfo(b.filePath(QStringLiteral("item"))).exists());
  QVERIFY(QFileInfo(b.filePath(QStringLiteral("item (1)"))).exists());
  QVERIFY(ops.clipboardMode().isEmpty());
}

void FileOpsTest::moveAndUndo() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  QVERIFY(writeFile(a.filePath(QStringLiteral("mv")), "m"));
  FileOpEngine ops;
  ops.movePaths({a.filePath(QStringLiteral("mv"))}, b.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(a.filePath(QStringLiteral("mv"))).exists());
  QVERIFY(QFileInfo(b.filePath(QStringLiteral("mv"))).exists());
  ops.undo();
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(a.filePath(QStringLiteral("mv"))).exists());
  QVERIFY(!QFileInfo(b.filePath(QStringLiteral("mv"))).exists());
}

void FileOpsTest::duplicateSuffixes() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("dup.txt")), "d"));
  FileOpEngine ops;
  ops.duplicatePaths({tmp.filePath(QStringLiteral("dup.txt"))});
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("dup (1).txt"))).exists());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("dup.txt"))).exists());
}

void FileOpsTest::refuseMkdirOnSlash() {
  FileOpEngine ops;
  ops.makeDir(QStringLiteral("/"), QStringLiteral("synchro-must-not-create"));
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(!QFileInfo(QStringLiteral("/synchro-must-not-create")).exists());
}

void FileOpsTest::refuseCopyOfSlash() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  FileOpEngine ops;
  ops.copyPaths({QStringLiteral("/")}, tmp.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
}

void FileOpsTest::keysYankDeletePaste() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("one"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  FileOpEngine ops;
  ops.setSelection(&sel);
  ops.setDirectoryModel(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  keys.setFileOps(&ops);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(keys.handleListKey(Qt::Key_C, Qt::ControlModifier, QString()));
  QCOMPARE(ops.clipboardMode(), QStringLiteral("copy"));
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::ControlModifier, QString()));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("one (1)"))).exists());
}

void FileOpsTest::keysRenameMkdirUndo() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("oldname"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  FileOpEngine ops;
  ops.setSelection(&sel);
  ops.setDirectoryModel(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  keys.setFileOps(&ops);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("oldname"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QVERIFY(keys.handleListKey(Qt::Key_R, Qt::NoModifier, QStringLiteral("r")));
  QCOMPARE(keys.mode(), QStringLiteral("rename-inline"));
  QCOMPARE(keys.promptText(), QStringLiteral("oldname"));
  keys.setPromptText(QStringLiteral("renamed"));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(waitIdle(ops));
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("renamed"))).exists());

  QVERIFY(keys.handleListKey(Qt::Key_N, Qt::NoModifier, QStringLiteral("n")));
  QCOMPARE(keys.mode(), QStringLiteral("confirm-dialog"));
  keys.setPromptText(QStringLiteral("made"));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("made"))).isDir());

  QVERIFY(keys.handleListKey(Qt::Key_U, Qt::NoModifier, QStringLiteral("u")));
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("made"))).exists());
  QVERIFY(keys.handleListKey(Qt::Key_Z, Qt::ControlModifier, QString()));
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("oldname"))).exists());
}

void FileOpsTest::enterActivatesSelectedFiles() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("f1"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("f2"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("d")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  QSignalSpy spy(&model, &DirectoryModel::fileActivated);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("f1"));
  const int b = findProxy(proxy, QStringLiteral("f2"));
  QVERIFY(a >= 0 && b >= 0);
  sel.click(a);
  sel.ctrlClick(b);
  QCOMPARE(sel.selectedCount(), 2);
  const QString root = model.path();
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QCOMPARE(canon(model.path()), canon(root));
  QCOMPARE(spy.count(), 2);
}

void FileOpsTest::refuseCopyIntoSelf() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("tree")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("tree/leaf")), "z"));
  const QString tree = tmp.filePath(QStringLiteral("tree"));
  FileOpEngine ops;
  ops.copyPaths({tree}, tree);
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("tree/leaf"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("tree/tree"))).exists());
}

void FileOpsTest::refuseMoveIntoSelf() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("tree")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("tree/leaf")), "z"));
  const QString tree = tmp.filePath(QStringLiteral("tree"));
  FileOpEngine ops;
  ops.movePaths({tree}, tree);
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("tree/leaf"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("tree/tree"))).exists());
}

void FileOpsTest::renameSameNameIsNoop() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.txt")), "hi"));
  FileOpEngine ops;
  ops.renamePath(tmp.filePath(QStringLiteral("keep.txt")),
                 QStringLiteral("keep.txt"));
  QVERIFY(waitIdle(ops));
  QVERIFY(ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("keep.txt"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("keep (1).txt"))).exists());
  QVERIFY(!ops.canUndo());
}

void FileOpsTest::moveSameDirIsNoop() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("item")), "1"));
  FileOpEngine ops;
  ops.movePaths({tmp.filePath(QStringLiteral("item"))}, tmp.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("item"))).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("item (1)"))).exists());
}

void FileOpsTest::refuseSpecialFileCopy() {
#ifndef Q_OS_UNIX
  QSKIP("fifo copy test is Unix-only");
#else
  QTemporaryDir src;
  QTemporaryDir dest;
  QVERIFY(src.isValid() && dest.isValid());
  const QString fifo = src.filePath(QStringLiteral("pipe"));
  QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  FileOpEngine ops;
  ops.copyPaths({fifo}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(!QFileInfo(dest.filePath(QStringLiteral("pipe"))).exists());
#endif
}

void FileOpsTest::slashAndPeriodExitVisual() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("aa"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("ab"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("zz"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(0);
  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::ShiftModifier, QStringLiteral("V")));
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QCOMPARE(sel.selectedCount(), 2);
  QVERIFY(sel.visual());
  QVERIFY(keys.handleListKey(Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/")));
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
  QVERIFY(!sel.visual());
  QCOMPARE(sel.selectedCount(), 2);
  keys.setFieldText(QStringLiteral("a"));
  QCOMPARE(proxy.filter(), QStringLiteral("a"));
  QCOMPARE(sel.selectedCount(), 2);
  keys.escape();
  keys.escape();
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  QVERIFY(keys.handleListKey(Qt::Key_V, Qt::ShiftModifier, QStringLiteral("V")));
  QVERIFY(sel.visual());
  QVERIFY(keys.handleListKey(Qt::Key_Period, Qt::NoModifier, QStringLiteral(".")));
  QVERIFY(!sel.visual());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void FileOpsTest::wasdMovesAndShiftLeaps() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 12; ++i)
    QVERIFY(writeFile(tmp.filePath(QStringLiteral("f%1").arg(i, 2, 10,
                                                            QLatin1Char('0')))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(5);
  QCOMPARE(sel.cursor(), 5);

  QVERIFY(keys.handleListKey(Qt::Key_W, Qt::NoModifier, QStringLiteral("w")));
  QCOMPARE(sel.cursor(), 4);
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(sel.cursor(), 5);
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QCOMPARE(sel.cursor(), 4);
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QCOMPARE(sel.cursor(), 5);

  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::ShiftModifier, QStringLiteral("S")));
  QCOMPARE(sel.cursor(), 10);
  QVERIFY(keys.handleListKey(Qt::Key_W, Qt::ShiftModifier, QStringLiteral("W")));
  QCOMPARE(sel.cursor(), 5);

  keys.setGridMode(true);
  keys.setGridStride(4);
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(sel.cursor(), 9);
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QCOMPARE(sel.cursor(), 8);
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::ShiftModifier, QStringLiteral("D")));
  QCOMPARE(sel.cursor(), 11);

  QVERIFY(keys.handleListKey(Qt::Key_X, Qt::NoModifier, QStringLiteral("x")));
  // no FileOpEngine — cut is a no-op; D no longer cuts
  QCOMPARE(sel.cursor(), 11);
}

void FileOpsTest::arrowsWalkHierarchy() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("child/inside.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("sibling.txt"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  SelectionModel sel(&proxy, &model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSelection(&sel);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int child = findProxy(proxy, QStringLiteral("child"));
  QVERIFY(child >= 0);
  proxy.setCurrentIndex(child);

  QVERIFY(keys.handleListKey(Qt::Key_Right, Qt::NoModifier, QString()));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.filePath(QStringLiteral("child"))).canonicalFilePath());

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  const int childAgain = findProxy(proxy, QStringLiteral("child"));
  QVERIFY(childAgain >= 0);
  proxy.setCurrentIndex(childAgain);
  QVERIFY(keys.handleListKey(Qt::Key_E, Qt::NoModifier, QStringLiteral("e")));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.filePath(QStringLiteral("child"))).canonicalFilePath());

  QVERIFY(keys.handleListKey(Qt::Key_Left, Qt::NoModifier, QString()));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return nameAt(proxy, proxy.currentIndex()) == QStringLiteral("child");
      },
      2000));

  QVERIFY(nav.canGoBack());
  QVERIFY(keys.handleListKey(Qt::Key_Left, Qt::AltModifier, QString()));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.filePath(QStringLiteral("child"))).canonicalFilePath());
  QVERIFY(keys.handleListKey(Qt::Key_Right, Qt::AltModifier, QString()));
  QVERIFY(waitListingDone(model));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
}

void FileOpsTest::visibleThumbsFollowSortedProxy() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  auto writeColor = [](const QString &path, QRgb color) {
    QImage img(24, 24, QImage::Format_RGB32);
    img.fill(color);
    return img.save(path, "PNG");
  };
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("zzz.png")), qRgb(9, 9, 9)));
  QVERIFY(writeColor(tmp.filePath(QStringLiteral("aaa.png")),
                     qRgb(240, 10, 10)));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(proxy.rowCount() >= 2);
  QCOMPARE(proxy.data(proxy.index(0, 0), DirectoryModel::NameRole).toString(),
           QStringLiteral("aaa.png"));

  proxy.requestVisibleThumbs(0, 0, 128);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !proxy.data(proxy.index(0, 0), DirectoryModel::ThumbnailRole)
                    .toString()
                    .isEmpty();
      },
      4000));
}

void FileOpsTest::partialCopyKeepsUndo() {
#ifndef Q_OS_UNIX
  QSKIP("partial copy uses a fifo as the failing sibling");
#else
  QTemporaryDir src;
  QTemporaryDir dest;
  QVERIFY(src.isValid() && dest.isValid());
  QVERIFY(writeFile(src.filePath(QStringLiteral("good")), "g"));
  const QString fifo = src.filePath(QStringLiteral("pipe"));
  QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  FileOpEngine ops;
  ops.copyPaths({src.filePath(QStringLiteral("good")), fifo}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("good"))).exists());
  QVERIFY(!QFileInfo(dest.filePath(QStringLiteral("pipe"))).exists());
  QVERIFY(ops.canUndo());
  ops.undo();
  QVERIFY(waitIdle(ops));
  QVERIFY(!QFileInfo(dest.filePath(QStringLiteral("good"))).exists());
#endif
}

void FileOpsTest::undoStackCapsAt32() {
  UndoStack stack;
  for (int i = 0; i < 40; ++i) {
    UndoRecord rec;
    rec.kind = UndoRecord::Kind::Mkdir;
    rec.dests = {QString::number(i)};
    stack.push(rec);
  }
  QCOMPARE(stack.count(), 32);
  QCOMPARE(stack.pop().dests.first(), QStringLiteral("39"));
}

void FileOpsTest::pasteReadsOsClipboardFromOtherEngine() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  QVERIFY(writeFile(a.filePath(QStringLiteral("shared.txt")), "hi"));

  DirectoryModel srcModel;
  FilterProxy srcProxy;
  srcProxy.setDirectoryModel(&srcModel);
  SelectionModel srcSel(&srcProxy, &srcModel);
  FileOpEngine srcOps;
  srcOps.setSelection(&srcSel);
  srcOps.setDirectoryModel(&srcModel);
  srcModel.setPath(a.path());
  QVERIFY(waitListingDone(srcModel));
  srcProxy.setCurrentIndex(findProxy(srcProxy, QStringLiteral("shared.txt")));
  srcOps.copySelection();
  QCOMPARE(srcOps.clipboardMode(), QStringLiteral("copy"));

  DirectoryModel destModel;
  FileOpEngine destOps;
  destOps.setDirectoryModel(&destModel);
  destModel.setPath(b.path());
  QVERIFY(waitListingDone(destModel));
  QVERIFY(destOps.clipboardPaths().isEmpty());
  destOps.paste();
  QVERIFY(waitIdle(destOps));
  QVERIFY2(QFileInfo(b.filePath(QStringLiteral("shared.txt"))).exists(),
           qPrintable(destOps.errorString()));
  QVERIFY(QFileInfo(a.filePath(QStringLiteral("shared.txt"))).exists());
}

void FileOpsTest::pasteOsGnomeCutMoves() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  const QString src = a.filePath(QStringLiteral("take.txt"));
  QVERIFY(writeFile(src, "x"));

  auto *mime = new QMimeData;
  const QUrl url = QUrl::fromLocalFile(src);
  mime->setUrls({url});
  QByteArray gnome = "cut\n";
  gnome += url.toString(QUrl::FullyEncoded).toUtf8();
  gnome += '\n';
  mime->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
  QGuiApplication::clipboard()->setMimeData(mime);

  DirectoryModel destModel;
  FileOpEngine destOps;
  destOps.setDirectoryModel(&destModel);
  destModel.setPath(b.path());
  QVERIFY(waitListingDone(destModel));
  destOps.paste();
  QVERIFY(waitIdle(destOps));
  QVERIFY2(QFileInfo(b.filePath(QStringLiteral("take.txt"))).exists(),
           qPrintable(destOps.errorString()));
  QVERIFY(!QFileInfo(src).exists());
}

void FileOpsTest::pasteReadsBareUriList() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  const QString src = a.filePath(QStringLiteral("only-uri.txt"));
  QVERIFY(writeFile(src, "z"));

  auto *mime = new QMimeData;
  mime->setData(QStringLiteral("x-special/gnome-copied-files"), QByteArray());
  const QString line =
      QUrl::fromLocalFile(src).toString(QUrl::FullyEncoded) +
      QStringLiteral("\r\n");
  mime->setData(QStringLiteral("text/uri-list"), line.toUtf8());
  QGuiApplication::clipboard()->setMimeData(mime);

  DirectoryModel destModel;
  FileOpEngine destOps;
  destOps.setDirectoryModel(&destModel);
  destModel.setPath(b.path());
  QVERIFY(waitListingDone(destModel));
  destOps.paste();
  QVERIFY(waitIdle(destOps));
  QVERIFY2(QFileInfo(b.filePath(QStringLiteral("only-uri.txt"))).exists(),
           qPrintable(destOps.errorString()));
}

void FileOpsTest::copyEmitsProgress() {
  QTemporaryDir src;
  QTemporaryDir dest;
  QVERIFY(src.isValid() && dest.isValid());
  QVERIFY(writeFile(src.filePath(QStringLiteral("blob.bin")),
                    QByteArray(512 * 1024, 'z')));
  FileOpEngine ops;
  QSignalSpy spy(&ops, &FileOpEngine::progressChanged);
  ops.copyPaths({src.filePath(QStringLiteral("blob.bin"))}, dest.path());
  QVERIFY(waitIdle(ops));
  QVERIFY(QFileInfo(dest.filePath(QStringLiteral("blob.bin"))).exists());
  QVERIFY2(spy.count() >= 1, "copy should publish progress");
  QCOMPARE(ops.progress(), 0);
  QVERIFY(ops.progressText().isEmpty());
}

void FileOpsTest::dragMimeHasUriListAndSynchroMark() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString src = tmp.filePath(QStringLiteral("drag.txt"));
  QVERIFY(writeFile(src, "d"));
  FileOpEngine ops;
  const QVariantMap mime = ops.dragMime({src});
  QVERIFY(mime.contains(QStringLiteral("text/uri-list")));
  QVERIFY(mime.contains(QStringLiteral("application/x-synchro-drop")));
  QVERIFY(mime.value(QStringLiteral("text/uri-list")).toString().contains(
      QStringLiteral("file://")));
  const QString gnome =
      mime.value(QStringLiteral("x-special/gnome-copied-files")).toString();
  QVERIFY(gnome.startsWith(QStringLiteral("copy")));
}

void FileOpsTest::pathsFromDropReadsGnomeAndUrls() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString src = tmp.filePath(QStringLiteral("from-drop.txt"));
  QVERIFY(writeFile(src, "z"));
  const QUrl url = QUrl::fromLocalFile(src);
  FileOpEngine ops;
  const QStringList fromUrls =
      ops.pathsFromDrop({QVariant::fromValue(url)}, QString(), QString());
  QCOMPARE(fromUrls, QStringList{src});

  const QString uri = url.toString(QUrl::FullyEncoded) + QStringLiteral("\r\n");
  const QStringList fromList =
      ops.pathsFromDrop({}, uri, QString());
  QCOMPARE(fromList, QStringList{src});

  const QString gnome = QStringLiteral("cut\n") +
                        url.toString(QUrl::FullyEncoded) + QLatin1Char('\n');
  const QStringList fromGnome = ops.pathsFromDrop({}, QString(), gnome);
  QCOMPARE(fromGnome, QStringList{src});
}

void FileOpsTest::dropOnCopyKeepsSource() {
  QTemporaryDir a;
  QTemporaryDir b;
  QVERIFY(a.isValid() && b.isValid());
  const QString src = a.filePath(QStringLiteral("keep.txt"));
  QVERIFY(writeFile(src, "k"));
  FileOpEngine ops;
  ops.dropOn({src}, b.path(), QStringLiteral("copy"));
  QVERIFY(waitIdle(ops));
  QVERIFY2(QFileInfo(b.filePath(QStringLiteral("keep.txt"))).exists(),
           qPrintable(ops.errorString()));
  QVERIFY(QFileInfo(src).exists());
}

void FileOpsTest::dropOnAutoSameDeviceMoves() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("dest")));
  const QString src = tmp.filePath(QStringLiteral("take.txt"));
  QVERIFY(writeFile(src, "m"));
  const QString dest = tmp.filePath(QStringLiteral("dest"));
  QVERIFY(FileOpEngine::sameDevice(src, dest));
  FileOpEngine ops;
  ops.dropOn({src}, dest, QStringLiteral("auto"));
  QVERIFY(waitIdle(ops));
  QVERIFY2(QFileInfo(QDir(dest).filePath(QStringLiteral("take.txt"))).exists(),
           qPrintable(ops.errorString()));
  QVERIFY(!QFileInfo(src).exists());
}

void FileOpsTest::dropOnRejectsVirtualDest() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString src = tmp.filePath(QStringLiteral("nope.txt"));
  QVERIFY(writeFile(src, "x"));
  FileOpEngine ops;
  QVERIFY(!ops.canDropOn(QStringLiteral("volumes://")));
  QVERIFY(!ops.canDropOn(QStringLiteral("trash://")));
  QVERIFY(!ops.canDropOn(QStringLiteral("recent://")));
  QVERIFY(!ops.canAcceptDrop({src}, QStringLiteral("volumes://")));
  ops.dropOn({src}, QStringLiteral("volumes://"), QStringLiteral("copy"));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(QFileInfo(src).exists());
}

void FileOpsTest::dropOnFolderIntoSelfFails() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("tree")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("tree/leaf")), "z"));
  const QString tree = tmp.filePath(QStringLiteral("tree"));
  FileOpEngine ops;
  QVERIFY(!ops.canAcceptDrop({tree}, tree));
  ops.dropOn({tree}, tree, QStringLiteral("move"));
  QVERIFY(!ops.errorString().isEmpty());
  QVERIFY(QFileInfo(tmp.filePath(QStringLiteral("tree/leaf"))).exists());
}

void FileOpsTest::dropOnSameDirMoveIsAlreadyThere() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString src = tmp.filePath(QStringLiteral("stay.txt"));
  QVERIFY(writeFile(src, "s"));
  FileOpEngine ops;
  ops.dropOn({src}, tmp.path(), QStringLiteral("move"));
  QVERIFY(waitIdle(ops));
  QCOMPARE(ops.lastMessage(), QStringLiteral("already there"));
  QVERIFY(QFileInfo(src).exists());
  QVERIFY(!QFileInfo(tmp.filePath(QStringLiteral("stay (1).txt"))).exists());
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  FileOpsTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "file_ops_test.moc"
