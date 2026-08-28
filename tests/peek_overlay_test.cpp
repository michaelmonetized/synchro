#include "Config.h"
#include "DirectoryModel.h"
#include "FileCatalog.h"
#include "FilterProxy.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "KeyMachine.h"
#include "LocationChips.h"
#include "MimeMap.h"
#include "NavStack.h"
#include "OmaflowBridge.h"
#include "SearchModel.h"
#include "SearchService.h"
#include "SelectionModel.h"
#include "ThumbImageProvider.h"
#include "XdgOpen.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>
#include <sqlite3.h>

Q_IMPORT_QML_PLUGIN(Synchro_ThemePlugin)
Q_IMPORT_QML_PLUGIN(Synchro_HandlerPlugin)

namespace {

class ScopedEnvironment {
public:
  explicit ScopedEnvironment(const QByteArray &name)
      : m_name(name), m_hadValue(qEnvironmentVariableIsSet(name.constData())),
        m_value(qgetenv(name.constData())) {}
  ~ScopedEnvironment() {
    if (m_hadValue)
      qputenv(m_name.constData(), m_value);
    else
      qunsetenv(m_name.constData());
  }

private:
  QByteArray m_name;
  bool m_hadValue = false;
  QByteArray m_value;
};

bool waitListingDone(DirectoryModel &model, int timeoutMs = 5000) {
  return QTest::qWaitFor([&] { return !model.listing(); }, timeoutMs);
}

void collectVisual(QQuickItem *root, QVector<QQuickItem *> *out) {
  if (!root)
    return;
  out->append(root);
  const auto kids = root->childItems();
  for (QQuickItem *child : kids)
    collectVisual(child, out);
}

QVector<QQuickItem *> visualNamed(QQuickItem *root, const QString &name) {
  QVector<QQuickItem *> all;
  collectVisual(root, &all);
  QVector<QQuickItem *> hit;
  for (QQuickItem *item : all) {
    if (item->objectName() == name)
      hit.append(item);
  }
  return hit;
}

int findProxy(const FilterProxy &proxy, const QString &name) {
  for (int i = 0; i < proxy.rowCount(); ++i) {
    if (proxy.data(proxy.index(i, 0), DirectoryModel::NameRole).toString() ==
        name)
      return i;
  }
  return -1;
}

QString nameAt(const FilterProxy &proxy, int row) {
  return proxy.data(proxy.index(row, 0), DirectoryModel::NameRole).toString();
}

// 1x1 PNG so Image {} has a real decode target.
bool writePng(const QString &path) {
  static const unsigned char kPng[] = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
      0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c, 0x02, 0x00, 0x00, 0x00,
      0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00,
      0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66, 0x00, 0x00, 0x00, 0x00,
      0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write(reinterpret_cast<const char *>(kPng), sizeof(kPng)) ==
         qint64(sizeof(kPng));
}

} // namespace

class FakeOmaflowBridge : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool installed READ installed CONSTANT)
  Q_PROPERTY(QString version READ version CONSTANT)
  Q_PROPERTY(QVariantList rules READ rules NOTIFY stateChanged)
  Q_PROPERTY(QVariantList activity READ activity NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap staging READ staging NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
  Q_PROPERTY(QString operationRule READ operationRule NOTIFY operationChanged)
  Q_PROPERTY(QString operationKind READ operationKind NOTIFY operationChanged)
  Q_PROPERTY(
      QString operationOutput READ operationOutput NOTIFY operationChanged)

public:
  explicit FakeOmaflowBridge(QObject *parent = nullptr) : QObject(parent) {
    const QVariantMap installedRule{
        {QStringLiteral("id"), QStringLiteral("existing-flow")},
        {QStringLiteral("name"), QStringLiteral("Existing flow")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("trigger"),
         QVariantMap{{QStringLiteral("type"), QStringLiteral("manual")}}},
        {QStringLiteral("actions"),
         QVariantList{QVariantMap{
             {QStringLiteral("type"), QStringLiteral("notify")},
             {QStringLiteral("message"), QStringLiteral("existing")}}}}};
    m_rules = {installedRule};

    const QVariantMap draftRule{
        {QStringLiteral("id"), QStringLiteral("presentation-draft")},
        {QStringLiteral("name"), QStringLiteral("Presentation draft")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("trigger"),
         QVariantMap{
             {QStringLiteral("type"), QStringLiteral("monitor-connected")},
             {QStringLiteral("match"),
              QVariantMap{{QStringLiteral("description"),
                           QStringLiteral("projector")}}}}},
        {QStringLiteral("conditions"),
         QVariantList{QVariantMap{
             {QStringLiteral("type"), QStringLiteral("weekday")},
             {QStringLiteral("days"),
              QVariantList{QStringLiteral("mon"), QStringLiteral("tue")}}}}},
        {QStringLiteral("actions"),
         QVariantList{
             QVariantMap{{QStringLiteral("type"), QStringLiteral("dnd")},
                         {QStringLiteral("state"), QStringLiteral("on")}},
             QVariantMap{{QStringLiteral("type"), QStringLiteral("workspace")},
                         {QStringLiteral("number"), 4}}}}};
    m_staging = {{QStringLiteral("status"), QStringLiteral("ready")},
                 {QStringLiteral("agent"), QStringLiteral("codex")},
                 {QStringLiteral("request"),
                  QStringLiteral("Prepare the desktop for a projector")},
                 {QStringLiteral("warnings"),
                  QVariantList{QStringLiteral("projector is not connected")}},
                 {QStringLiteral("rule"), draftRule}};
  }

  bool installed() const { return true; }
  QString version() const { return QStringLiteral("test"); }
  QVariantList rules() const { return m_rules; }
  QVariantList activity() const { return {}; }
  QVariantMap staging() const { return m_staging; }
  bool busy() const { return false; }
  QString operationRule() const { return {}; }
  QString operationKind() const { return {}; }
  QString operationOutput() const { return {}; }
  int accepts() const { return m_accepts; }
  QString authoredRequest() const { return m_authoredRequest; }

  Q_INVOKABLE void setActive(bool) {}
  Q_INVOKABLE void refresh() {}
  Q_INVOKABLE bool dryRun(const QString &) { return true; }
  Q_INVOKABLE bool run(const QString &) { return true; }
  Q_INVOKABLE bool author(const QString &request) {
    m_authoredRequest = request;
    return true;
  }
  Q_INVOKABLE bool acceptStage() {
    ++m_accepts;
    return true;
  }
  Q_INVOKABLE bool rejectStage() { return true; }
  Q_INVOKABLE void cancel() {}

signals:
  void stateChanged();
  void operationChanged();

private:
  QVariantList m_rules;
  QVariantMap m_staging;
  int m_accepts = 0;
  QString m_authoredRequest;
};

class PeekOverlayTest : public QObject {
  Q_OBJECT

private slots:
  void tooltipRemapsAfterDockMoves();
  void omaflowGraphAdaptsToDockShape();
  void omaflowStepInspectorEscapesGraphClip();
  void omaflowPanelReviewsStagedRule();
  void omaflowRenderLabGraphsLoad();
  void shiftSpaceOpensImagePeekAndJSteps();
  void ctrlKClosesOpenWithOverlay();
  void textAndMarkdownHandlersResolve();
  void videoHandlerAndWebpRaster();
  void spaceOnFolderDrillsAndEscReturns();
  void folderPeekWasdUsesOwnStride();
  void folderPeekFileBackKeepsListingAndScroll();
  void folderPeekSpaceOnFileReturnsListing();
  void folderPeekStepsPastUnpreviewableFile();
  void filePeekAdHopsAndPreviewScrolls();
  void sqliteAndDuckdbHandlersResolve();
  void rootFilePeekShowsIndexAndQCloses();
  void gridPeekIndexUsesThumbs();
  void emptyFolderShowsHintInRootAndPeek();
  void gridPeekIndexHidesFolders();
  void peekEnterCommitsFileAndFolder();
  void doLayerVerbsKeysAndCopyAs();
  void doLayerOffersOnlyMatchingOmaflows();
  void doLayerOverlaySplitChrome();
  void doLayerShowsFilePreviewAndFolderGrid();
  void volumesListingChromeUrls();
  void volumesListingChromeVisible();
  void pathBarTabsSitAboveCommandField();
  void fileGridCellsFillWidth();
  void gridWasdTracksRenderedGeometryAcrossRelayout();
  void locationCloseConsumesClick();
  void searchGridCellsMatchRows();
  void searchGridDropsFolderTiles();
  void searchGridVirtualizesGroups();
  void fileGridFollowsProxySort();
  void contextualPanelRelevanceFollowsSelection();
  void panelLookUsesOptInQuickAppsAndAsyncReads();
  void standaloneLookFollowsSelectionAndMigrates();
  void findInFilePastDefaultWindow();
  void textPeekFindCyclesHits();
  void textPeekFindJumpsPastWindow();
  void textPeekFindKeepsNewlines();
  void textPeekFindKeepsSyntaxColors();
  void contentSearchPeekOpensFind();
};

void PeekOverlayTest::tooltipRemapsAfterDockMoves() {
  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import Synchro.Theme 1.0 as Synchro

Window {
    id: window
    width: 640
    height: 480
    visible: true
    property bool showTip: false

    Item {
        id: dock
        objectName: "movingDock"
        x: 24
        y: 32
        width: 300
        height: 180

        Item {
            id: anchor
            objectName: "tipAnchor"
            x: 110
            y: 20
            width: 40
            height: 24
        }
    }

    Synchro.ToolTip {
        objectName: "movingTooltip"
        anchorItem: anchor
        shown: window.showTip
        label: "Dock action"
    }
}
)QML",
                    QUrl(QStringLiteral("inline:tooltip-remap.qml")));

  QVERIFY(QTest::qWaitFor(
      [&] { return component.status() != QQmlComponent::Loading; }, 3000));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));
  std::unique_ptr<QObject> instance(component.create());
  QVERIFY2(instance, qPrintable(component.errorString()));
  auto *window = qobject_cast<QQuickWindow *>(instance.get());
  QVERIFY(window);
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *dock = window->findChild<QQuickItem *>(QStringLiteral("movingDock"));
  auto *anchor = window->findChild<QQuickItem *>(QStringLiteral("tipAnchor"));
  auto *tooltip =
      window->findChild<QQuickItem *>(QStringLiteral("movingTooltip"));
  QVERIFY(dock);
  QVERIFY(anchor);
  QVERIFY(tooltip);

  // Simulate moving the whole app panel to another dock edge before the
  // pointer enters a button and reveals its tooltip.
  dock->setX(330);
  dock->setY(250);
  QVERIFY(window->setProperty("showTip", true));
  QVERIFY(QTest::qWaitFor([&] { return tooltip->isVisible(); }, 1000));

  const QPointF anchorScene = anchor->mapToScene(QPointF(0, 0));
  const QPointF tooltipScene = tooltip->mapToScene(QPointF(0, 0));
  const qreal anchorCenter = anchorScene.x() + anchor->width() / 2.0;
  const qreal tooltipCenter = tooltipScene.x() + tooltip->width() / 2.0;
  QVERIFY2(qAbs(anchorCenter - tooltipCenter) < 1.0,
           qPrintable(QStringLiteral("tooltip center %1, anchor center %2")
                          .arg(tooltipCenter)
                          .arg(anchorCenter)));
  QVERIFY(tooltipScene.y() > anchorScene.y());
}

void PeekOverlayTest::omaflowGraphAdaptsToDockShape() {
  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  const QString graphPath =
      QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
          .filePath(QStringLiteral("synchro.panel.omaflow/FlowGraph.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(graphPath));
  QVERIFY(QTest::qWaitFor(
      [&] { return component.status() != QQmlComponent::Loading; }, 3000));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));

  std::unique_ptr<QObject> instance(component.create());
  QVERIFY2(instance, qPrintable(component.errorString()));
  auto *graph = qobject_cast<QQuickItem *>(instance.get());
  QVERIFY(graph);
  graph->setWidth(900);

  const QVariantList nodes{
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("start")},
          {QStringLiteral("kind"), QStringLiteral("trigger")},
          {QStringLiteral("label"), QStringLiteral("Monitor connected")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("route")},
                  {QStringLiteral("kind"), QStringLiteral("condition")},
                  {QStringLiteral("label"), QStringLiteral("Weekday")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("work")},
                  {QStringLiteral("kind"), QStringLiteral("action")},
                  {QStringLiteral("label"), QStringLiteral("DND")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("weekend")},
                  {QStringLiteral("kind"), QStringLiteral("action")},
                  {QStringLiteral("label"), QStringLiteral("Notify")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("done")},
                  {QStringLiteral("kind"), QStringLiteral("terminal")},
                  {QStringLiteral("tone"), QStringLiteral("success")},
                  {QStringLiteral("label"), QStringLiteral("Done")}}};
  const QVariantList edges{
      QVariantMap{{QStringLiteral("from"), QStringLiteral("start")},
                  {QStringLiteral("to"), QStringLiteral("route")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("route")},
                  {QStringLiteral("to"), QStringLiteral("work")},
                  {QStringLiteral("label"), QStringLiteral("YES")},
                  {QStringLiteral("tone"), QStringLiteral("pass")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("route")},
                  {QStringLiteral("to"), QStringLiteral("weekend")},
                  {QStringLiteral("label"), QStringLiteral("NO")},
                  {QStringLiteral("tone"), QStringLiteral("fail")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("work")},
                  {QStringLiteral("to"), QStringLiteral("done")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("weekend")},
                  {QStringLiteral("to"), QStringLiteral("done")}}};
  const QVariantMap graphSpec{{QStringLiteral("nodes"), nodes},
                              {QStringLiteral("edges"), edges}};
  QVERIFY(graph->setProperty("graphSpec", graphSpec));
  QVERIFY(graph->setProperty("horizontalLayout", true));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return visualNamed(graph, QStringLiteral("omaflowGraphNode")).size() ==
               nodes.size();
      },
      1000));

  auto cards = visualNamed(graph, QStringLiteral("omaflowGraphNode"));
  QHash<QString, QQuickItem *> byId;
  for (QQuickItem *card : cards)
    byId.insert(card->property("modelData")
                    .toMap()
                    .value(QStringLiteral("id"))
                    .toString(),
                card);
  QVERIFY(byId.contains(QStringLiteral("work")));
  QVERIFY(byId.contains(QStringLiteral("weekend")));
  QCOMPARE(byId.value(QStringLiteral("work"))->x(),
           byId.value(QStringLiteral("weekend"))->x());
  QVERIFY(byId.value(QStringLiteral("work"))->y() !=
          byId.value(QStringLiteral("weekend"))->y());
  QCOMPARE(graph->property("nodeCount").toInt(), nodes.size());
  QVERIFY(graph->property("graphWidth").toReal() > graph->width());
  const qreal horizontalHeight = graph->implicitHeight();

  QVERIFY(graph->setProperty("horizontalLayout", false));
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto vertical = visualNamed(graph, QStringLiteral("omaflowGraphNode"));
        if (vertical.size() != nodes.size())
          return false;
        QHash<QString, QQuickItem *> verticalById;
        for (QQuickItem *card : vertical)
          verticalById.insert(card->property("modelData")
                                  .toMap()
                                  .value(QStringLiteral("id"))
                                  .toString(),
                              card);
        return verticalById.contains(QStringLiteral("work")) &&
               verticalById.contains(QStringLiteral("weekend")) &&
               verticalById.value(QStringLiteral("work"))->y() ==
                   verticalById.value(QStringLiteral("weekend"))->y() &&
               verticalById.value(QStringLiteral("work"))->x() !=
                   verticalById.value(QStringLiteral("weekend"))->x();
      },
      1000));
  QCOMPARE(graph->property("graphWidth").toReal(), graph->width());
  QVERIFY(graph->implicitHeight() > horizontalHeight);

  const auto labels =
      visualNamed(graph, QStringLiteral("omaflowGraphEdgeLabel"));
  int visibleLabels = 0;
  for (QQuickItem *label : labels)
    visibleLabels += label->isVisible() ? 1 : 0;
  QCOMPARE(visibleLabels, 2);
}

void PeekOverlayTest::omaflowStepInspectorEscapesGraphClip() {
  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  const QString graphPath =
      QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
          .filePath(QStringLiteral("synchro.panel.omaflow/FlowGraph.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(graphPath));
  QVERIFY(QTest::qWaitFor(
      [&] { return component.status() != QQmlComponent::Loading; }, 3000));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));

  std::unique_ptr<QObject> instance(component.create());
  QVERIFY2(instance, qPrintable(component.errorString()));
  auto *graph = qobject_cast<QQuickItem *>(instance.get());
  QVERIFY(graph);

  QQuickWindow window;
  window.resize(720, 480);
  graph->setParentItem(window.contentItem());
  graph->setX(24);
  graph->setY(42);
  graph->setWidth(660);
  graph->setHeight(360);

  const QVariantList nodes{
      QVariantMap{{QStringLiteral("id"), QStringLiteral("start")},
                  {QStringLiteral("kind"), QStringLiteral("trigger")},
                  {QStringLiteral("label"), QStringLiteral("New file")}},
      QVariantMap{
          {QStringLiteral("id"), QStringLiteral("route")},
          {QStringLiteral("kind"), QStringLiteral("condition")},
          {QStringLiteral("label"), QStringLiteral("Image family?")},
          {QStringLiteral("detail"), QStringLiteral("Route by detected kind")},
          {QStringLiteral("parameters"),
           QVariantMap{{QStringLiteral("match"), QStringLiteral("image/*")},
                       {QStringLiteral("case_sensitive"), false},
                       {QStringLiteral("limit"), 24}}}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("yes")},
                  {QStringLiteral("label"), QStringLiteral("Optimize")}},
      QVariantMap{{QStringLiteral("id"), QStringLiteral("no")},
                  {QStringLiteral("label"), QStringLiteral("Retain")}}};
  const QVariantList edges{
      QVariantMap{{QStringLiteral("from"), QStringLiteral("start")},
                  {QStringLiteral("to"), QStringLiteral("route")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("route")},
                  {QStringLiteral("to"), QStringLiteral("yes")},
                  {QStringLiteral("label"), QStringLiteral("YES")}},
      QVariantMap{{QStringLiteral("from"), QStringLiteral("route")},
                  {QStringLiteral("to"), QStringLiteral("no")},
                  {QStringLiteral("label"), QStringLiteral("ARCHIVE")}}};
  QVERIFY(graph->setProperty("graphSpec",
                             QVariantMap{{QStringLiteral("nodes"), nodes},
                                         {QStringLiteral("edges"), edges}}));
  QVERIFY(graph->setProperty("horizontalLayout", true));

  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return visualNamed(graph, QStringLiteral("omaflowGraphNode")).size() ==
               nodes.size();
      },
      1000));

  QQuickItem *routeCard = nullptr;
  const auto cards = visualNamed(graph, QStringLiteral("omaflowGraphNode"));
  for (QQuickItem *card : cards) {
    if (card->property("modelData").toMap().value(QStringLiteral("id")) ==
        QStringLiteral("route")) {
      routeCard = card;
      break;
    }
  }
  QVERIFY(routeCard);

  auto *inspector =
      graph->findChild<QQuickItem *>(QStringLiteral("omaflowNodeInspector"));
  QVERIFY(inspector);
  QTest::mouseMove(&window, QPoint(4, 4));
  const QPoint routeCenter =
      routeCard
          ->mapToScene(
              QPointF(routeCard->width() / 2.0, routeCard->height() / 2.0))
          .toPoint();
  QTest::mouseMove(&window, routeCenter);
  QVERIFY(QTest::qWaitFor([&] { return inspector->isVisible(); }, 1000));
  QVERIFY(!graph->property("inspectorPinned").toBool());

  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, routeCenter);
  QVERIFY(QTest::qWaitFor(
      [&] { return graph->property("inspectorPinned").toBool(); }, 1000));
  QCOMPARE(inspector->parentItem(), window.contentItem());
  QVERIFY(inspector->x() >= 0);
  QVERIFY(inspector->y() >= 0);
  QVERIFY(inspector->x() + inspector->width() <= window.width());
  QVERIFY(inspector->y() + inspector->height() <= window.height());
  QCOMPARE(inspector->property("parameterRows").toList().size(), 3);
  QCOMPARE(
      visualNamed(inspector, QStringLiteral("omaflowInspectorRoute")).size(),
      3);
  const auto routePills =
      visualNamed(inspector, QStringLiteral("omaflowInspectorRoutePill"));
  QCOMPARE(routePills.size(), 3);
  for (QQuickItem *pill : routePills)
    QVERIFY(pill->property("contentFits").toBool());

  QVERIFY(QMetaObject::invokeMethod(graph, "dismissInspector"));
  QVERIFY(QTest::qWaitFor([&] { return !inspector->isVisible(); }, 1000));
}

void PeekOverlayTest::omaflowPanelReviewsStagedRule() {
  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  FakeOmaflowBridge bridge;
  engine.rootContext()->setContextProperty(QStringLiteral("omaflow"), &bridge);
  const QString panelPath =
      QDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR))
          .filePath(QStringLiteral("synchro.panel.omaflow/Panel.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(panelPath));
  QVERIFY(QTest::qWaitFor(
      [&] { return component.status() != QQmlComponent::Loading; }, 3000));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));

  std::unique_ptr<QObject> instance(component.create());
  QVERIFY2(instance, qPrintable(component.errorString()));
  auto *panel = qobject_cast<QQuickItem *>(instance.get());
  QVERIFY(panel);
  panel->setWidth(900);
  panel->setHeight(560);

  QQuickWindow window;
  window.resize(900, 560);
  panel->setParentItem(window.contentItem());
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return panel->property("reviewingDraft").toBool() &&
               panel->property("displayRule")
                       .toMap()
                       .value(QStringLiteral("id")) ==
                   QStringLiteral("presentation-draft");
      },
      1000));

  auto *graph =
      panel->findChild<QQuickItem *>(QStringLiteral("omaflowFlowGraph"));
  auto *install =
      panel->findChild<QQuickItem *>(QStringLiteral("omaflowInstallDraft"));
  auto *discard =
      panel->findChild<QQuickItem *>(QStringLiteral("omaflowDiscardDraft"));
  QVERIFY(graph);
  QVERIFY(install);
  QVERIFY(discard);
  QVERIFY(graph->isVisible());
  QVERIFY(install->isVisible());
  QVERIFY(discard->isVisible());
  QCOMPARE(graph->property("nodeCount").toInt(), 6);

  QVERIFY(QMetaObject::invokeMethod(panel, "installDraft"));
  QVERIFY(panel->property("stageArmed").toBool());
  QCOMPARE(bridge.accepts(), 0);
  QVERIFY(QMetaObject::invokeMethod(panel, "installDraft"));
  QCOMPARE(bridge.accepts(), 1);

  QVERIFY(QMetaObject::invokeMethod(panel, "openAuthor"));
  auto *composer =
      panel->findChild<QQuickItem *>(QStringLiteral("omaflowAuthorComposer"));
  QVERIFY(composer);
  QVERIFY(QTest::qWaitFor([&] { return composer->isVisible(); }, 1000));
  QVERIFY(panel->setProperty("authorPrompt",
                             QStringLiteral("Draft a quieter flow")));
  QVERIFY(QMetaObject::invokeMethod(panel, "submitAuthor"));
  QCOMPARE(bridge.authoredRequest(), QStringLiteral("Draft a quieter flow"));
  QVERIFY(!panel->property("authoring").toBool());
}

void PeekOverlayTest::omaflowRenderLabGraphsLoad() {
  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  const QDir handlers(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  QQmlComponent component(
      &engine, QUrl::fromLocalFile(handlers.filePath(
                   QStringLiteral("synchro.panel.omaflow/FlowGraph.qml"))));
  QVERIFY(QTest::qWaitFor(
      [&] { return component.status() != QQmlComponent::Loading; }, 3000));
  QVERIFY2(component.status() == QQmlComponent::Ready,
           qPrintable(component.errorString()));

  const QDir fixtureDir(QDir(handlers.absolutePath())
                            .filePath(QStringLiteral(
                                "../tests/fixtures/omaflow-lab/config/rules")));
  const QStringList fixtures =
      fixtureDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
  QCOMPARE(fixtures.size(), 4);

  for (const QString &name : fixtures) {
    QFile file(fixtureDir.filePath(name));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    QVERIFY2(error.error == QJsonParseError::NoError,
             qPrintable(name + QStringLiteral(": ") + error.errorString()));
    const QJsonObject visual =
        doc.object().value(QStringLiteral("visualGraph")).toObject();
    QVERIFY(!visual.isEmpty());
    const int expectedNodes =
        visual.value(QStringLiteral("nodes")).toArray().size();
    QVERIFY(expectedNodes >= 8);

    std::unique_ptr<QObject> instance(component.create());
    QVERIFY2(instance, qPrintable(component.errorString()));
    auto *graph = qobject_cast<QQuickItem *>(instance.get());
    QVERIFY(graph);
    graph->setWidth(540);
    QVERIFY(graph->setProperty("graphSpec", visual.toVariantMap()));
    QVERIFY(graph->setProperty("horizontalLayout", false));
    QVERIFY(QTest::qWaitFor(
        [&] {
          return visualNamed(graph, QStringLiteral("omaflowGraphNode"))
                     .size() == expectedNodes;
        },
        1000));
    QCOMPARE(graph->property("nodeCount").toInt(), expectedNodes);
    QVERIFY(graph->property("graphWidth").toReal() >= graph->width());
    QVERIFY(graph->implicitHeight() > 0);

    QVERIFY(graph->setProperty("horizontalLayout", true));
    QCOMPARE(graph->property("nodeCount").toInt(), expectedNodes);
    QVERIFY(graph->property("graphWidth").toReal() >= graph->width());

    if (name == QStringLiteral("network-retry-lab.json")) {
      const QVariantList laidOutEdges = graph->property("layoutData")
                                            .toMap()
                                            .value(QStringLiteral("edges"))
                                            .toList();
      bool foundBackEdge = false;
      for (const QVariant &edge : laidOutEdges)
        foundBackEdge = foundBackEdge ||
                        edge.toMap().value(QStringLiteral("backEdge")).toBool();
      QVERIFY(foundBackEdge);
    }
  }
}

void PeekOverlayTest::shiftSpaceOpensImagePeekAndJSteps() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.image")));
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  const int b = findProxy(proxy, QStringLiteral("b.png"));
  QVERIFY(a >= 0);
  QVERIFY(b >= 0);
  const int first = qMin(a, b);
  const int second = qMax(a, b);
  proxy.setCurrentIndex(first);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

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

  const auto tooltips =
      visualNamed(window->contentItem(), QStringLiteral("tooltipOverlay"));
  QVERIFY(!tooltips.isEmpty());
  for (QQuickItem *tooltip : tooltips)
    QCOMPARE(tooltip->parentItem(), window->contentItem());

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  auto *overlay =
      window->findChild<QQuickItem *>(QStringLiteral("peekOverlay"));
  QVERIFY(overlay);

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));

  QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
  QVERIFY(QTest::qWaitFor([&] { return hostApi.isOpen(); }, 2000));
  QVERIFY(overlay->isVisible());
  auto *frame = window->findChild<QQuickItem *>(QStringLiteral("peekPanel"));
  auto *keyline =
      window->findChild<QQuickItem *>(QStringLiteral("peekModalKeyline"));
  auto *blocker =
      window->findChild<QQuickItem *>(QStringLiteral("peekModalBlocker"));
  QVERIFY(frame);
  QVERIFY(keyline);
  QVERIFY(blocker);
  QVERIFY(blocker->isVisible());
  QCOMPARE(keyline->width(), frame->width());
  QCOMPARE(keyline->height(), frame->height());
  QVERIFY(frame->x() > 0);
  QVERIFY(frame->x() + frame->width() < overlay->width());
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 2000));
  auto *fileBackground = window->findChild<QQuickItem *>(
      QStringLiteral("peekFileSurfaceBackground"));
  QVERIFY(fileBackground);
  QVERIFY(fileBackground->isVisible());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !visualNamed(window->contentItem(),
                            QStringLiteral("peekIndexSelection"))
                    .isEmpty();
      },
      1000));
  QVERIFY2(list->hasActiveFocus(),
           "preview must not steal list focus; KeyMachine owns peek keys");

  QTest::keyClick(window, Qt::Key_Space);
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.isOpen(); }, 2000));
  QVERIFY(!overlay->isVisible());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));

  QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
  QVERIFY(QTest::qWaitFor([&] { return hostApi.isOpen(); }, 2000));
  QVERIFY(list->hasActiveFocus());
  QTest::keyClick(window, Qt::Key_J);
  QVERIFY(
      QTest::qWaitFor([&] { return proxy.currentIndex() == second; }, 2000));
  QCOMPARE(nameAt(proxy, proxy.currentIndex()), nameAt(proxy, second));
  QVERIFY(hostApi.isOpen());
}

void PeekOverlayTest::ctrlKClosesOpenWithOverlay() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.action.open-with")));
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("a.png"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

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
  auto *overlay =
      window->findChild<QQuickItem *>(QStringLiteral("actionOverlay"));
  QVERIFY(overlay);

  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::ControlModifier, QString()));
  QVERIFY2(QTest::qWaitFor([&] { return hostApi.actionOpen(); }, 2000),
           qPrintable(hostApi.lastError()));
  QVERIFY(overlay->isVisible());
  QVERIFY(keys.actionOpen());
  auto *verbs = window->findChild<QQuickItem *>(QStringLiteral("doVerbList"));
  QVERIFY(verbs);
  auto *caption = window->findChild<QQuickItem *>(QStringLiteral("doCaption"));
  QVERIFY(caption);
  QVERIFY(caption->property("text").toString().startsWith(
      QStringLiteral("Actions ·")));

  keys.focusFilter();
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.actionOpen(); }, 2000));
  QVERIFY(!overlay->isVisible());
  QCOMPARE(keys.mode(), QStringLiteral("field-filter"));
}

void PeekOverlayTest::textAndMarkdownHandlersResolve() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# hi\n");
  }
  {
    QFile f(tmp.filePath(QStringLiteral("plain.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello text\n");
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.text")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.markdown")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.pdf")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.parquet")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.archive")));

  Manifest::Item md;
  md.path = tmp.filePath(QStringLiteral("notes.md"));
  md.mime = QStringLiteral("text/markdown");
  const auto mdHits = registry.resolve(QStringLiteral("preview"), {md});
  QVERIFY(!mdHits.isEmpty());
  QCOMPARE(mdHits.constFirst().id, QStringLiteral("synchro.preview.markdown"));

  Manifest::Item txt;
  txt.path = tmp.filePath(QStringLiteral("plain.txt"));
  txt.mime = QStringLiteral("text/plain");
  const auto txtHits = registry.resolve(QStringLiteral("preview"), {txt});
  QVERIFY(!txtHits.isEmpty());
  QCOMPARE(txtHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item yaml;
  yaml.path = tmp.filePath(QStringLiteral("app.yaml"));
  yaml.mime = QStringLiteral("application/yaml");
  const auto yamlHits = registry.resolve(QStringLiteral("preview"), {yaml});
  QVERIFY(!yamlHits.isEmpty());
  QCOMPARE(yamlHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item sql;
  sql.path = tmp.filePath(QStringLiteral("schema.sql"));
  sql.mime = QStringLiteral("application/sql");
  const auto sqlHits = registry.resolve(QStringLiteral("preview"), {sql});
  QVERIFY(!sqlHits.isEmpty());
  QCOMPARE(sqlHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item docker;
  docker.path = tmp.filePath(QStringLiteral("Dockerfile"));
  docker.mime = QStringLiteral("application/octet-stream");
  const auto dockerHits = registry.resolve(QStringLiteral("preview"), {docker});
  QVERIFY(!dockerHits.isEmpty());
  QCOMPARE(dockerHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  Manifest::Item ts;
  ts.path = tmp.filePath(QStringLiteral("app.ts"));
  ts.mime = QStringLiteral("video/mp2t");
  const auto tsHits = registry.resolve(QStringLiteral("preview"), {ts});
  QVERIFY(!tsHits.isEmpty());
  QCOMPARE(tsHits.constFirst().id, QStringLiteral("synchro.preview.text"));

  {
    QFile f(tmp.filePath(QStringLiteral("NOTES")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("loose notes\n");
  }
  QVERIFY(MimeMap::isProbablyText(tmp.filePath(QStringLiteral("NOTES")),
                                  QStringLiteral("application/octet-stream")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  const QVariantMap preview =
      host.readPreview(QUrl::fromLocalFile(txt.path), 1024);
  QVERIFY(preview.value(QStringLiteral("ok")).toBool());
  QVERIFY(preview.value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("hello text")));
}

void PeekOverlayTest::panelLookUsesOptInQuickAppsAndAsyncReads() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString textPath = tmp.filePath(QStringLiteral("notes.txt"));
  {
    QFile f(textPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("ambient preview\n");
  }
  {
    QFile f(tmp.filePath(QStringLiteral("bundle.zip")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not really a zip");
  }
  const QString duckdbPath = tmp.filePath(QStringLiteral("sample.duckdb"));
  {
    QFile f(duckdbPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not really duckdb");
  }
  const QString parquetPath = tmp.filePath(QStringLiteral("sample.parquet"));
  {
    QFile f(parquetPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not really parquet");
  }
  const QString imagePath = tmp.filePath(QStringLiteral("search-image.png"));
  QVERIFY(writePng(imagePath));
  {
    QFile f(imagePath);
    QVERIFY(f.open(QIODevice::Append));
    f.write("png trailer");
  }
  const QString contentImagePath =
      tmp.filePath(QStringLiteral("search-vector.svg"));
  {
    QFile f(contentImagePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("<svg xmlns=\"http://www.w3.org/2000/svg\">"
            "<text>visual_search_token</text></svg>\n");
  }
  const QString videoPath = tmp.filePath(QStringLiteral("search-video.mp4"));
  {
    QFile f(videoPath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a real video");
  }
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("child-folder")));
  {
    QFile f(tmp.filePath(QStringLiteral("child-folder/inside.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("inside the lens\n");
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);

  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.terminal"),
                                      QStringLiteral("preview")));
  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.sql"),
                                      QStringLiteral("preview")));
  QVERIFY(host.panelSupportsCompanion(QStringLiteral("synchro.panel.duckdb"),
                                      QStringLiteral("preview")));

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int textRow = findProxy(proxy, QStringLiteral("notes.txt"));
  const int zipRow = findProxy(proxy, QStringLiteral("bundle.zip"));
  const int duckdbRow = findProxy(proxy, QStringLiteral("sample.duckdb"));
  const int parquetRow = findProxy(proxy, QStringLiteral("sample.parquet"));
  const int folderRow = findProxy(proxy, QStringLiteral("child-folder"));
  QVERIFY(textRow >= 0);
  QVERIFY(zipRow >= 0);
  QVERIFY(duckdbRow >= 0);
  QVERIFY(parquetRow >= 0);
  QVERIFY(folderRow >= 0);

  proxy.setCurrentIndex(textRow);
  host.setInlinePreviewActive(true);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("text"));
  QCOMPARE(host.inlinePreviewHandler(), QStringLiteral("synchro.preview.text"));
  QVERIFY(!host.inlinePreviewItem());

  QSignalSpy ready(&host, &HostApi::previewReady);
  const quint64 request =
      host.requestPreview(QUrl::fromLocalFile(textPath), 1024, 0);
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (const QList<QVariant> &args : ready) {
          if (args.at(0).toULongLong() == request)
            return true;
        }
        return false;
      },
      2000));
  QList<QVariant> result;
  for (const QList<QVariant> &args : ready) {
    if (args.at(0).toULongLong() == request) {
      result = args;
      break;
    }
  }
  QVERIFY(result.at(2)
              .toMap()
              .value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("ambient preview")));

  proxy.setCurrentIndex(zipRow);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("card"));
  QVERIFY(!host.inlinePreviewItem());
  QVERIFY(host.inlinePreviewHandler().isEmpty());

  // Data handlers explicitly opt into Look as chrome-less quick apps. Their
  // file inspection still happens on a worker and is request-tagged.
  proxy.setCurrentIndex(duckdbRow);
  host.refreshInlinePreview();
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("rich"),
                            3000);
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.duckdb"));
  QVERIFY(host.inlinePreviewItem());
  QVERIFY(host.inlinePreviewItem()->property("inlinePreview").toBool());
  QSignalSpy databaseReady(&host, &HostApi::databaseReady);
  const quint64 databaseRequest = host.requestDatabase(
      QUrl::fromLocalFile(duckdbPath), QStringLiteral("duckdb"));
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (const QList<QVariant> &args : databaseReady) {
          if (args.at(0).toULongLong() == databaseRequest)
            return true;
        }
        return false;
      },
      3000));

  proxy.setCurrentIndex(parquetRow);
  host.refreshInlinePreview();
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("rich"),
                            3000);
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.parquet"));
  QVERIFY(host.inlinePreviewItem());
  QVERIFY(host.inlinePreviewItem()->property("inlinePreview").toBool());
  QSignalSpy parquetReady(&host, &HostApi::parquetReady);
  const quint64 parquetRequest =
      host.requestParquet(QUrl::fromLocalFile(parquetPath), 12);
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (const QList<QVariant> &args : parquetReady) {
          if (args.at(0).toULongLong() == parquetRequest)
            return true;
        }
        return false;
      },
      3000));

  proxy.setCurrentIndex(folderRow);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("folder"));
  auto *folderModel = qobject_cast<DirectoryModel *>(host.inlineFolderModel());
  auto *folderProxy = qobject_cast<FilterProxy *>(host.inlineFolderProxy());
  QVERIFY(folderModel);
  QVERIFY(folderProxy);
  QVERIFY(QTest::qWaitFor(
      [&] { return !folderModel->listing() && folderProxy->count() == 1; },
      2000));
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("inside.txt"));
  QVERIFY(host.commitInlineFolderRow(0));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(model.path()).canonicalFilePath() ==
               QFileInfo(tmp.filePath(QStringLiteral("child-folder")))
                   .canonicalFilePath();
      },
      2000));

  // Name and content search are virtual listings, but their real files must
  // use the same MIME-backed preview dispatch as ordinary folder rows.
  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("search-image"), tmp.path(), false, false);
  QVERIFY(QTest::qWaitFor([&] { return !search.listing(); }, 5000));
  QCOMPARE(search.count(), 1);
  proxy.setCurrentIndex(0);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewPath(), imagePath);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("image"));
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.image"));

  search.start(QStringLiteral("visual_search_token"), tmp.path(), false, true);
  QVERIFY(QTest::qWaitFor([&] { return !search.listing(); }, 5000));
  QCOMPARE(search.count(), 1);
  proxy.setCurrentIndex(0);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewPath(), contentImagePath);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("image"));
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.image"));

  search.start(QStringLiteral("search-video"), tmp.path(), false, false);
  QVERIFY(QTest::qWaitFor([&] { return !search.listing(); }, 5000));
  QCOMPARE(search.count(), 1);
  proxy.setCurrentIndex(0);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("video"));
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.video"));
  QVERIFY(!host.inlinePreviewItem());
  QCOMPARE(host.inlinePreviewPath(), videoPath);
  QCOMPARE(model.data(model.index(0, 0), DirectoryModel::MimeRole).toString(),
           QStringLiteral("video/mp4"));
  search.setThumbnail(videoPath,
                      QStringLiteral("image://synchrothumb/video-poster"));
  host.refreshInlinePreview();
  QCOMPARE(
      host.inlinePreviewStat().value(QStringLiteral("thumbnail")).toString(),
      QStringLiteral("image://synchrothumb/video-poster"));

  // A direct SQL file projection has the same contract even when the query
  // did not include a MIME column.
  QVariantMap sqlImage;
  sqlImage.insert(QStringLiteral("cwd"), tmp.path());
  sqlImage.insert(QStringLiteral("columns"),
                  QVariantList{QVariantMap{
                      {QStringLiteral("name"), QStringLiteral("path")}}});
  sqlImage.insert(QStringLiteral("rows"),
                  QVariantList{QVariantMap{{QStringLiteral("name"),
                                            QStringLiteral("search-image.png")},
                                           {QStringLiteral("path"), imagePath},
                                           {QStringLiteral("is_dir"), false}}});
  model.showSqlResult(sqlImage, QStringLiteral("image result"));
  proxy.setCurrentIndex(0);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewPath(), imagePath);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("image"));
  QCOMPARE(host.inlinePreviewHandler(),
           QStringLiteral("synchro.preview.image"));

  // Aggregate rows are query-backed folders too. The Look surface runs the
  // generated drill SQL asynchronously, and a newer cursor wins even if an
  // older query finishes later.
  ScopedEnvironment restoreHome(QByteArrayLiteral("SYNCHRO_HOME"));
  QTemporaryDir catalogHome;
  QVERIFY(catalogHome.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(catalogHome.path()));
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  FileCatalog catalog(&model);
  host.setFileCatalog(&catalog);
  catalog.scanTree(tmp.path(), 100);
  QVERIFY(QTest::qWaitFor([&] { return !catalog.indexing(); }, 10000));
  const QVariantMap groups = FileCatalog::querySync(
      QStringLiteral("select extension,count(*) as files from selection "
                     "where not is_dir group by extension order by extension"),
      tmp.path(),
      {textPath, tmp.filePath(QStringLiteral("bundle.zip")),
       tmp.filePath(QStringLiteral("child-folder/inside.txt"))});
  QVERIFY2(groups.value(QStringLiteral("ok")).toBool(),
           qPrintable(groups.value(QStringLiteral("error")).toString()));
  model.showSqlResult(groups, QStringLiteral("types"));
  const int textGroup = findProxy(proxy, QStringLiteral("txt"));
  const int zipGroup = findProxy(proxy, QStringLiteral("zip"));
  QVERIFY(textGroup >= 0);
  QVERIFY(zipGroup >= 0);

  proxy.setCurrentIndex(textGroup);
  host.refreshInlinePreview();
  proxy.setCurrentIndex(zipGroup);
  host.refreshInlinePreview();
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("folder"));
  QVERIFY(host.inlineFolderLoading());
  QVERIFY(QTest::qWaitFor(
      [&] { return !host.inlineFolderLoading() && folderProxy->count() == 1; },
      10000));
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("bundle.zip"));
  QCOMPARE(folderProxy->data(folderProxy->index(0, 0), DirectoryModel::MimeRole)
               .toString(),
           QStringLiteral("application/zip"));
  QTest::qWait(100);
  QCOMPARE(folderProxy->rowMap(0).value(QStringLiteral("name")).toString(),
           QStringLiteral("bundle.zip"));
  host.setFileCatalog(nullptr);

  host.setInlinePreviewActive(false);
  QVERIFY(host.inlinePreviewMode().isEmpty());
}

void PeekOverlayTest::standaloneLookFollowsSelectionAndMigrates() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile note(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("standalone look\n");
  }
  {
    QFile source(tmp.filePath(QStringLiteral("sample.cpp")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("int main() { return 0; }\n");
  }
  {
    QFile markdown(tmp.filePath(QStringLiteral("guide.md")));
    QVERIFY(markdown.open(QIODevice::WriteOnly));
    markdown.write(
        "# Project guide\n\nReadable prose.\n\n```sh\necho ready\n```\n");
  }
  QImage colorfulPreview(64, 40, QImage::Format_RGB32);
  colorfulPreview.fill(qRgb(35, 85, 220));
  QVERIFY(
      colorfulPreview.save(tmp.filePath(QStringLiteral("sample.png")), "PNG"));
  {
    QFile video(tmp.filePath(QStringLiteral("sample.mp4")));
    QVERIFY(video.open(QIODevice::WriteOnly));
    video.write("not-media");
  }
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("folder")));
  {
    QFile child(tmp.filePath(QStringLiteral("folder/inside.txt")));
    QVERIFY(child.open(QIODevice::WriteOnly));
    child.write("inside\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  SelectionModel selection(&proxy, &model);
  keys.setSelection(&selection);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  Config config(tmp.filePath(QStringLiteral("config.json")));

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int noteRow = findProxy(proxy, QStringLiteral("notes.txt"));
  const int sourceRow = findProxy(proxy, QStringLiteral("sample.cpp"));
  const int markdownRow = findProxy(proxy, QStringLiteral("guide.md"));
  const int imageRow = findProxy(proxy, QStringLiteral("sample.png"));
  const int videoRow = findProxy(proxy, QStringLiteral("sample.mp4"));
  const int folderRow = findProxy(proxy, QStringLiteral("folder"));
  QVERIFY(noteRow >= 0);
  QVERIFY(sourceRow >= 0);
  QVERIFY(markdownRow >= 0);
  QVERIFY(imageRow >= 0);
  QVERIFY(videoRow >= 0);
  QVERIFY(folderRow >= 0);
  selection.click(noteRow);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  host.setSelection(&selection);
  keys.setPeekHost(&host);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("selectionModel"),
                                           &selection);
  engine.rootContext()->setContextProperty(QStringLiteral("appConfig"),
                                           &config);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &host);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  QVERIFY(window);
  // A common narrow floating-window size must still admit Look; the old
  // fixed 760 px gate made the enabled control appear to do nothing here.
  window->setWidth(741);
  window->setHeight(700);

  auto *look = window->findChild<QQuickItem *>(QStringLiteral("panelLook"));
  auto *dock = window->findChild<QQuickItem *>(QStringLiteral("panelDock"));
  auto *toggle =
      window->findChild<QQuickItem *>(QStringLiteral("browserLookToggle"));
  QVERIFY(look);
  QVERIFY(dock);
  QVERIFY(toggle);
  QVERIFY(toggle->property("checked").toBool());
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible() && look->width() >= 240, 1000);
  QVERIFY(look->parentItem() != dock);
  QCOMPARE(host.inlinePreviewPath(), tmp.filePath(QStringLiteral("notes.txt")));

  // Grid geometry is live state: resizing or moving Look must update the
  // keyboard stride without changing which filesystem item is selected.
  keys.setGridMode(true);
  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QTRY_VERIFY_WITH_TIMEOUT(grid && grid->isVisible(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(keys.gridStride(),
                            grid->property("columns").toInt(), 1000);
  const QString selectedPath = selection.selectedPaths().constFirst();
  window->setWidth(940);
  QTRY_COMPARE_WITH_TIMEOUT(keys.gridStride(),
                            grid->property("columns").toInt(), 1000);
  QCOMPARE(selection.selectedPaths(), QStringList{selectedPath});
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(keys.gridStride(),
                            grid->property("columns").toInt(), 1000);
  QCOMPARE(selection.selectedPaths(), QStringList{selectedPath});
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(keys.gridStride(),
                            grid->property("columns").toInt(), 1000);
  QCOMPARE(selection.selectedPaths(), QStringList{selectedPath});
  window->setWidth(741);
  keys.setGridMode(false);
  QTRY_VERIFY_WITH_TIMEOUT(
      window->findChild<QQuickItem *>(QStringLiteral("fileList")) != nullptr,
      1000);

  // StrataV and MapV are browser presentations, not modal worlds: the same
  // selected item and Look/Miller companion remain mounted beside them.
  keys.setFsnMode(true);
  QTRY_VERIFY_WITH_TIMEOUT(
      window->findChild<QQuickItem *>(QStringLiteral("fileFsn")) != nullptr,
      2000);
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible() && look->width() >= 240, 1000);
  QCOMPARE(host.inlinePreviewPath(), tmp.filePath(QStringLiteral("notes.txt")));

  // A recursive 3D node is not a row in the root listing, but clicking it
  // must still retarget the shared Look companion and browser selection.
  const QString nested3d = tmp.filePath(QStringLiteral("folder/inside.txt"));
  auto *fsnView = window->findChild<QQuickItem *>(QStringLiteral("fileFsn"));
  QVERIFY(fsnView);
  QVERIFY(QMetaObject::invokeMethod(
      fsnView, "selectPath", Q_ARG(QVariant, nested3d),
      Q_ARG(QVariant, false), Q_ARG(QVariant, 0), Q_ARG(QVariant, false),
      Q_ARG(QVariant, QStringLiteral("inside.txt")), Q_ARG(QVariant, 7)));
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewPath(), nested3d, 2000);
  QCOMPARE(selection.selectedPaths(), QStringList{nested3d});

  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("folder/media")));
  const QString nestedImage =
      tmp.filePath(QStringLiteral("folder/media/inside.png"));
  QImage nestedPixels(12, 8, QImage::Format_RGB32);
  nestedPixels.fill(qRgb(30, 120, 210));
  QVERIFY(nestedPixels.save(nestedImage, "PNG"));
  QVERIFY(QMetaObject::invokeMethod(
      fsnView, "selectPath", Q_ARG(QVariant, nestedImage),
      Q_ARG(QVariant, false), Q_ARG(QVariant, 0), Q_ARG(QVariant, false),
      Q_ARG(QVariant, QStringLiteral("inside.png")),
      Q_ARG(QVariant, QFileInfo(nestedImage).size())));
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewPath(), nestedImage, 2000);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("image"),
                            2000);
  QCOMPARE(host.inlinePreviewStat().value(QStringLiteral("mime")).toString(),
           QStringLiteral("image/png"));
  QVERIFY(!host.inlinePreviewStat().value(QStringLiteral("uri")).toUrl()
               .isEmpty());

  const QString nestedVideo =
      tmp.filePath(QStringLiteral("folder/media/inside.mp4"));
  {
    QFile nestedVideoFile(nestedVideo);
    QVERIFY(nestedVideoFile.open(QIODevice::WriteOnly));
    QCOMPARE(nestedVideoFile.write(QByteArrayLiteral("not-media")), 9);
  }
  QVERIFY(QMetaObject::invokeMethod(
      fsnView, "selectPath", Q_ARG(QVariant, nestedVideo),
      Q_ARG(QVariant, false), Q_ARG(QVariant, 0), Q_ARG(QVariant, false),
      Q_ARG(QVariant, QStringLiteral("inside.mp4")),
      Q_ARG(QVariant, QFileInfo(nestedVideo).size())));
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewPath(), nestedVideo, 2000);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("video"),
                            2000);
  QCOMPARE(host.inlinePreviewStat().value(QStringLiteral("mime")).toString(),
           QStringLiteral("video/mp4"));
  QVERIFY(QDir(tmp.filePath(QStringLiteral("folder/media")))
              .removeRecursively());

  selection.click(noteRow);
  QCOMPARE(selection.selectedPaths(),
           QStringList{tmp.filePath(QStringLiteral("notes.txt"))});
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewPath(),
                            tmp.filePath(QStringLiteral("notes.txt")), 2000);
  keys.setFsnTreeView(false);
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  keys.setFsnMode(false);
  QTRY_VERIFY_WITH_TIMEOUT(
      window->findChild<QQuickItem *>(QStringLiteral("fileList")) != nullptr,
      1000);

  // Browser Space is a transient Look toggle. It must not rewrite the saved
  // startup preference; Shift+Space remains the deliberate full Peek.
  QVERIFY(keys.lookKeyMode());
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
  QVERIFY(config.panelLookOpen());
  QVERIFY(!host.isOpen());
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(host.isOpen() && !look->isVisible(), 2000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen() && look->isVisible(), 2000);

  // When the browser cannot retain a usable content column, Space falls back
  // to full Peek instead of silently doing nothing.
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
  window->setWidth(500);
  QTRY_VERIFY_WITH_TIMEOUT(!window->property("lookStandaloneHasRoom").toBool(),
                           1000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(host.isOpen(), 2000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 2000);
  window->setWidth(741);
  QTRY_VERIFY_WITH_TIMEOUT(window->property("lookStandaloneHasRoom").toBool(),
                           1000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);

  selection.click(sourceRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewPath(),
                            tmp.filePath(QStringLiteral("sample.cpp")), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("text"),
                            1000);
  QTRY_VERIFY_WITH_TIMEOUT(look->property("coreData")
                               .toMap()
                               .value(QStringLiteral("highlighted"))
                               .toBool(),
                           2000);

  selection.click(markdownRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(),
                            QStringLiteral("markdown"), 1000);
  QTRY_VERIFY_WITH_TIMEOUT(!look->property("coreData")
                                .toMap()
                                .value(QStringLiteral("markdownHtml"))
                                .toString()
                                .isEmpty(),
                           2000);
  auto *markdownBody =
      look->findChild<QQuickItem *>(QStringLiteral("inlineLookText"));
  QVERIFY(markdownBody);
  QCOMPARE(markdownBody->property("textFormat").toInt(), int(Qt::RichText));
  QVERIFY(markdownBody->width() <= look->width());

  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(host.isOpen(), 2000);
  auto *markdownPreview = qobject_cast<QQuickItem *>(host.previewItem());
  QVERIFY(markdownPreview);
  QQuickItem *peekMarkdownBody = nullptr;
  QVERIFY(QTest::qWaitFor(
      [&] {
        peekMarkdownBody = markdownPreview->findChild<QQuickItem *>(
            QStringLiteral("markdownReadingBody"));
        return peekMarkdownBody && peekMarkdownBody->isVisible() &&
               peekMarkdownBody->property("textFormat").toInt() ==
                   int(Qt::RichText);
      },
      3000));
  QVERIFY(peekMarkdownBody->property("text").toString().contains(
      QStringLiteral("Project guide")));
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen() && look->isVisible(), 2000);

  selection.click(sourceRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("text"),
                            1000);

  // The Action Deck is an overlay over the current browser composition. It
  // must not collapse Look or clear/recreate its inline preview behind the
  // modal, which otherwise produces a visible layout and rendering thrash on
  // both open and close.
  const QQuickItem *lookParent = look->parentItem();
  const qreal lookWidth = look->width();
  const QString lookPath = host.inlinePreviewPath();
  QSignalSpy inlinePreviewChanges(&host, &HostApi::inlinePreviewChanged);
  QVERIFY2(host.openDoLayer(), qPrintable(host.lastError()));
  QTRY_VERIFY_WITH_TIMEOUT(host.actionOpen(), 1000);
  QVERIFY(look->isVisible());
  QCOMPARE(look->parentItem(), lookParent);
  QCOMPARE(look->width(), lookWidth);
  QCOMPARE(host.inlinePreviewPath(), lookPath);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("text"));
  QCOMPARE(inlinePreviewChanges.count(), 0);
  host.closeAction();
  QTRY_VERIFY_WITH_TIMEOUT(!host.actionOpen(), 1000);
  QVERIFY(look->isVisible());
  QCOMPARE(look->parentItem(), lookParent);
  QCOMPARE(look->width(), lookWidth);
  QCOMPARE(host.inlinePreviewPath(), lookPath);
  QCOMPARE(host.inlinePreviewMode(), QStringLiteral("text"));
  QCOMPARE(inlinePreviewChanges.count(), 0);

  selection.click(imageRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("image"),
                            1000);
  auto *imageStage =
      look->findChild<QQuickItem *>(QStringLiteral("lookImageStage"));
  auto *imageBloom =
      look->findChild<QQuickItem *>(QStringLiteral("lookImageBloom"));
  QVERIFY(imageStage);
  QVERIFY(imageBloom);
  QTRY_VERIFY_WITH_TIMEOUT(imageStage->isVisible(), 1000);
  QTRY_VERIFY_WITH_TIMEOUT(imageBloom->isVisible(), 3000);
  QVERIFY(!look->findChild<QQuickItem *>(QStringLiteral("lookImageMatte")));

  selection.click(videoRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("video"),
                            1000);
  auto *videoStage =
      look->findChild<QQuickItem *>(QStringLiteral("lookVideoStage"));
  auto *videoLoader =
      look->findChild<QQuickItem *>(QStringLiteral("lookVideoLoader"));
  QVERIFY(videoStage);
  QVERIFY(videoLoader);
  QTRY_VERIFY_WITH_TIMEOUT(videoStage->isVisible(), 1000);
  QVERIFY(!videoLoader->property("active").toBool());
  QVERIFY(QMetaObject::invokeMethod(look, "toggleVideoPlayback"));
  QTRY_VERIFY_WITH_TIMEOUT(videoLoader->property("active").toBool(), 1000);
  QTRY_VERIFY_WITH_TIMEOUT(
      look->findChild<QQuickItem *>(QStringLiteral("lookVideoPlayer")), 1000);

  selection.click(folderRow);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("folder"),
                            1000);
  auto *folderProxy = qobject_cast<FilterProxy *>(host.inlineFolderProxy());
  QVERIFY(folderProxy);
  QTRY_COMPARE_WITH_TIMEOUT(folderProxy->count(), 1, 2000);

  auto *folderLens =
      look->findChild<QQuickItem *>(QStringLiteral("folderLens"));
  QVERIFY(folderLens);
  QVERIFY(
      QMetaObject::invokeMethod(folderLens, "selectRow", Q_ARG(QVariant, 0)));
  folderLens->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_Space);
  QTRY_VERIFY_WITH_TIMEOUT(host.isOpen(), 2000);
  QCOMPARE(host.file().toLocalFile(),
           tmp.filePath(QStringLiteral("folder/inside.txt")));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(),
           QFileInfo(tmp.path()).canonicalFilePath());
  keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString());
  QTRY_VERIFY_WITH_TIMEOUT(!host.isOpen(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("folder"),
                            1000);

  selection.click(noteRow);
  selection.ctrlClick(folderRow);
  QCOMPARE(selection.selectedCount(), 2);
  QTRY_COMPARE_WITH_TIMEOUT(host.inlinePreviewMode(), QStringLiteral("multi"),
                            1000);
  auto *multiCard =
      look->findChild<QQuickItem *>(QStringLiteral("lookMultiCard"));
  QVERIFY(multiCard);
  QTRY_VERIFY_WITH_TIMEOUT(multiCard->isVisible(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(look->property("multiSummary")
                                .toMap()
                                .value(QStringLiteral("count"))
                                .toInt(),
                            2, 1000);

  selection.ctrlClick(noteRow);
  selection.ctrlClick(folderRow);
  QCOMPARE(selection.selectedCount(), 0);
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
  QVERIFY(host.inlinePreviewMode().isEmpty());

  selection.click(noteRow);
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  keys.setPanelSide(QStringLiteral("bottom"));
  keys.setPanelId(QStringLiteral("synchro.panel.sql"));
  QTRY_VERIFY_WITH_TIMEOUT(dock->isVisible(), 1000);
  QTRY_COMPARE_WITH_TIMEOUT(look->parentItem(), dock, 1000);

  keys.setPanelId(QString());
  QTRY_VERIFY_WITH_TIMEOUT(look->parentItem() != dock, 1000);
  QVERIFY(QMetaObject::invokeMethod(toggle, "triggered"));
  QVERIFY(!config.panelLookOpen());
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
}

void PeekOverlayTest::videoHandlerAndWebpRaster() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("clip.mp4")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a real mp4");
  }
  static const unsigned char kWebp[] = {
      0x52, 0x49, 0x46, 0x46, 0x40, 0x00, 0x00, 0x00, 0x57, 0x45, 0x42, 0x50,
      0x56, 0x50, 0x38, 0x20, 0x34, 0x00, 0x00, 0x00, 0xd0, 0x01, 0x00, 0x9d,
      0x01, 0x2a, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x34, 0x25, 0x98, 0x02,
      0x74, 0x01, 0x0e, 0xfe, 0x03, 0xc8, 0x00, 0x00, 0xfe, 0xe2, 0x9f, 0x61,
      0x0f, 0x3c, 0xf5, 0xbf, 0xc8, 0x2e, 0x6e, 0xa4, 0xa1, 0x1b, 0x3d, 0x0c,
      0x01, 0x32, 0x3f, 0xf0, 0x1f, 0xc4, 0xbf, 0xb0, 0x22, 0xb2, 0xb0, 0x00};
  {
    QFile f(tmp.filePath(QStringLiteral("tile.webp")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(reinterpret_cast<const char *>(kWebp), sizeof(kWebp));
  }

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.video")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.image")));

  Manifest::Item vid;
  vid.path = tmp.filePath(QStringLiteral("clip.mp4"));
  vid.mime = QStringLiteral("video/mp4");
  const auto vidHits = registry.resolve(QStringLiteral("preview"), {vid});
  QVERIFY(!vidHits.isEmpty());
  QCOMPARE(vidHits.constFirst().id, QStringLiteral("synchro.preview.video"));

  Manifest::Item webp;
  webp.path = tmp.filePath(QStringLiteral("tile.webp"));
  webp.mime = QStringLiteral("image/webp");
  const auto webpHits = registry.resolve(QStringLiteral("preview"), {webp});
  QVERIFY(!webpHits.isEmpty());
  QCOMPARE(webpHits.constFirst().id, QStringLiteral("synchro.preview.image"));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  HandlerRegistry::Record rec =
      registry.handler(QStringLiteral("synchro.preview.video"));
  QQuickItem *preview = loader.create(&engine, rec, QStringLiteral("preview"),
                                      &host, QUrl::fromLocalFile(vid.path), {});
  QVERIFY2(preview, qPrintable(loader.lastError()));
  preview->deleteLater();

  const QUrl src = QUrl::fromLocalFile(webp.path);
  const QUrl raster = host.rasterUrl(src);
  QVERIFY(raster.isLocalFile());
  // Qt imageformat plugins may make the original WebP directly paintable;
  // otherwise HostApi materializes the libwebp/ffmpeg result as PNG.
  QVERIFY(raster == src ||
          raster.toLocalFile().endsWith(QStringLiteral(".png")));
  QVERIFY(!QImage(raster.toLocalFile()).isNull());
}

void PeekOverlayTest::spaceOnFolderDrillsAndEscReturns() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("photos")));
  QVERIFY(QDir(tmp.filePath(QStringLiteral("photos")))
              .mkdir(QStringLiteral("trip")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("photos/a.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.folder")));

  Manifest::Item dir;
  dir.path = tmp.filePath(QStringLiteral("photos"));
  dir.mime = QStringLiteral("inode/directory");
  dir.isDir = true;
  const auto hits = registry.resolve(QStringLiteral("preview"), {dir});
  QVERIFY(!hits.isEmpty());
  QCOMPARE(hits.constFirst().id, QStringLiteral("synchro.preview.folder"));

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("photos"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  const QString rootPath = QFileInfo(tmp.path()).canonicalFilePath();

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() > 0;
      },
      3000));
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(),
           QStringLiteral("photos"));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int tripRow = findProxy(*peekProxy, QStringLiteral("trip"));
  QVERIFY(tripRow >= 0);
  peekProxy->setCurrentIndex(tripRow);
  keys.setGridMode(true);
  keys.setGridStride(4);
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(),
           QStringLiteral("photos"));
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QVERIFY(hostApi.folderPeek());
  peekProxy->setCurrentIndex(tripRow);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(QTest::qWaitFor(
      [&] {
        return QFileInfo(hostApi.folderPath()).fileName() ==
               QStringLiteral("trip");
      },
      3000));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);

  hostApi.peekBack();
  QCOMPARE(QFileInfo(hostApi.folderPath()).fileName(),
           QStringLiteral("photos"));
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());
  QVERIFY(!hostApi.folderPeek());
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), rootPath);
}

void PeekOverlayTest::folderPeekWasdUsesOwnStride() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("grid")));
  for (int i = 0; i < 16; ++i) {
    QFile f(tmp.filePath(
        QStringLiteral("grid/f%1.txt").arg(i, 2, 10, QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(keys.gridMode());
  QObject::connect(&keys, &KeyMachine::gridModeChanged, &hostApi,
                   [&] { hostApi.setGridMode(keys.gridMode()); });

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("grid"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() >= 16;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  peekProxy->setCurrentIndex(7);
  keys.setGridMode(true);
  keys.setGridStride(8);
  hostApi.setPeekGridStride(3);
  QCOMPARE(hostApi.peekGridStride(), 3);
  QVERIFY(keys.handleListKey(Qt::Key_W, Qt::NoModifier, QStringLiteral("w")));
  QCOMPARE(peekProxy->currentIndex(), 4);
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(peekProxy->currentIndex(), 7);
  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QCOMPARE(peekProxy->currentIndex(), 6);
}

void PeekOverlayTest::folderPeekFileBackKeepsListingAndScroll() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  for (int i = 0; i < 40; ++i) {
    QFile f(tmp.filePath(
        QStringLiteral("docs/n%1.txt").arg(i, 2, 10, QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("note\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("docs"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(keys.gridMode());
  QObject::connect(&keys, &KeyMachine::gridModeChanged, &hostApi,
                   [&] { hostApi.setGridMode(keys.gridMode()); });
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

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
  window->resize(800, 520);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  list->forceActiveFocus();
  QVERIFY(QTest::qWaitFor([&] { return list->hasActiveFocus(); }, 1000));

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier,
                         QStringLiteral(" ")));
  QVERIFY(hostApi.folderPeek());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return hostApi.folderListingItem() && pm && !pm->listing() &&
               pm->count() >= 40;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  peekProxy->setCurrentIndex(32);

  QObject *listing = hostApi.folderListingItem();
  QVERIFY(listing);
  auto *overlay =
      window->findChild<QQuickItem *>(QStringLiteral("peekOverlay"));
  QVERIFY(overlay);
  QMetaObject::invokeMethod(overlay, "reparentPreview");

  QQuickItem *folderList = nullptr;
  QVERIFY(QTest::qWaitFor(
      [&] {
        folderList =
            qobject_cast<QQuickItem *>(listing)->findChild<QQuickItem *>(
                QStringLiteral("peekFolderList"));
        if (!folderList)
          folderList =
              window->findChild<QQuickItem *>(QStringLiteral("peekFolderList"));
        return folderList && folderList->height() > 40 &&
               folderList->property("contentHeight").toReal() > 200;
      },
      2000));
  folderList->setProperty("contentY", 160.0);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return qAbs(folderList->property("contentY").toReal() - 160.0) < 2.0;
      },
      1000));
  const qreal yBefore = folderList->property("contentY").toReal();

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor(
      [&] { return hostApi.isOpen() && !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.folderListingItem(), listing);
  QVERIFY(hostApi.folderPeek());
  QVERIFY(hostApi.peekFileName().endsWith(QStringLiteral(".txt")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *name =
            window->findChild<QQuickItem *>(QStringLiteral("peekFileName"));
        auto *idx =
            window->findChild<QQuickItem *>(QStringLiteral("peekFileIndex"));
        return name && name->isVisible() && idx && idx->isVisible() &&
               idx->width() > 8;
      },
      2000));

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.folderListingItem(), listing);
  QCOMPARE(hostApi.previewItem(), listing);
  folderList = qobject_cast<QQuickItem *>(listing)->findChild<QQuickItem *>(
      QStringLiteral("peekFolderList"));
  QVERIFY(folderList);
  const qreal yAfter = folderList->property("contentY").toReal();
  QVERIFY2(
      qAbs(yAfter - yBefore) < 2.0,
      qPrintable(
          QStringLiteral("contentY jumped %1 -> %2").arg(yBefore).arg(yAfter)));
  auto *nameAfter =
      window->findChild<QQuickItem *>(QStringLiteral("peekFileName"));
  QVERIFY(!nameAfter || !nameAfter->isVisible());
}

void PeekOverlayTest::folderPeekSpaceOnFileReturnsListing() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  {
    QFile a(tmp.filePath(QStringLiteral("docs/alpha.txt")));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write("a\n");
    QFile b(tmp.filePath(QStringLiteral("docs/beta.txt")));
    QVERIFY(b.open(QIODevice::WriteOnly));
    b.write("b\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("docs"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing() && pm->count() >= 2;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int fileRow = findProxy(*peekProxy, QStringLiteral("beta.txt"));
  QVERIFY(fileRow >= 0);
  peekProxy->setCurrentIndex(fileRow);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("beta.txt"));

  QSignalSpy listingSpy(&hostApi, &HostApi::folderPeekChanged);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderListing());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(listingSpy.count() >= 1);

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.folderPeek());
  QVERIFY(!hostApi.isOpen());
}

void PeekOverlayTest::folderPeekStepsPastUnpreviewableFile() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("mix")));
  QVERIFY(QDir(tmp.filePath(QStringLiteral("mix")))
              .mkdir(QStringLiteral("subdir")));
  {
    QFile a(tmp.filePath(QStringLiteral("mix/aa.txt")));
    QVERIFY(a.open(QIODevice::WriteOnly));
    a.write("one\n");
    QFile z(tmp.filePath(QStringLiteral("mix/mystery.7z")));
    QVERIFY(z.open(QIODevice::WriteOnly));
    z.write("7z\xbc\xaf\x27\x1cnot-a-real-7z");
    QFile b(tmp.filePath(QStringLiteral("mix/zz.txt")));
    QVERIFY(b.open(QIODevice::WriteOnly));
    b.write("two\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("mix"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return hostApi.folderListing() && pm && !pm->listing() &&
               pm->count() >= 4;
      },
      3000));

  auto *peekProxy = qobject_cast<FilterProxy *>(hostApi.peekProxy());
  QVERIFY(peekProxy);
  const int aRow = findProxy(*peekProxy, QStringLiteral("aa.txt"));
  const int zipRow = findProxy(*peekProxy, QStringLiteral("mystery.7z"));
  const int zRow = findProxy(*peekProxy, QStringLiteral("zz.txt"));
  QVERIFY(aRow >= 0 && zipRow >= 0 && zRow >= 0);
  peekProxy->setCurrentIndex(aRow);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(QTest::qWaitFor([&] { return !hostApi.folderListing(); }, 2000));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("aa.txt"));
  QVERIFY(hostApi.previewItem() != nullptr);

  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("mystery.7z"));
  QVERIFY2(!hostApi.folderListing(),
           "unpreviewable file must not kick back to folder listing");
  QVERIFY(hostApi.previewItem() == nullptr);

  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(hostApi.peekFileName(), QStringLiteral("zz.txt"));
  QVERIFY(!hostApi.folderListing());
  QVERIFY(hostApi.previewItem() != nullptr);
  QVERIFY(hostApi.previewItem() != hostApi.folderListingItem());
}

void PeekOverlayTest::filePeekAdHopsAndPreviewScrolls() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  auto writeLines = [&](const QString &name) {
    QFile f(tmp.filePath(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      return false;
    for (int i = 0; i < 80; ++i)
      f.write(QStringLiteral("line %1 of %2\n").arg(i).arg(name).toUtf8());
    return true;
  };
  QVERIFY(writeLines(QStringLiteral("a.txt")));
  QVERIFY(writeLines(QStringLiteral("b.txt")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.txt"));
  const int b = findProxy(proxy, QStringLiteral("b.txt"));
  QVERIFY(a >= 0 && b >= 0);
  proxy.setCurrentIndex(qMin(a, b));
  const QString firstName = nameAt(proxy, proxy.currentIndex());

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  QVERIFY(!hostApi.peekPreviewFocused());

  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QVERIFY(hostApi.peekPreviewFocused());
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(nameAt(proxy, proxy.currentIndex()), firstName);

  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVariant consumed;
  QVERIFY(QMetaObject::invokeMethod(preview, "peekKey", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, consumed),
                                    Q_ARG(QVariant, int(Qt::Key_S)),
                                    Q_ARG(QVariant, int(Qt::NoModifier))));
  QVERIFY(consumed.toBool());

  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QVERIFY(proxy.currentIndex() != qMin(a, b));
  QVERIFY(hostApi.peekPreviewFocused());

  QVERIFY(keys.handleListKey(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
  QVERIFY(!hostApi.peekPreviewFocused());
}

void PeekOverlayTest::sqliteAndDuckdbHandlersResolve() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.sqlite")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.duckdb")));
  QVERIFY(registry.contains(QStringLiteral("synchro.preview.archive")));

  Manifest::Item db;
  db.path = tmp.filePath(QStringLiteral("app.sqlite"));
  db.mime = QStringLiteral("application/vnd.sqlite3");
  const auto dbHits = registry.resolve(QStringLiteral("preview"), {db});
  QVERIFY(!dbHits.isEmpty());
  QCOMPARE(dbHits.constFirst().id, QStringLiteral("synchro.preview.sqlite"));

  Manifest::Item duck;
  duck.path = tmp.filePath(QStringLiteral("lake.duckdb"));
  duck.mime = QStringLiteral("application/x-duckdb");
  const auto duckHits = registry.resolve(QStringLiteral("preview"), {duck});
  QVERIFY(!duckHits.isEmpty());
  QCOMPARE(duckHits.constFirst().id, QStringLiteral("synchro.preview.duckdb"));

  Manifest::Item zip;
  zip.path = tmp.filePath(QStringLiteral("pack.zip"));
  zip.mime = QStringLiteral("application/zip");
  const auto zipHits = registry.resolve(QStringLiteral("preview"), {zip});
  QVERIFY(!zipHits.isEmpty());
  QCOMPARE(zipHits.constFirst().id, QStringLiteral("synchro.preview.archive"));

  sqlite3 *raw = nullptr;
  QVERIFY(sqlite3_open(QFile::encodeName(db.path).constData(), &raw) ==
          SQLITE_OK);
  QVERIFY(sqlite3_exec(raw,
                       "CREATE TABLE t(id INTEGER); INSERT INTO t VALUES (3);",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
  sqlite3_close(raw);

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  const QVariantMap info =
      host.readDatabase(QUrl::fromLocalFile(db.path), QStringLiteral("sqlite"));
  QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
           qPrintable(info.value(QStringLiteral("error")).toString()));
  QCOMPARE(info.value(QStringLiteral("table")).toString(), QStringLiteral("t"));
}

void PeekOverlayTest::rootFilePeekShowsIndexAndQCloses() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  const int b = findProxy(proxy, QStringLiteral("b.png"));
  QVERIFY(a >= 0 && b >= 0);
  proxy.setCurrentIndex(qMin(a, b));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

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

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier,
                         QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(!hostApi.folderPeek());
  QCOMPARE(hostApi.peekProxy(), static_cast<QObject *>(&proxy));
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));

  auto *idx = window->findChild<QQuickItem *>(QStringLiteral("peekFileIndex"));
  QVERIFY(idx);
  QVERIFY(QTest::qWaitFor([&] { return idx->isVisible() && idx->width() > 0; },
                          2000));

  const QString first = hostApi.peekFileName();
  QVERIFY(!first.isEmpty());
  QVERIFY(keys.handleListKey(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
  QVERIFY(hostApi.peekFileName() != first);

  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("list-focused"));
}

void PeekOverlayTest::gridPeekIndexUsesThumbs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setGridMode(true);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int a = findProxy(proxy, QStringLiteral("a.png"));
  QVERIFY(a >= 0);
  proxy.setCurrentIndex(a);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(true);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier,
                         QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.gridMode());

  auto *grid =
      window->findChild<QQuickItem *>(QStringLiteral("peekFileIndexGrid"));
  auto *list =
      window->findChild<QQuickItem *>(QStringLiteral("peekFileIndexList"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] { return grid->isVisible() && grid->width() > 0; }, 2000));
  QVERIFY(!list || !list->isVisible());
}

void PeekOverlayTest::emptyFolderShowsHintInRootAndPeek() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("hollow")));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.filePath(QStringLiteral("hollow")));
  QVERIFY(waitListingDone(model));
  QCOMPARE(model.count(), 0);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto effectivelyVisible = [](QQuickItem *it) {
    for (QQuickItem *p = it; p; p = p->parentItem()) {
      if (!p->isVisible())
        return false;
    }
    return true;
  };
  QVERIFY(QTest::qWaitFor(
      [&] {
        const auto hints =
            window->findChildren<QQuickItem *>(QStringLiteral("emptyListing"));
        for (auto *h : hints) {
          if (effectivelyVisible(h) &&
              h->property("text").toString() == QStringLiteral("empty folder"))
            return true;
        }
        return false;
      },
      2000));

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("hollow"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::ShiftModifier,
                         QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(hostApi.folderPeek());
  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *pm = qobject_cast<DirectoryModel *>(hostApi.peekModel());
        return pm && !pm->listing();
      },
      3000));

  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  auto *peekHint =
      preview->findChild<QQuickItem *>(QStringLiteral("peekEmptyListing"));
  if (!peekHint)
    peekHint =
        window->findChild<QQuickItem *>(QStringLiteral("peekEmptyListing"));
  QVERIFY2(peekHint, "folder peek surface should include EmptyListing");
  QVERIFY(QTest::qWaitFor(
      [&] {
        return peekHint->property("text").toString() ==
               QStringLiteral("empty folder");
      },
      3000));
}

void PeekOverlayTest::gridPeekIndexHidesFolders() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("keep")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setGridMode(true);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("a.png")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  hostApi.setGridMode(true);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  auto *all = qobject_cast<QAbstractItemModel *>(hostApi.peekProxy());
  auto *files = qobject_cast<QAbstractItemModel *>(hostApi.peekFileProxy());
  QVERIFY(all);
  QVERIFY(files);
  QVERIFY(all->rowCount() >= 3);
  QCOMPARE(files->rowCount(), 2);
}

void PeekOverlayTest::peekEnterCommitsFileAndFolder() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("docs")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("shot.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("shot.png")));

  QQmlApplicationEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QCOMPARE(keys.mode(), QStringLiteral("peek-open"));
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("docs")));
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.folderPeek());
  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::NoModifier, QString()));
  QVERIFY(!hostApi.isOpen());
  QCOMPARE(QFileInfo(model.path()).fileName(), QStringLiteral("docs"));
}

void PeekOverlayTest::doLayerVerbsKeysAndCopyAs() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString file = tmp.filePath(QStringLiteral("notes.md"));
  {
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hi\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.action.copy-as")));
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("notes.md"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);

  QVERIFY(keys.handleListKey(Qt::Key_Return, Qt::ControlModifier, QString()));
  QVERIFY2(hostApi.actionOpen(), qPrintable(hostApi.lastError()));
  QCOMPARE(hostApi.listHint(), QStringLiteral("Enter open · Ctrl+Enter do"));
  QVERIFY(hostApi.doCaption().contains(QStringLiteral("notes.md")));
  QCOMPARE(hostApi.doMetadata().value(QStringLiteral("path")).toString(), file);
  QCOMPARE(hostApi.doMetadata().value(QStringLiteral("count")).toInt(), 1);
  QCOMPARE(hostApi.doMetadata().value(QStringLiteral("size")).toLongLong(), 3);

  QStringList ids;
  for (const QVariant &rowVar : hostApi.doVerbs()) {
    const QVariantMap m = rowVar.toMap();
    ids.append(m.value(QStringLiteral("id")).toString());
  }
  QVERIFY(ids.contains(QStringLiteral("synchro.do.open")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.open-with")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.copy-as")));
  QVERIFY(ids.contains(QStringLiteral("synchro.action.trash")));
  QCOMPARE(ids.constFirst(), QStringLiteral("synchro.do.open"));
  QVERIFY(ids.indexOf(QStringLiteral("synchro.action.open-with")) <
          ids.indexOf(QStringLiteral("synchro.action.copy-as")));
  QCOMPARE(hostApi.doIndex(), 0);
  QCOMPARE(hostApi.doBriefTitle(), QStringLiteral("Open"));
  QVERIFY(!hostApi.doHasParams());
  QVERIFY2(hostApi.doPreviewItem(),
           "markdown should mount a peek preview in the do content box");
  QVERIFY(!hostApi.doTargetIsDir());
  QVERIFY(hostApi.doFolderProxy() == nullptr);

  const int listing = proxy.currentIndex();
  QVERIFY(keys.handleListKey(Qt::Key_S, Qt::NoModifier, QStringLiteral("s")));
  QCOMPARE(proxy.currentIndex(), listing);
  QCOMPARE(hostApi.doIndex(), 1);
  QVERIFY(hostApi.doHasParams());
  QVERIFY(hostApi.actionItem());

  QVERIFY(hostApi.openDoLayer(QStringLiteral("synchro.action.copy-as")));
  QCOMPARE(hostApi.doBriefTitle(), QStringLiteral("Copy path"));
  QVERIFY(hostApi.doHasParams());
  QVERIFY(hostApi.actionItem());
  QVERIFY2(hostApi.runDoVerb(), qPrintable(hostApi.lastError()));
  QVERIFY(!hostApi.actionOpen());
  QCOMPARE(QGuiApplication::clipboard()->text(), file);

  QVERIFY(hostApi.openDoContext(88.5, 144.25));
  QVERIFY(hostApi.doContextual());
  QCOMPARE(hostApi.doAnchorX(), 88.5);
  QCOMPARE(hostApi.doAnchorY(), 144.25);
  hostApi.closeAction();

  QVERIFY(hostApi.openDoLayer());
  QVERIFY(keys.handleListKey(Qt::Key_Q, Qt::NoModifier, QStringLiteral("q")));
  QVERIFY(!hostApi.actionOpen());
  QCOMPARE(proxy.currentIndex(), listing);
}

void PeekOverlayTest::doLayerOffersOnlyMatchingOmaflows() {
  ScopedEnvironment binGuard("SYNCHRO_OMAFLOW_BIN");
  ScopedEnvironment configGuard("SYNCHRO_OMAFLOW_CONFIG_DIR");
  ScopedEnvironment stateGuard("SYNCHRO_OMAFLOW_STATE_DIR");
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const auto write = [](const QString &path, const QByteArray &body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(body) == body.size();
  };
  const QString binary = tmp.filePath(QStringLiteral("omaflow"));
  const QString config = tmp.filePath(QStringLiteral("config"));
  const QString state = tmp.filePath(QStringLiteral("state"));
  QVERIFY(write(binary, QByteArrayLiteral("#!/bin/sh\nexit 0\n")));
  QVERIFY(QFile::setPermissions(binary, QFileDevice::ReadOwner |
                                            QFileDevice::WriteOwner |
                                            QFileDevice::ExeOwner));
  QVERIFY(write(
      QDir(state).filePath(QStringLiteral("index.json")),
      QByteArrayLiteral("{\"rules\":[{\"id\":\"markdown-flow\","
                        "\"name\":\"Publish notes\",\"enabled\":true}]}")));
  QVERIFY(
      write(QDir(config).filePath(QStringLiteral("rules/markdown-flow.json")),
            QByteArrayLiteral(
                "{\"schemaVersion\":1,\"id\":\"markdown-flow\","
                "\"name\":\"Publish notes\",\"enabled\":true,"
                "\"effect\":\"create\","
                "\"trigger\":{\"type\":\"manual\"},"
                "\"accepts\":{\"mime\":[\"text/markdown\"],"
                "\"suffix\":[\".md\"],\"kind\":\"files\"},"
                "\"actions\":[{\"type\":\"notify\",\"message\":\"done\"}]}")));
  const QString selectedPath = tmp.filePath(QStringLiteral("notes.md"));
  QVERIFY(write(selectedPath, QByteArrayLiteral("# notes\n")));

  qputenv("SYNCHRO_OMAFLOW_BIN", binary.toUtf8());
  qputenv("SYNCHRO_OMAFLOW_CONFIG_DIR", config.toUtf8());
  qputenv("SYNCHRO_OMAFLOW_STATE_DIR", state.toUtf8());
  OmaflowBridge omaflow;

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  QVERIFY(registry.contains(QStringLiteral("synchro.action.omaflow")));
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("notes.md"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);
  SelectionModel selection(&proxy, &model);
  selection.click(row);

  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  host.setSelection(&selection);
  host.setOmaflowBridge(&omaflow);
  QVERIFY2(host.openDoLayer(QStringLiteral("synchro.action.omaflow")),
           qPrintable(host.lastError()));
  QStringList ids;
  for (const QVariant &value : host.doVerbs())
    ids.append(value.toMap().value(QStringLiteral("id")).toString());
  QVERIFY(ids.contains(QStringLiteral("synchro.flow.markdown-flow")));
  QCOMPARE(host.doBriefTitle(), QStringLiteral("Publish notes"));
  QCOMPARE(host.doProvider(), QStringLiteral("FLOW"));
  QCOMPARE(host.doEffect(), QStringLiteral("create"));
  QVERIFY(!host.doHasParams());
  QVERIFY(host.actionItem() == nullptr);
  QCOMPARE(host.omaflowMatches().size(), 1);
  QVERIFY(host.runDoVerb());
  QTRY_COMPARE_WITH_TIMEOUT(host.doOperationState(),
                            QStringLiteral("succeeded"), 3000);
  QVERIFY(host.actionOpen());
  host.closeAction();
  QVERIFY(host.openDoLayer(QStringLiteral("synchro.flow.markdown-flow")));
  QCOMPARE(host.doOperationState(), QStringLiteral("succeeded"));
  QVERIFY(host.runDoVerb());
  QVERIFY(!host.actionOpen());
  QVERIFY(host.doOperationState().isEmpty());

  selection.ctrlClick(row);
  QCOMPARE(selection.selectedCount(), 0);
  QVERIFY(host.openDoLayer());
  ids.clear();
  for (const QVariant &value : host.doVerbs())
    ids.append(value.toMap().value(QStringLiteral("id")).toString());
  QVERIFY(!ids.contains(QStringLiteral("synchro.flow.markdown-flow")));
}

void PeekOverlayTest::doLayerOverlaySplitChrome() {
  ScopedEnvironment homeGuard("SYNCHRO_HOME");
  QTemporaryDir home;
  QTemporaryDir tmp;
  QVERIFY(home.isValid());
  QVERIFY(tmp.isValid());
  qputenv("SYNCHRO_HOME", QFile::encodeName(home.path()));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("a.png")));
  FileCatalog catalog(&model);
  catalog.scanTree(tmp.path(), 100);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.indexing() && catalog.indexedCount() == 1,
                           10000);
  catalog.analyzeImages(tmp.path(), 10);
  QTRY_VERIFY_WITH_TIMEOUT(!catalog.analyzing(), 10000);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  hostApi.setFileCatalog(&catalog);
  keys.setPeekHost(&hostApi);
  QObject::connect(&keys, &KeyMachine::openWithRequested, &hostApi,
                   &HostApi::openWithPalette);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window =
      qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
  QVERIFY(window);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  QVERIFY2(hostApi.openDoLayer(QStringLiteral("synchro.action.open-with")),
           qPrintable(hostApi.lastError()));
  QVERIFY(QTest::qWaitFor([&] { return hostApi.actionOpen(); }, 2000));
  auto *frame = window->findChild<QQuickItem *>(QStringLiteral("doOverlay"));
  QVERIFY(frame);
  QVERIFY(frame->isVisible());
  auto *brief = window->findChild<QQuickItem *>(QStringLiteral("doBriefTitle"));
  QVERIFY(brief);
  QCOMPARE(brief->property("text").toString(), QStringLiteral("Open with…"));
  QVERIFY2(hostApi.doHasParams(), qPrintable(hostApi.lastError()));
  auto *params = hostApi.actionItem();
  QVERIFY2(params, qPrintable(hostApi.lastError().isEmpty()
                                  ? QStringLiteral("no mounted params QML")
                                  : hostApi.lastError()));
  QVERIFY(params->findChild<QQuickItem *>(QStringLiteral("openWithList")));
  auto *surface =
      window->findChild<QQuickItem *>(QStringLiteral("doParamSurface"));
  QVERIFY(surface);
  QVERIFY(
      QTest::qWaitFor([&] { return params->parentItem() == surface; }, 2000));
  QVERIFY2(hostApi.doPreviewItem(),
           "png should keep a file preview above the params strip");
  auto *content =
      window->findChild<QQuickItem *>(QStringLiteral("doContentSurface"));
  QVERIFY(content);
  QVERIFY(QTest::qWaitFor(
      [&] { return hostApi.doPreviewItem()->parentItem() == content; }, 2000));
  auto *metadata =
      window->findChild<QQuickItem *>(QStringLiteral("doMetadata"));
  QVERIFY(metadata);
  QCOMPARE(hostApi.doMetadata().value(QStringLiteral("path")).toString(),
           tmp.filePath(QStringLiteral("a.png")));
  QTRY_COMPARE_WITH_TIMEOUT(
      hostApi.doMetadata().value(QStringLiteral("width")).toInt(), 1, 3000);
  QCOMPARE(hostApi.doMetadata().value(QStringLiteral("height")).toInt(), 1);
  QTRY_VERIFY_WITH_TIMEOUT(
      hostApi.doMetadata().value(QStringLiteral("cataloged")).toBool(), 3000);
  QVERIFY(!hostApi.doMetadata().value(QStringLiteral("loading")).toBool());
  QVERIFY(!hostApi.doMetadata()
               .value(QStringLiteral("palette"))
               .toList()
               .isEmpty());
}

void PeekOverlayTest::doLayerShowsFilePreviewAndFolderGrid() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("album")));
  const QString album = tmp.filePath(QStringLiteral("album"));
  QVERIFY(writePng(QDir(album).filePath(QStringLiteral("one.png"))));
  QVERIFY(writePng(QDir(album).filePath(QStringLiteral("two.png"))));
  {
    QFile f(tmp.filePath(QStringLiteral("notes.md")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("# hi\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  QQmlEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("notes.md")));
  QVERIFY2(hostApi.openDoLayer(), qPrintable(hostApi.lastError()));
  QVERIFY(hostApi.doPreviewItem());
  QVERIFY(!hostApi.doTargetIsDir());
  QVERIFY(hostApi.doFolderProxy() == nullptr);
  hostApi.closeAction();

  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("album")));
  bool sawFolderProxy = false;
  QObject::connect(&hostApi, &HostApi::doPreviewChanged, &hostApi, [&] {
    if (hostApi.doFolderProxy())
      sawFolderProxy = true;
  });
  QVERIFY2(hostApi.openDoLayer(), qPrintable(hostApi.lastError()));
  QVERIFY2(
      sawFolderProxy,
      "doFolderProxy must be visible on doPreviewChanged so QML is not stuck");
  QVERIFY(hostApi.doTargetIsDir());
  QVERIFY(hostApi.doPreviewItem() == nullptr);
  auto *folder = qobject_cast<FilterProxy *>(hostApi.doFolderProxy());
  QVERIFY2(folder, "folder do-layer should list the folder, not a mosaic");
  QVERIFY(QTest::qWaitFor([&] { return folder->rowCount() >= 2; }, 2000));
  QVERIFY(findProxy(*folder, QStringLiteral("one.png")) >= 0);
  QVERIFY(findProxy(*folder, QStringLiteral("two.png")) >= 0);
  QCOMPARE(hostApi.doTargetName(), QStringLiteral("album"));
}

void PeekOverlayTest::volumesListingChromeUrls() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlEngine engine;
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  QVERIFY(hostApi.listingRowUrl().isEmpty());
  model.setPath(QStringLiteral("volumes://"));
  QVERIFY(hostApi.listingRowUrl().isValid());
  QVERIFY(hostApi.listingRowUrl().toLocalFile().endsWith(
      QStringLiteral("Row.qml")));
  QVERIFY(hostApi.listingThumbUrl().toLocalFile().endsWith(
      QStringLiteral("Thumb.qml")));
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(hostApi.listingRowUrl().isEmpty());
}

void PeekOverlayTest::volumesListingChromeVisible() {
  QCOMPARE(int(SearchModel::PercentRole), int(DirectoryModel::PercentRole));
  QCOMPARE(int(SearchModel::DetailRole), int(DirectoryModel::DetailRole));

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"), &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &hostApi);

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

  model.setPath(QStringLiteral("volumes://"));
  QVERIFY(hostApi.listingRowUrl().isValid());
  QVERIFY(model.rowCount() > 0);

  auto *list = window->findChild<QQuickItem *>(QStringLiteral("fileList"));
  QVERIFY(list);
  QVERIFY2(QTest::qWaitFor(
               [&] {
                 const auto chromes =
                     visualNamed(list, QStringLiteral("listingRowChrome"));
                 for (QQuickItem *chrome : chromes) {
                   if (chrome->isVisible() &&
                       chrome->property("percent").toInt() >= 0 &&
                       chrome->property("detail").toString().contains(
                           QStringLiteral("free")))
                     return true;
                 }
                 return false;
               },
               2000),
           "volumes:// rows must bind percent/detail so the bar paints");
}

void PeekOverlayTest::pathBarTabsSitAboveCommandField() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
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
  window->resize(480, 360);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *bar = window->findChild<QQuickItem *>(QStringLiteral("pathBar"));
  auto *crumbs = window->findChild<QQuickItem *>(QStringLiteral("pathCrumbs"));
  auto *tabs = window->findChild<QQuickItem *>(QStringLiteral("locationTabs"));
  auto *field = window->findChild<QQuickItem *>(QStringLiteral("commandField"));
  QVERIFY(bar);
  QVERIFY(crumbs);
  QVERIFY(tabs);
  QVERIFY(field);
  QVERIFY(crumbs->y() + crumbs->height() <= tabs->y() + 1);
  QVERIFY(bar->y() + bar->height() <= field->y() + 1);
  QVERIFY(tabs->y() + tabs->height() <= field->y() + 1);
  QVERIFY(crumbs->width() > window->width() * 0.6);
}

void PeekOverlayTest::fileGridCellsFillWidth() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
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
  window->resize(733, 500);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] { return grid->isVisible() && grid->width() > 0; }, 1000));
  const int cols = grid->property("columns").toInt();
  const qreal cell = grid->property("cellWidth").toReal();
  QVERIFY(cols >= 1);
  QVERIFY(cell > 0);
  QCOMPARE(qRound(cell * cols), qRound(grid->width()));

  window->resize(501, 500);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return qAbs(grid->property("cellWidth").toReal() *
                        grid->property("columns").toInt() -
                    grid->width()) < 1.0;
      },
      1000));

  window->resize(1900, 700);
  QVERIFY(QTest::qWaitFor([&] { return grid->width() > 1800; }, 1000));
  const qreal layout = grid->property("layoutWidth").toReal();
  QVERIFY(layout > 0);
  QVERIFY(layout < grid->width());
  QVERIFY(qAbs(grid->property("cellWidth").toReal() *
                   grid->property("columns").toInt() -
               layout) < 1.0);
}

void PeekOverlayTest::gridWasdTracksRenderedGeometryAcrossRelayout() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  for (int i = 0; i < 30; ++i) {
    QFile f(tmp.filePath(
        QStringLiteral("tile-%1.txt").arg(i, 2, 10, QLatin1Char('0'))));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("tile\n");
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setGridMode(true);
  SelectionModel selection(&proxy, &model);
  keys.setSelection(&selection);
  Config config(tmp.filePath(QStringLiteral("config.json")));

  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  selection.click(5);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);
  host.setSelection(&selection);
  keys.setPeekHost(&host);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"),
                                           &model);
  engine.rootContext()->setContextProperty(QStringLiteral("filterProxy"),
                                           &proxy);
  engine.rootContext()->setContextProperty(QStringLiteral("navStack"), &nav);
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"),
                                           &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("selectionModel"),
                                           &selection);
  engine.rootContext()->setContextProperty(QStringLiteral("appConfig"),
                                           &config);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), &host);

  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  QVERIFY(window);
  window->resize(920, 620);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));
  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  auto *look = window->findChild<QQuickItem *>(QStringLiteral("panelLook"));
  QVERIFY(grid);
  QVERIFY(look);
  QTRY_VERIFY_WITH_TIMEOUT(grid->isVisible(), 1000);

  auto renderedTarget = [&](int row, int dx, int dy) {
    QVariant target;
    const bool invoked = QMetaObject::invokeMethod(
        grid, "geometryTarget", Qt::DirectConnection,
        Q_RETURN_ARG(QVariant, target), Q_ARG(QVariant, row),
        Q_ARG(QVariant, dx), Q_ARG(QVariant, dy), Q_ARG(QVariant, 1));
    return invoked ? target.toInt() : -1;
  };
  auto moveDownFrom = [&](int row) {
    keys.moveGridCursorTo(row);
    int expected = -1;
    QTRY_VERIFY_WITH_TIMEOUT((expected = renderedTarget(row, 0, 1)) >= 0,
                             1000);
    grid->forceActiveFocus();
    QVERIFY(QTest::qWaitFor([&] { return grid->hasActiveFocus(); }, 1000));
    QTest::keyClick(window, Qt::Key_S);
    QCOMPARE(proxy.currentIndex(), expected);
  };

  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  moveDownFrom(5);

  // Look animates the grid width. The expected target is recalculated from
  // delegate centers after each composition change, not from stale columns.
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(!look->isVisible(), 1000);
  moveDownFrom(5);
  const qreal wideGridWidth = grid->width();
  window->setWidth(660);
  QTRY_VERIFY_WITH_TIMEOUT(grid->width() < wideGridWidth, 1000);
  moveDownFrom(5);
  window->setWidth(920);
  QTRY_VERIFY_WITH_TIMEOUT(grid->width() > 660, 1000);
  QVERIFY(keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QString()));
  QTRY_VERIFY_WITH_TIMEOUT(look->isVisible(), 1000);
  moveDownFrom(5);
}

void PeekOverlayTest::locationCloseConsumesClick() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("Pinned")));
  const QString pinned = tmp.filePath(QStringLiteral("Pinned"));

  Config config(tmp.filePath(QStringLiteral("config.json")));
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  LocationChips chips;
  chips.setRegistry(&registry);
  chips.setConfig(&config);
  chips.setNav(&nav);
  chips.setDirectoryModel(&model);
  keys.setLocationChips(&chips);
  QVERIFY(chips.pin(pinned));
  const QString bookmarkId = config.saveSqlBookmark(
      QStringLiteral("large files"),
      QStringLiteral("select path from tree order by size desc"), tmp.path());
  QVERIFY(!bookmarkId.isEmpty());

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
  engine.rootContext()->setContextProperty(QStringLiteral("keyMachine"),
                                           &keys);
  engine.rootContext()->setContextProperty(QStringLiteral("locationChips"),
                                           &chips);
  engine.rootContext()->setContextProperty(QStringLiteral("appConfig"),
                                           &config);
  engine.rootContext()->setContextProperty(QStringLiteral("hostApi"), nullptr);
  engine.load(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_MAIN_QML)));
  QVERIFY(!engine.rootObjects().isEmpty());
  auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  QVERIFY(window);
  window->resize(960, 640);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  const QString originalPath = QFileInfo(model.path()).canonicalFilePath();
  QQuickItem *pinRemove = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT([&] {
    const auto hits = visualNamed(
        window->contentItem(),
        QStringLiteral("remove:") + LocationChips::pinId(pinned));
    pinRemove = hits.isEmpty() ? nullptr : hits.first();
    return pinRemove != nullptr;
  }(), 1000);
  QPointF scene = pinRemove->mapToScene(
      QPointF(pinRemove->width() / 2, pinRemove->height() / 2));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene.toPoint());
  QTRY_VERIFY_WITH_TIMEOUT(!chips.isPinned(pinned), 1000);
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), originalPath);

  QSignalSpy bookmarkActivated(&chips, &LocationChips::sqlBookmarkActivated);
  QQuickItem *bookmarkRemove = nullptr;
  QTRY_VERIFY_WITH_TIMEOUT([&] {
    const auto hits = visualNamed(
        window->contentItem(), QStringLiteral("remove:") +
                                   LocationChips::sqlBookmarkId(bookmarkId));
    bookmarkRemove = hits.isEmpty() ? nullptr : hits.first();
    return bookmarkRemove != nullptr;
  }(), 1000);
  scene = bookmarkRemove->mapToScene(
      QPointF(bookmarkRemove->width() / 2, bookmarkRemove->height() / 2));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, scene.toPoint());
  QTRY_VERIFY_WITH_TIMEOUT(config.sqlBookmarks().isEmpty(), 1000);
  QCOMPARE(bookmarkActivated.count(), 0);
  QCOMPARE(QFileInfo(model.path()).canonicalFilePath(), originalPath);
}

void PeekOverlayTest::searchGridCellsMatchRows() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("b")));
  QVERIFY(QDir(tmp.path()).mkdir(QStringLiteral("a")));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("root.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a/alpha.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a/beta.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("b/zeta.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
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
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() >= 3;
      },
      2000));

  // Stream hits into an already-visible grouped grid (creation-order
  // tiles used to stay stale while selection walked sorted indexes).
  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("png"), tmp.path(), false);

  QVERIFY(QTest::qWaitFor(
      [&] {
        auto *groups =
            window->findChild<QQuickItem *>(QStringLiteral("searchGridGroups"));
        return !search.listing() && groups && groups->isVisible() &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() == 4;
      },
      5000));
  auto *groups =
      window->findChild<QQuickItem *>(QStringLiteral("searchGridGroups"));
  QVERIFY(groups);
  QCOMPARE(model.count(), 4);

  const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
  QCOMPARE(cells.size(), 4);
  QVector<int> seen;
  for (QQuickItem *cell : cells) {
    const int row = cell->property("rowIndex").toInt();
    QVERIFY(row >= 0 && row < model.count());
    QVERIFY(!seen.contains(row));
    seen.append(row);
    QCOMPARE(
        cell->property("name").toString(),
        model.data(model.index(row, 0), DirectoryModel::NameRole).toString());
    QCOMPARE(
        cell->property("path").toString(),
        model.data(model.index(row, 0), DirectoryModel::PathRole).toString());
  }
  std::sort(seen.begin(), seen.end());
  QCOMPARE(seen, (QVector<int>{0, 1, 2, 3}));

  const QString thumb = QStringLiteral("image://synchrothumb/search-grid-test");
  const QString path0 =
      model.data(model.index(0, 0), DirectoryModel::PathRole).toString();
  search.setThumbnail(path0, thumb);
  QVERIFY(QTest::qWaitFor(
      [&] {
        for (QQuickItem *cell : visualNamed(grid, QStringLiteral("gridCell"))) {
          if (cell->property("rowIndex").toInt() == 0)
            return cell->property("thumbnail").toString() == thumb;
        }
        return false;
      },
      1000));

  model.setCurrentIndex(2);
  QCOMPARE(model.currentName(),
           model.data(model.index(2, 0), DirectoryModel::NameRole).toString());
}

void PeekOverlayTest::searchGridDropsFolderTiles() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("keep-me.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("unrelated-folder-tile.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
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
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               visualNamed(grid, QStringLiteral("gridCell")).size() == 2;
      },
      2000));

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("keep-me"), tmp.path(), false);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
        return !search.listing() && model.count() == 1 &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles")) &&
               window->findChild<QQuickItem *>(
                   QStringLiteral("searchGridGroups")) &&
               cells.size() == 1 &&
               cells.constFirst()->property("name").toString() ==
                   QStringLiteral("keep-me.png");
      },
      5000));

  const auto cells = visualNamed(grid, QStringLiteral("gridCell"));
  QCOMPARE(cells.size(), 1);
  QCOMPARE(cells.constFirst()->property("name").toString(),
           QStringLiteral("keep-me.png"));
  for (QQuickItem *cell : cells) {
    QVERIFY(cell->property("name").toString() !=
            QStringLiteral("unrelated-folder-tile.png"));
  }
}

void PeekOverlayTest::searchGridVirtualizesGroups() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString listing = tmp.filePath(QStringLiteral("cwd"));
  QVERIFY(QDir().mkpath(listing));
  for (int i = 0; i < 48; ++i) {
    const QString dir =
        tmp.filePath(QStringLiteral("hits/g%1").arg(i, 2, 10, QChar('0')));
    QVERIFY(QDir().mkpath(dir));
    QFile f(dir + QStringLiteral("/synchrohit.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("x") == 1);
  }

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(listing);
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
  window->resize(800, 520);
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(QStringLiteral("fileGridTiles"));
      },
      2000));

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("synchrohit"), tmp.path(), false);
  QVERIFY2(QTest::qWaitFor(
               [&] { return !search.listing() && model.count() == 48; }, 8000),
           qPrintable(QStringLiteral("listing=%1 count=%2")
                          .arg(search.listing())
                          .arg(model.count())));
  QVERIFY(QTest::qWaitFor(
      [&] {
        return window->findChild<QQuickItem *>(
                   QStringLiteral("searchGridGroups")) &&
               !window->findChild<QQuickItem *>(
                   QStringLiteral("fileGridTiles"));
      },
      2000));

  auto *groups =
      window->findChild<QQuickItem *>(QStringLiteral("searchGridGroups"));
  QVERIFY(groups);
  auto *content = groups->property("contentItem").value<QQuickItem *>();
  QVERIFY(content);
  int delegates = 0;
  for (QQuickItem *child : content->childItems()) {
    if (child && child->width() > 0 && child->height() > 0)
      ++delegates;
  }
  QVERIFY2(delegates > 0 && delegates < 30,
           qPrintable(QStringLiteral("search instantiated %1 group delegates "
                                     "(expected viewport-sized)")
                          .arg(delegates)));
  QVERIFY(visualNamed(grid, QStringLiteral("gridCell")).size() < 30);
}

void PeekOverlayTest::contextualPanelRelevanceFollowsSelection() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile db(tmp.filePath(QStringLiteral("sample.duckdb")));
    QVERIFY(db.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(db.write("DUCK", 4) == 4);
    QFile note(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(note.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(note.write("plain", 5) == 5);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;

  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int dbRow = findProxy(proxy, QStringLiteral("sample.duckdb"));
  const int noteRow = findProxy(proxy, QStringLiteral("notes.txt"));
  QVERIFY(dbRow >= 0);
  QVERIFY(noteRow >= 0);
  proxy.setCurrentIndex(dbRow);
  SelectionModel selection(&proxy, &model);

  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               nullptr);
  host.setSelection(&selection);
  const auto hasPanel = [&](const QString &id) {
    const QVariantList panels = host.relevantPanels();
    for (const QVariant &panel : panels) {
      if (panel.toMap().value(QStringLiteral("id")).toString() == id)
        return true;
    }
    return false;
  };

  QVERIFY(hasPanel(QStringLiteral("synchro.panel.terminal")));
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.duckdb")));

  selection.ctrlClick(dbRow);
  QCOMPARE(selection.selectedCount(), 0);
  QVERIFY(!hasPanel(QStringLiteral("synchro.panel.duckdb")));
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.terminal")));

  selection.click(noteRow);
  QCOMPARE(selection.selectedCount(), 1);
  QVERIFY(!hasPanel(QStringLiteral("synchro.panel.duckdb")));

  selection.click(dbRow);
  QVERIFY(hasPanel(QStringLiteral("synchro.panel.duckdb")));
}

void PeekOverlayTest::fileGridFollowsProxySort() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  QVERIFY(writePng(tmp.filePath(QStringLiteral("z.png"))));
  QVERIFY(writePng(tmp.filePath(QStringLiteral("a.png"))));

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  proxy.setSortRoleName(QStringLiteral("name"));
  proxy.setSortOrder(QStringLiteral("asc"));
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QCOMPARE(proxy.data(proxy.index(0, 0), DirectoryModel::NameRole).toString(),
           QStringLiteral("a.png"));

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
  keys.setGridMode(true);
  window->show();
  QVERIFY(QTest::qWaitForWindowExposed(window));

  auto *grid = window->findChild<QQuickItem *>(QStringLiteral("fileGrid"));
  QVERIFY(grid);
  QVERIFY(QTest::qWaitFor(
      [&] { return visualNamed(grid, QStringLiteral("gridCell")).size() == 2; },
      2000));

  QString name0;
  for (QQuickItem *cell : visualNamed(grid, QStringLiteral("gridCell"))) {
    if (cell->property("rowIndex").toInt() == 0)
      name0 = cell->property("name").toString();
  }
  QCOMPARE(name0, QStringLiteral("a.png"));

  proxy.selectRow(0);
  QCOMPARE(proxy.currentName(), QStringLiteral("a.png"));
  QCOMPARE(model.currentName(), QStringLiteral("a.png"));
}

void PeekOverlayTest::findInFilePastDefaultWindow() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString path = tmp.filePath(QStringLiteral("big.txt"));
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("head token\n") > 0);
    QByteArray pad(70 * 1024, 'x');
    QVERIFY(f.write(pad) == pad.size());
    QVERIFY(f.write("\nFINDME_TOKEN in the tail\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  HandlerLoader loader;
  XdgOpen xdg;
  MimeMap mimeMap;
  QQmlApplicationEngine engine;
  HostApi host(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
               &engine);

  const QUrl url = QUrl::fromLocalFile(path);
  const QVariantList hits =
      host.findInFile(url, QStringLiteral("FINDME_TOKEN"));
  QCOMPARE(hits.size(), 1);
  const QVariantMap hit = hits.at(0).toMap();
  QVERIFY(hit.value(QStringLiteral("offset")).toLongLong() > 65536);
  QCOMPARE(hit.value(QStringLiteral("line")).toInt(), 3);

  const QVariantMap head = host.readPreview(url, 65536, 0);
  QVERIFY(head.value(QStringLiteral("ok")).toBool());
  QVERIFY(!head.value(QStringLiteral("text"))
               .toString()
               .contains(QStringLiteral("FINDME_TOKEN")));

  const QVariantMap mid = host.readPreview(
      url, 65536, hit.value(QStringLiteral("offset")).toLongLong() - 64);
  QVERIFY(mid.value(QStringLiteral("ok")).toBool());
  QVERIFY(mid.value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("FINDME_TOKEN")));
  QVERIFY(mid.value(QStringLiteral("startByte")).toLongLong() > 0);

  const QVariantList both = host.findInFile(url, QStringLiteral("token"));
  QCOMPARE(both.size(), 2);
}

void PeekOverlayTest::textPeekFindCyclesHits() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("alpha zzhit\nbeta\nzzhit again\nnope\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();

  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  const int row = findProxy(proxy, QStringLiteral("notes.txt"));
  QVERIFY(row >= 0);
  proxy.setCurrentIndex(row);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(hostApi.isOpen());
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  QVERIFY(keys.handleListKey(Qt::Key_D, Qt::NoModifier, QStringLiteral("d")));
  QVERIFY(hostApi.peekPreviewFocused());

  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findOpen").toBool(); }, 1000));
  preview->setProperty("findQuery", QStringLiteral("zzhit"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() >= 2; }, 1000));
  QCOMPARE(preview->property("findCount").toInt(), 2);
  QCOMPARE(preview->property("findIndex").toInt(), 0);

  QVariant consumed;
  QVERIFY(QMetaObject::invokeMethod(preview, "peekKey", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, consumed),
                                    Q_ARG(QVariant, int(Qt::Key_N)),
                                    Q_ARG(QVariant, int(Qt::NoModifier))));
  QVERIFY(consumed.toBool());
  QCOMPARE(preview->property("findIndex").toInt(), 1);

  QVERIFY(QMetaObject::invokeMethod(preview, "peekKey", Qt::DirectConnection,
                                    Q_RETURN_ARG(QVariant, consumed),
                                    Q_ARG(QVariant, int(Qt::Key_N)),
                                    Q_ARG(QVariant, int(Qt::ShiftModifier))));
  QVERIFY(consumed.toBool());
  QCOMPARE(preview->property("findIndex").toInt(), 0);

  QVERIFY(keys.handleListKey(Qt::Key_N, Qt::NoModifier, QStringLiteral("n")));
  QCOMPARE(preview->property("findIndex").toInt(), 1);

  QVERIFY(keys.handleListKey(Qt::Key_Escape, Qt::NoModifier, QString()));
  QVERIFY(hostApi.isOpen());
  QVERIFY(!preview->property("findOpen").toBool());
  QCOMPARE(preview->property("findCount").toInt(), 0);
}

void PeekOverlayTest::textPeekFindJumpsPastWindow() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("log.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("start\n") > 0);
    QVERIFY(f.write(QByteArray(70 * 1024, 'x')) == 70 * 1024);
    QVERIFY(f.write("\nTAIL_UNIQUE_HIT here\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("log.txt")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  const QVariantMap head = preview->property("preview").toMap();
  QVERIFY(!head.value(QStringLiteral("text"))
               .toString()
               .contains(QStringLiteral("TAIL_UNIQUE_HIT")));

  preview->setProperty("findQuery", QStringLiteral("TAIL_UNIQUE_HIT"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  QCOMPARE(preview->property("findIndex").toInt(), 0);
  QVERIFY(preview->property("viewStart").toInt() > 0);
  const QVariantMap mid = preview->property("preview").toMap();
  QVERIFY(mid.value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("TAIL_UNIQUE_HIT")));
}

void PeekOverlayTest::textPeekFindKeepsNewlines() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("lines.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("one\ntwo HIT\nthree\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("lines.txt")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  preview->setProperty("findQuery", QStringLiteral("HIT"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  auto *body = preview->findChild<QQuickItem *>(QStringLiteral("textPeekBody"));
  QVERIFY(body);
  QVERIFY(QTest::qWaitFor([&] { return body->implicitHeight() > 48; }, 1000));
  QVERIFY2(body->implicitHeight() > 48,
           qPrintable(QString::number(body->implicitHeight())));
}

void PeekOverlayTest::textPeekFindKeepsSyntaxColors() {
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("sample.py")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("def foo():\n    return \"synchro_hl_token\"\n") > 0);
  }

  DirectoryModel model;
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  proxy.setCurrentIndex(findProxy(proxy, QStringLiteral("sample.py")));

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  const QVariantMap head = preview->property("preview").toMap();
  if (!head.value(QStringLiteral("highlighted")).toBool())
    QSKIP("preview has no syntax HTML");

  QVERIFY(QMetaObject::invokeMethod(preview, "openFind"));
  preview->setProperty("findQuery", QStringLiteral("synchro_hl_token"));
  QVERIFY(QMetaObject::invokeMethod(preview, "runFind"));
  QVERIFY(QTest::qWaitFor(
      [&] { return preview->property("findCount").toInt() == 1; }, 1000));
  auto *body = preview->findChild<QQuickItem *>(QStringLiteral("textPeekBody"));
  QVERIFY(body);
  QVERIFY(QTest::qWaitFor(
      [&] {
        const QString t = body->property("text").toString();
        return t.contains(QStringLiteral("background-color:")) &&
               t.contains(QStringLiteral("color:"));
      },
      1000));
  const QString html = body->property("text").toString();
  QVERIFY2(html.contains(QStringLiteral("color:")), qPrintable(html.left(240)));
  QVERIFY2(html.contains(QStringLiteral("background-color:")),
           qPrintable(html.left(240)));
}

void PeekOverlayTest::contentSearchPeekOpensFind() {
  if (SearchService::executable(SearchService::Kind::Content).isEmpty())
    QSKIP("rg is not available");

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  {
    QFile f(tmp.filePath(QStringLiteral("notes.txt")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(f.write("alpha\nsynchro_deeplink_token here\nbeta\n") > 0);
  }

  DirectoryModel model;
  SearchModel search;
  model.setSearchModel(&search);
  FilterProxy proxy;
  proxy.setDirectoryModel(&model);
  NavStack nav(&model);
  KeyMachine keys(&model, &proxy, &nav);
  keys.setSearchModel(&search);
  MimeMap mimeMap;
  HandlerRegistry registry;
  registry.setFirstPartyDir(QStringLiteral(SYNCHRO_FIRST_PARTY_HANDLER_DIR));
  registry.setUserDir(tmp.filePath(QStringLiteral("no-user-handlers")));
  registry.setConfigPath(tmp.filePath(QStringLiteral("handlers.json")));
  registry.setScanEnv(false);
  registry.scan();
  HandlerLoader loader;
  XdgOpen xdg;
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));

  model.setPath(QStringLiteral("search://"));
  search.start(QStringLiteral("synchro_deeplink_token"), tmp.path(), false,
               true);
  QVERIFY(QTest::qWaitFor(
      [&] { return !search.listing() && search.count() == 1; }, 5000));
  QVERIFY(keys.statusMessage().contains(QStringLiteral("content matches")));

  // Search completion/status is contextual. Returning to a normal folder
  // must not leave the last result count in the browser chrome.
  model.setPath(tmp.path());
  QVERIFY(waitListingDone(model));
  QVERIFY(keys.statusMessage().isEmpty());

  model.setPath(QStringLiteral("search://"));
  QCOMPARE(model.isContentSearch(), true);
  proxy.setCurrentIndex(0);

  QQmlApplicationEngine engine;
  engine.addImportPath(QCoreApplication::applicationDirPath() +
                       QStringLiteral("/qml"));
  ThumbImageProvider::install(&engine);
  HostApi hostApi(&model, &proxy, &nav, &registry, &loader, &xdg, &mimeMap,
                  &engine);
  keys.setPeekHost(&hostApi);
  QCOMPARE(hostApi.peekFindQuery(), QStringLiteral("synchro_deeplink_token"));

  QVERIFY(
      keys.handleListKey(Qt::Key_Space, Qt::NoModifier, QStringLiteral(" ")));
  QVERIFY(
      QTest::qWaitFor([&] { return hostApi.previewItem() != nullptr; }, 3000));
  auto *preview = qobject_cast<QQuickItem *>(hostApi.previewItem());
  QVERIFY(preview);
  QVERIFY(QTest::qWaitFor(
      [&] {
        return preview->property("findOpen").toBool() &&
               preview->property("findQuery").toString() ==
                   QStringLiteral("synchro_deeplink_token") &&
               preview->property("findCount").toInt() >= 1;
      },
      3000));
  QCOMPARE(preview->property("findIndex").toInt(), 0);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  PeekOverlayTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "peek_overlay_test.moc"
