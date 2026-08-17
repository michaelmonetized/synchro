#include "DirectoryModel.h"
#include "FilterProxy.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "PortalService.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

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

bool writeFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write("x", 1);
  return true;
}

QString testService(const char *suffix) {
  return QStringLiteral("org.freedesktop.impl.portal.desktop.synchro.test%1.%2")
      .arg(QCoreApplication::applicationPid())
      .arg(QLatin1String(suffix));
}

// Same-connection loopback cannot delay replies; use two bus names.
struct BusPair {
  QDBusConnection server = QDBusConnection::sessionBus();
  QDBusConnection client = QDBusConnection::sessionBus();
  QString serverName;
  QString clientName;
  bool ok = false;
};

BusPair openBusPair(const char *suffix) {
  BusPair pair;
  pair.serverName =
      QStringLiteral("synchro-portal-server-%1-%2")
          .arg(QCoreApplication::applicationPid())
          .arg(QLatin1String(suffix));
  pair.clientName =
      QStringLiteral("synchro-portal-client-%1-%2")
          .arg(QCoreApplication::applicationPid())
          .arg(QLatin1String(suffix));
  pair.server = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                              pair.serverName);
  pair.client = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                              pair.clientName);
  pair.ok = pair.server.isConnected() && pair.client.isConnected();
  return pair;
}

void closeBusPair(BusPair &pair) {
  if (!pair.serverName.isEmpty())
    QDBusConnection::disconnectFromBus(pair.serverName);
  if (!pair.clientName.isEmpty())
    QDBusConnection::disconnectFromBus(pair.clientName);
}

QDBusPendingCall callChooser(const QDBusConnection &bus, const QString &service,
                             const QString &method, const QDBusObjectPath &handle,
                             const QVariantMap &options, const QString &title) {
  QDBusMessage msg = QDBusMessage::createMethodCall(
      service, PortalService::objectPath(),
      QStringLiteral("org.freedesktop.impl.portal.FileChooser"), method);
  msg << QVariant::fromValue(handle) << QString() << QString() << title
      << options;
  return bus.asyncCall(msg, 15000);
}

QDBusPendingCall openFile(const QDBusConnection &bus, const QString &service,
                          const QDBusObjectPath &handle,
                          const QVariantMap &options,
                          const QString &title = QStringLiteral("Open File")) {
  return callChooser(bus, service, QStringLiteral("OpenFile"), handle, options,
                     title);
}

QDBusPendingCall saveFile(const QDBusConnection &bus, const QString &service,
                          const QDBusObjectPath &handle,
                          const QVariantMap &options,
                          const QString &title = QStringLiteral("Save File")) {
  return callChooser(bus, service, QStringLiteral("SaveFile"), handle, options,
                     title);
}

QByteArray folderBytes(const QString &path) {
  QByteArray folder = QFile::encodeName(path);
  folder.append('\0');
  return folder;
}

bool waitFinished(QDBusPendingCall &call, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return call.isFinished(); }, timeoutMs);
}

} // namespace

class PortalDbusTest : public QObject {
  Q_OBJECT

private slots:
  void portalFilterGlobKeepsDirs();
  void delayedReplyAndCloseCancelsOnlyA();
  void openFileWritableDefaultFalse();
  void multipleReturnsMoreThanOneFileUri();
  void saveFileOverwriteEnterConfirmsEscDismisses();
  void saveFileRejectsDotDotAndDirectoryName();
  void enterWhileFilePeekSendsUri();
  void directoryEnterSendsFolderUnderCursor();
  void chooserHasHomeAndRecentChips();
  void questionSearchThenEnterSendsFile();
  void directoryEnterOnFileSendsParent();
  void saveEnterOnFolderSavesInsideIt();
  void bracketsCyclePortalFilters();
  void saveInvalidNameSetsStatus();
};

void PortalDbusTest::portalFilterGlobKeepsDirs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.png"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("skip.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(findProxy(proxy, QStringLiteral("skip.txt")) >= 0);

  proxy.setPortalRules({{0, QStringLiteral("*.png")}});
  QVERIFY(findProxy(proxy, QStringLiteral("keep.png")) >= 0);
  QVERIFY(findProxy(proxy, QStringLiteral("sub")) >= 0);
  QCOMPARE(findProxy(proxy, QStringLiteral("skip.txt")), -1);
}

void PortalDbusTest::delayedReplyAndCloseCancelsOnlyA() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  BusPair bus = openBusPair("close");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("close");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handleA(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/a"));
  const QDBusObjectPath handleB(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/b"));

  QDBusPendingCall pendingA = openFile(bus.client, service, handleA, {});
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handleA.path()) != nullptr; }));
  QVERIFY(!pendingA.isFinished());

  QDBusPendingCall pendingB = openFile(bus.client, service, handleB, {});
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handleB.path()) != nullptr; }));
  QVERIFY(!pendingA.isFinished());
  QVERIFY(!pendingB.isFinished());

  QDBusMessage close = QDBusMessage::createMethodCall(
      service, handleA.path(),
      QStringLiteral("org.freedesktop.impl.portal.Request"),
      QStringLiteral("Close"));
  QDBusPendingCall closeCall = bus.client.asyncCall(close, 5000);
  QVERIFY2(waitFinished(closeCall), "Request.Close did not finish");
  QDBusPendingReply<void> closeReply(closeCall);
  QVERIFY2(closeReply.isValid(), qPrintable(closeReply.error().message()));

  QVERIFY(waitFinished(pendingA));
  QDBusPendingReply<uint, QVariantMap> replyA(pendingA);
  QVERIFY(replyA.isValid());
  QCOMPARE(replyA.argumentAt<0>(), 1u);
  const QStringList urisA =
      replyA.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QVERIFY(urisA.isEmpty());
  QVERIFY(!pendingB.isFinished());
  QVERIFY(portal.session(handleB.path()) != nullptr);

  portal.session(handleB.path())->cancel();
  QVERIFY(waitFinished(pendingB));
  QDBusPendingReply<uint, QVariantMap> replyB(pendingB);
  QVERIFY(replyB.isValid());
  QCOMPARE(replyB.argumentAt<0>(), 1u);
  closeBusPair(bus);
}

void PortalDbusTest::openFileWritableDefaultFalse() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("doc.txt"))));

  BusPair bus = openBusPair("writable");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("writable");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/w"));
  QVariantMap options;
  QByteArray folder = QFile::encodeName(tmp.path());
  folder.append('\0');
  options.insert(QStringLiteral("current_folder"), folder);

  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  QVERIFY(!pending.isFinished());

  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int row = findProxy(*session->filterProxy(), QStringLiteral("doc.txt"));
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  session->accept();

  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QVariantMap results = reply.argumentAt<1>();
  QVERIFY(results.contains(QStringLiteral("writable")));
  QCOMPARE(results.value(QStringLiteral("writable")).toBool(), false);
  const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QVERIFY(uris.first().startsWith(QLatin1String("file://")));
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.filePath(QStringLiteral("doc.txt"))).absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::multipleReturnsMoreThanOneFileUri() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("one.txt"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("two.txt"))));

  BusPair bus = openBusPair("multi");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("multi");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/m"));
  QVariantMap options;
  options.insert(QStringLiteral("multiple"), true);
  QByteArray folder = QFile::encodeName(tmp.path());
  folder.append('\0');
  options.insert(QStringLiteral("current_folder"), folder);

  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));

  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int a = findProxy(*session->filterProxy(), QStringLiteral("one.txt"));
  const int b = findProxy(*session->filterProxy(), QStringLiteral("two.txt"));
  QVERIFY(a >= 0);
  QVERIFY(b >= 0);
  session->selectionModel()->click(a);
  session->selectionModel()->ctrlClick(b);
  session->accept();

  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 2);
  for (const QString &uri : uris) {
    QVERIFY(uri.startsWith(QLatin1String("file://")));
    QVERIFY(QUrl(uri).isLocalFile());
  }
  closeBusPair(bus);
}

void PortalDbusTest::saveFileOverwriteEnterConfirmsEscDismisses() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString existing = tmp.filePath(QStringLiteral("exists.txt"));
  QVERIFY(writeFile(existing));

  BusPair bus = openBusPair("saveow");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("saveow");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handleEsc(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/se"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  options.insert(QStringLiteral("current_name"), QStringLiteral("exists.txt"));

  QDBusPendingCall pendingEsc =
      saveFile(bus.client, service, handleEsc, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handleEsc.path()) != nullptr; }));
  ChooserSession *sessionEsc = portal.session(handleEsc.path());
  QVERIFY(sessionEsc);
  QVERIFY(waitListingDone(*sessionEsc->directoryModel()));
  sessionEsc->accept();
  QVERIFY(!pendingEsc.isFinished());
  QVERIFY(sessionEsc->overwriteOpen());

  QVERIFY(sessionEsc->keyMachine()->handleListKey(Qt::Key_Escape, Qt::NoModifier,
                                                  QString()));
  QVERIFY(sessionEsc->overwriteOpen() == false);
  QVERIFY(!pendingEsc.isFinished());
  QVERIFY(portal.session(handleEsc.path()) != nullptr);

  const QDBusObjectPath handleOk(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/so"));
  QDBusPendingCall pendingOk = saveFile(bus.client, service, handleOk, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handleOk.path()) != nullptr; }));
  ChooserSession *sessionOk = portal.session(handleOk.path());
  QVERIFY(sessionOk);
  QVERIFY(waitListingDone(*sessionOk->directoryModel()));
  sessionOk->accept();
  QVERIFY(!pendingOk.isFinished());
  QVERIFY(sessionOk->overwriteOpen());
  QVERIFY(sessionOk->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                                 QString()));
  QVERIFY(waitFinished(pendingOk));
  QDBusPendingReply<uint, QVariantMap> replyOk(pendingOk);
  QVERIFY(replyOk.isValid());
  QCOMPARE(replyOk.argumentAt<0>(), 0u);
  const QStringList uris =
      replyOk.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QVERIFY(uris.first().startsWith(QLatin1String("file://")));
  QCOMPARE(QUrl(uris.first()).toLocalFile(), QFileInfo(existing).absoluteFilePath());
  QVERIFY(QFileInfo(QUrl(uris.first()).toLocalFile()).isDir() == false);

  QVERIFY(!pendingEsc.isFinished());
  sessionEsc->cancel();
  QVERIFY(waitFinished(pendingEsc));
  closeBusPair(bus);
}

void PortalDbusTest::saveFileRejectsDotDotAndDirectoryName() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("Documents")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("note.txt"))));

  BusPair bus = openBusPair("savedir");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("savedir");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/sd"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  options.insert(QStringLiteral("current_name"), QStringLiteral(".."));

  QDBusPendingCall pending = saveFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int fileRow =
      findProxy(*session->filterProxy(), QStringLiteral("note.txt"));
  QVERIFY(fileRow >= 0);
  session->selectionModel()->setCursor(fileRow);
  session->setSaveName(QStringLiteral(".."));
  session->accept();
  QVERIFY(!pending.isFinished());
  QVERIFY(!session->overwriteOpen());

  session->setSaveName(QStringLiteral("."));
  session->accept();
  QVERIFY(!pending.isFinished());
  QVERIFY(!session->overwriteOpen());

  session->setSaveName(QStringLiteral("Documents"));
  session->accept();
  QVERIFY(!pending.isFinished());
  QVERIFY(!session->overwriteOpen());

  const int dirRow =
      findProxy(*session->filterProxy(), QStringLiteral("Documents"));
  QVERIFY(dirRow >= 0);
  session->selectionModel()->setCursor(dirRow);
  session->setSaveName(QStringLiteral(".."));
  session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                       QString());
  QVERIFY(!pending.isFinished());
  QCOMPARE(session->keyMachine()->statusMessage(),
           QStringLiteral("need a file name"));

  session->cancel();
  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 1u);
  closeBusPair(bus);
}

void PortalDbusTest::enterWhileFilePeekSendsUri() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("shot.txt"))));

  BusPair bus = openBusPair("peekenter");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("peekenter");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/pe"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));

  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int row = findProxy(*session->filterProxy(), QStringLiteral("shot.txt"));
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  auto *host = qobject_cast<HostApi *>(session->hostApi());
  QVERIFY(host);
  QVERIFY(host->openCurrent());
  QVERIFY(host->isOpen());
  QVERIFY(session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                               QString()));
  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.filePath(QStringLiteral("shot.txt"))).absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::directoryEnterSendsFolderUnderCursor() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("vacation")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("vacation/note.txt"))));

  BusPair bus = openBusPair("dirpick");
  QVERIFY(bus.ok);

  PortalService portal;
  const QString service = testService("dirpick");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));

  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/dp"));
  QVariantMap options;
  options.insert(QStringLiteral("directory"), true);
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));

  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int row =
      findProxy(*session->filterProxy(), QStringLiteral("vacation"));
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                       QString());
  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.filePath(QStringLiteral("vacation"))).absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::chooserHasHomeAndRecentChips() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  BusPair bus = openBusPair("chips");
  QVERIFY(bus.ok);
  PortalService portal;
  const QString service = testService("chips");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));
  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/ch"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(session->locationChips());
  QStringList ids;
  for (const QVariant &v : session->locationChips()->chips())
    ids.append(v.toMap().value(QStringLiteral("id")).toString());
  QVERIFY(ids.contains(QStringLiteral("synchro.location.home")));
  QVERIFY(ids.contains(QStringLiteral("synchro.location.recent")));
  QVERIFY(!ids.contains(QStringLiteral("synchro.location.trash")));
  session->cancel();
  QVERIFY(waitFinished(pending));
  closeBusPair(bus);
}

void PortalDbusTest::questionSearchThenEnterSendsFile() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");
  if (QStandardPaths::findExecutable(QStringLiteral("fd")).isEmpty())
    QSKIP("fd not on PATH");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString name = QStringLiteral("synchro-chooser-seek-9f3c.txt");
  QVERIFY(writeFile(tmp.filePath(name)));

  BusPair bus = openBusPair("qsearch");
  QVERIFY(bus.ok);
  PortalService portal;
  const QString service = testService("qsearch");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));
  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/qs"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  session->keyMachine()->setFieldText(QStringLiteral("?synchro-chooser-seek-9f3c"));
  session->keyMachine()->acceptField();
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !session->directoryModel()->listing() &&
               findProxy(*session->filterProxy(), name) >= 0;
      },
      5000));
  const int row = findProxy(*session->filterProxy(), name);
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                       QString());
  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.filePath(name)).absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::directoryEnterOnFileSendsParent() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("inside.txt"))));

  BusPair bus = openBusPair("dirfile");
  QVERIFY(bus.ok);
  PortalService portal;
  const QString service = testService("dirfile");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));
  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/df"));
  QVariantMap options;
  options.insert(QStringLiteral("directory"), true);
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  QDBusPendingCall pending = openFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  const int row =
      findProxy(*session->filterProxy(), QStringLiteral("inside.txt"));
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                       QString());
  QVERIFY(waitFinished(pending));
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.path()).absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::saveEnterOnFolderSavesInsideIt() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("inbox")));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("other.txt"))));

  BusPair bus = openBusPair("savein");
  QVERIFY(bus.ok);
  PortalService portal;
  const QString service = testService("savein");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));
  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/si"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  options.insert(QStringLiteral("current_name"), QStringLiteral("shot.png"));
  QDBusPendingCall pending = saveFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  QSignalSpy tabSpy(session->keyMachine(),
                    &KeyMachine::saveNameFocusRequested);
  QVERIFY(session->keyMachine()->handleListKey(Qt::Key_Tab, Qt::NoModifier,
                                              QString()));
  QCOMPARE(tabSpy.count(), 1);
  const int row = findProxy(*session->filterProxy(), QStringLiteral("inbox"));
  QVERIFY(row >= 0);
  session->selectionModel()->setCursor(row);
  QCOMPARE(QFileInfo(session->destPreview()).fileName(),
           QStringLiteral("shot.png"));
  QVERIFY(session->destPreview().contains(QStringLiteral("inbox")));
  const QString cwd = session->directoryModel()->path();
  session->keyMachine()->handleListKey(Qt::Key_Return, Qt::NoModifier,
                                       QString());
  QVERIFY(waitFinished(pending));
  QCOMPARE(session->directoryModel()->path(), cwd);
  QDBusPendingReply<uint, QVariantMap> reply(pending);
  QVERIFY(reply.isValid());
  QCOMPARE(reply.argumentAt<0>(), 0u);
  const QStringList uris =
      reply.argumentAt<1>().value(QStringLiteral("uris")).toStringList();
  QCOMPARE(uris.size(), 1);
  QCOMPARE(QUrl(uris.first()).toLocalFile(),
           QFileInfo(tmp.filePath(QStringLiteral("inbox/shot.png")))
               .absoluteFilePath());
  closeBusPair(bus);
}

void PortalDbusTest::bracketsCyclePortalFilters() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.png"))));
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("skip.txt"))));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("sub")));

  QVariantList filters;
  filters << QVariant(QVariantList{
      QStringLiteral("PNG"),
      QVariantList{QVariant(QVariantList{0u, QStringLiteral("*.png")})}});
  filters << QVariant(QVariantList{
      QStringLiteral("Text"),
      QVariantList{QVariant(QVariantList{0u, QStringLiteral("*.txt")})}});
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  options.insert(QStringLiteral("filters"), filters);
  ChooserSession session(ChooserSession::Kind::OpenFile,
                         QDBusObjectPath(QStringLiteral("/test/cf")),
                         QStringLiteral("Open"), options, nullptr, nullptr,
                         nullptr, nullptr, nullptr);
  QVERIFY(waitListingDone(*session.directoryModel()));
  QCOMPARE(session.currentFilterIndex(), 0);
  QVERIFY(findProxy(*session.filterProxy(), QStringLiteral("keep.png")) >= 0);
  QCOMPARE(findProxy(*session.filterProxy(), QStringLiteral("skip.txt")), -1);
  QVERIFY(findProxy(*session.filterProxy(), QStringLiteral("sub")) >= 0);

  QVERIFY(session.keyMachine()->handleListKey(Qt::Key_BracketRight,
                                              Qt::NoModifier, QString()));
  QCOMPARE(session.currentFilterIndex(), 1);
  QCOMPARE(session.keyMachine()->statusMessage(), QStringLiteral("Text"));
  QCOMPARE(findProxy(*session.filterProxy(), QStringLiteral("keep.png")), -1);
  QVERIFY(findProxy(*session.filterProxy(), QStringLiteral("skip.txt")) >= 0);

  QVERIFY(session.keyMachine()->handleListKey(Qt::Key_BracketLeft,
                                              Qt::NoModifier, QString()));
  QCOMPARE(session.currentFilterIndex(), 0);
  QCOMPARE(session.keyMachine()->statusMessage(), QStringLiteral("PNG"));
  QVERIFY(findProxy(*session.filterProxy(), QStringLiteral("keep.png")) >= 0);
}

void PortalDbusTest::saveInvalidNameSetsStatus() {
  if (!QDBusConnection::sessionBus().isConnected())
    QSKIP("No session bus");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writeFile(tmp.filePath(QStringLiteral("keep.txt"))));

  BusPair bus = openBusPair("savename");
  QVERIFY(bus.ok);
  PortalService portal;
  const QString service = testService("savename");
  QVERIFY2(portal.start(bus.server, service), qPrintable(portal.lastError()));
  const QDBusObjectPath handle(
      QStringLiteral("/org/freedesktop/portal/desktop/request/test/sn"));
  QVariantMap options;
  options.insert(QStringLiteral("current_folder"), folderBytes(tmp.path()));
  QDBusPendingCall pending = saveFile(bus.client, service, handle, options);
  QVERIFY(QTest::qWaitFor(
      [&] { return portal.session(handle.path()) != nullptr; }));
  ChooserSession *session = portal.session(handle.path());
  QVERIFY(session);
  QVERIFY(waitListingDone(*session->directoryModel()));
  session->setSaveName(QString());
  const int fileRow =
      findProxy(*session->filterProxy(), QStringLiteral("keep.txt"));
  QVERIFY(fileRow >= 0);
  // Park on the folder-less file name field: accept() with empty name.
  session->selectionModel()->setCursor(fileRow);
  session->setSaveName(QString());
  session->accept();
  QVERIFY(!pending.isFinished());
  QCOMPARE(session->keyMachine()->statusMessage(),
           QStringLiteral("need a file name"));
  session->cancel();
  QVERIFY(waitFinished(pending));
  closeBusPair(bus);
}

QTEST_MAIN(PortalDbusTest)
#include "portal_dbus_test.moc"
