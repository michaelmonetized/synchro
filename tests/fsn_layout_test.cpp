#include "FsnLayout.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cmath>

namespace {

bool writeBytes(const QString &path, int n) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  return f.write(QByteArray(n, 'x')) == n;
}

const FsnLayout::Prim *byName(const QVector<FsnLayout::Prim> &prims,
                              const QString &name) {
  for (const FsnLayout::Prim &p : prims) {
    if (p.name == name)
      return &p;
  }
  return nullptr;
}

struct Bounds {
  double minX = 1e18, maxX = -1e18, minZ = 1e18, maxZ = -1e18;
  double w() const { return maxX - minX; }
  double d() const { return maxZ - minZ; }
  double area() const { return w() * d(); }
  double cx() const { return 0.5 * (minX + maxX); }
  double cz() const { return 0.5 * (minZ + maxZ); }
};

Bounds boundsOf(const FsnLayout::Prim &p) {
  Bounds b;
  for (int i = 0; i + 1 < p.base.size(); i += 2) {
    b.minX = std::min(b.minX, double(p.base[i]));
    b.maxX = std::max(b.maxX, double(p.base[i]));
    b.minZ = std::min(b.minZ, double(p.base[i + 1]));
    b.maxZ = std::max(b.maxZ, double(p.base[i + 1]));
  }
  return b;
}

bool containsCenter(const FsnLayout::Prim &parent,
                    const FsnLayout::Prim &child) {
  const Bounds pb = boundsOf(parent);
  const Bounds cb = boundsOf(child);
  return cb.cx() >= pb.minX && cb.cx() <= pb.maxX && cb.cz() >= pb.minZ &&
         cb.cz() <= pb.maxZ && child.y0 >= parent.y0 + parent.h * 0.5f;
}

QTemporaryDir *makeTree() {
  auto *tmp = new QTemporaryDir;
  if (!tmp->isValid())
    return tmp;
  QDir(tmp->path()).mkdir(QStringLiteral("nested"));
  QDir(tmp->path()).mkdir(QStringLiteral("other"));
  writeBytes(tmp->filePath(QStringLiteral("big.bin")), 40000);
  writeBytes(tmp->filePath(QStringLiteral("small.txt")), 20);
  writeBytes(tmp->filePath(QStringLiteral("nested/inner.bin")), 8000);
  writeBytes(tmp->filePath(QStringLiteral("nested/tiny.txt")), 20);
  writeBytes(tmp->filePath(QStringLiteral("other/note.md")), 400);
  return tmp;
}

} // namespace

class FsnLayoutTest : public QObject {
  Q_OBJECT

private slots:
  void mapvAreasAndNesting();
  void stratavNeighborhoodMapBuildings();
  void catalogItemsRetainHierarchyAndMetadata();
  void widePartialCatalogTreeIsRepresentedHonestly();
};

// MapV, after fsv: footprint area tracks size, heights are constant
// (dirs tall, files short), children sit on their parent's top face.
void FsnLayoutTest::mapvAreasAndNesting() {
  QScopedPointer<QTemporaryDir> tmp(makeTree());
  QVERIFY(tmp->isValid());

  const QVector<FsnLayout::Prim> prims =
      FsnLayout::build(tmp->path(), false, FsnLayout::MapView,
                       {tmp->filePath(QStringLiteral("nested"))});
  QVERIFY(prims.size() >= 5);
  const FsnLayout::Prim *root = &prims.constFirst();
  QVERIFY(root->root);
  QVERIFY(root->isDir);
  QCOMPARE(root->kind, int(FsnLayout::KindPlatform));

  const FsnLayout::Prim *nested = byName(prims, QStringLiteral("nested"));
  const FsnLayout::Prim *big = byName(prims, QStringLiteral("big.bin"));
  const FsnLayout::Prim *small = byName(prims, QStringLiteral("small.txt"));
  const FsnLayout::Prim *inner = byName(prims, QStringLiteral("inner.bin"));
  QVERIFY(nested);
  QVERIFY(big);
  QVERIFY(small);
  QVERIFY(inner);

  // Size shows as footprint, not height (fsv mapv: dir 384, leaf 128).
  const double bigArea = boundsOf(*big).area();
  const double smallArea = boundsOf(*small).area();
  QVERIFY2(bigArea > smallArea * 1.4,
           qPrintable(QStringLiteral("big %1 small %2")
                          .arg(bigArea)
                          .arg(smallArea)));
  QCOMPARE(big->h, small->h);
  QVERIFY(nested->h > big->h);

  QVERIFY(containsCenter(*nested, *inner));
  QVERIFY(containsCenter(*root, *nested));
  QVERIFY(containsCenter(*root, *big));

  // The top face tapers inward (fsv side slants), never outward.
  const Bounds bb = boundsOf(*big);
  Bounds tb;
  for (int i = 0; i + 1 < big->top.size(); i += 2) {
    tb.minX = std::min(tb.minX, double(big->top[i]));
    tb.maxX = std::max(tb.maxX, double(big->top[i]));
    tb.minZ = std::min(tb.minZ, double(big->top[i + 1]));
    tb.maxZ = std::max(tb.maxZ, double(big->top[i + 1]));
  }
  QVERIFY(tb.w() <= bb.w() + 1e-4);
  QVERIFY(tb.d() <= bb.d() + 1e-4);

  // MapV has no roads and no ground labels.
  for (const FsnLayout::Prim &p : prims) {
    QVERIFY(p.kind != FsnLayout::KindRoad);
    QVERIFY(!p.hasLabel);
  }
}

// StrataV: the root itself disappears. Its folders become road-connected MapV
// buildings while current-folder files occupy bounded-height loose lots.
void FsnLayoutTest::stratavNeighborhoodMapBuildings() {
  QScopedPointer<QTemporaryDir> tmp(makeTree());
  QVERIFY(tmp->isValid());

  const QVector<FsnLayout::Prim> prims =
      FsnLayout::build(tmp->path(), false, FsnLayout::StrataVView,
                       {tmp->filePath(QStringLiteral("nested"))});
  QVERIFY(prims.size() >= 5);

  const FsnLayout::Prim *root = nullptr;
  int platforms = 0;
  int roads = 0;
  for (const FsnLayout::Prim &p : prims) {
    if (p.kind == FsnLayout::KindPlatform) {
      ++platforms;
      if (p.root)
        root = &p;
      QVERIFY(p.hasLabel);
      QVERIFY(p.labelSize > 0);
    } else if (p.kind == FsnLayout::KindRoad) {
      ++roads;
    }
  }
  QVERIFY(!root);
  QVERIFY(platforms >= 2); // nested + other neighborhood buildings
  QVERIFY(roads >= 10);    // one bent street, emitted in stepped segments

  const FsnLayout::Prim *nested = byName(prims, QStringLiteral("nested"));
  const FsnLayout::Prim *big = byName(prims, QStringLiteral("big.bin"));
  const FsnLayout::Prim *small = byName(prims, QStringLiteral("small.txt"));
  const FsnLayout::Prim *inner = byName(prims, QStringLiteral("inner.bin"));
  const FsnLayout::Prim *other = byName(prims, QStringLiteral("other"));
  QVERIFY(nested);
  QVERIFY(other);
  QVERIFY(big);
  QVERIFY(small);
  QVERIFY(inner);

  // Root folders are stable neighborhood buildings, not children of a root
  // plaza. The street graph is available for camera routing.
  QCOMPARE(nested->kind, int(FsnLayout::KindPlatform));
  QVERIFY(nested->hasRoads);
  QVERIFY(nested->parentPath.isEmpty());
  QVERIFY(nested->neighbors.contains(other->path));
  QVERIFY(other->neighbors.contains(nested->path));

  // Loose root files retain size ordering, but logarithmic bounded height
  // prevents one giant file from dwarfing the entire neighborhood.
  QCOMPARE(big->kind, int(FsnLayout::KindLeaf));
  QVERIFY(big->h > small->h);
  QVERIFY(big->h < 6.3f);

  // Expanding nested reuses MapV packing on that building's roof.
  QVERIFY(inner->y0 >= nested->y0 + nested->h - 0.01f);
  QCOMPARE(inner->h, 0.5f);
}

void FsnLayoutTest::catalogItemsRetainHierarchyAndMetadata() {
  const QString root = QStringLiteral("/indexed");
  QVector<FsnLayout::Item> items;
  FsnLayout::Item project;
  project.name = QStringLiteral("project");
  project.path = root + QStringLiteral("/project");
  project.parentPath = root;
  project.isDir = true;
  project.bytes = 50000;
  project.childCount = 1;
  project.dirCount = 1;
  project.aggregate = true;
  project.previewChildren = true;
  items.append(project);

  FsnLayout::Item notes;
  notes.name = QStringLiteral("notes");
  notes.path = root + QStringLiteral("/notes");
  notes.parentPath = root;
  notes.isDir = true;
  notes.bytes = 5000;
  notes.childCount = 1;
  notes.fileCount = 1;
  notes.aggregate = true;
  items.append(notes);

  FsnLayout::Item src;
  src.name = QStringLiteral("src");
  src.path = project.path + QStringLiteral("/src");
  src.parentPath = project.path;
  src.isDir = true;
  src.bytes = 30000;
  src.childCount = 1;
  src.fileCount = 1;
  src.aggregate = true;
  items.append(src);

  FsnLayout::Item code;
  code.name = QStringLiteral("main.cpp");
  code.path = src.path + QStringLiteral("/main.cpp");
  code.parentPath = src.path;
  code.extension = QStringLiteral("cpp");
  code.category = QStringLiteral("code");
  code.ageBucket = QStringLiteral("week");
  code.bytes = 30000;
  code.mtime = 1234;
  items.append(code);

  const QVector<FsnLayout::Prim> prims = FsnLayout::buildFromItems(
      QStringLiteral("indexed"), root, items, FsnLayout::StrataVView,
      {project.path, src.path});
  const FsnLayout::Prim *projectPrim = byName(prims, project.name);
  const FsnLayout::Prim *notesPrim = byName(prims, notes.name);
  const FsnLayout::Prim *srcPrim = byName(prims, src.name);
  const FsnLayout::Prim *codePrim = byName(prims, code.name);
  QVERIFY(projectPrim);
  QVERIFY(notesPrim);
  QVERIFY(srcPrim);
  QVERIFY(codePrim);
  QCOMPARE(projectPrim->kind, int(FsnLayout::KindPlatform));
  QVERIFY(projectPrim->sizeScore > notesPrim->sizeScore);
  QVERIFY(projectPrim->h > notesPrim->h);
  QVERIFY(boundsOf(*projectPrim).area() > boundsOf(*notesPrim).area());
  QCOMPARE(srcPrim->kind, int(FsnLayout::KindPlatform));
  QCOMPARE(srcPrim->parentPath, project.path);
  QCOMPARE(srcPrim->ownerPath, project.path);
  QCOMPARE(codePrim->ownerPath, project.path);
  QCOMPARE(codePrim->sourceParent, src.path);
  QCOMPARE(codePrim->category, QStringLiteral("code"));
  QCOMPARE(codePrim->ageBucket, QStringLiteral("week"));
  QCOMPARE(codePrim->mtime, qint64(1234));
  QCOMPARE(srcPrim->childCount, 1);
  QVERIFY(srcPrim->aggregate);

  // A collapsed district still gets one MapV preview level, but that geometry
  // is owned by the district and does not recursively expose grandchildren.
  const QVector<FsnLayout::Prim> collapsedPreview =
      FsnLayout::buildFromItems(QStringLiteral("indexed"), root, items,
                                FsnLayout::StrataVView);
  const FsnLayout::Prim *previewSrc = byName(collapsedPreview, src.name);
  QVERIFY(previewSrc);
  QCOMPARE(previewSrc->ownerPath, project.path);
  QVERIFY(!byName(collapsedPreview, code.name));
}

void FsnLayoutTest::widePartialCatalogTreeIsRepresentedHonestly() {
  const QString root = QStringLiteral("/wide");
  QVector<FsnLayout::Item> items;
  FsnLayout::Item partial;
  partial.name = QStringLiteral("large-folder");
  partial.path = root + QStringLiteral("/large-folder");
  partial.parentPath = root;
  partial.isDir = true;
  partial.bytes = 1024 * 1024 * 1024LL;
  partial.childCount = 900;
  partial.fileCount = 899;
  partial.dirCount = 1;
  partial.aggregate = true;
  items.append(partial);

  FsnLayout::Item visibleChild;
  visibleChild.name = QStringLiteral("visible-child.txt");
  visibleChild.path = partial.path + QStringLiteral("/visible-child.txt");
  visibleChild.parentPath = partial.path;
  visibleChild.bytes = 2048;
  items.append(visibleChild);

  // This is deliberately wider than the old 36-child cap.
  for (int i = 0; i < 96; ++i) {
    FsnLayout::Item file;
    file.name = QStringLiteral("file-%1.bin").arg(i);
    file.path = root + QLatin1Char('/') + file.name;
    file.parentPath = root;
    file.bytes = (i + 1) * 1024;
    items.append(file);
  }

  FsnLayout::Item tenMiB;
  tenMiB.name = QStringLiteral("ten-mib.bin");
  tenMiB.path = root + QLatin1Char('/') + tenMiB.name;
  tenMiB.parentPath = root;
  tenMiB.bytes = 10 * 1024 * 1024;
  items.append(tenMiB);
  FsnLayout::Item oneGiB = tenMiB;
  oneGiB.name = QStringLiteral("one-gib.bin");
  oneGiB.path = root + QLatin1Char('/') + oneGiB.name;
  oneGiB.bytes = 1024 * 1024 * 1024LL;
  items.append(oneGiB);

  const QVector<FsnLayout::Prim> collapsed = FsnLayout::buildFromItems(
      QStringLiteral("wide"), root, items, FsnLayout::StrataVView);
  const FsnLayout::Prim *folder = byName(collapsed, partial.name);
  const FsnLayout::Prim *child = byName(collapsed, visibleChild.name);
  const FsnLayout::Prim *smallTower = byName(collapsed, tenMiB.name);
  const FsnLayout::Prim *largeTower = byName(collapsed, oneGiB.name);
  QVERIFY(folder);
  QVERIFY(!child);
  QVERIFY(smallTower);
  QVERIFY(largeTower);
  // A collapsed directory remains a branch platform. Expansion controls its
  // visible contents, not whether it participates in StrataV topology.
  QCOMPARE(folder->kind, int(FsnLayout::KindPlatform));
  QVERIFY(folder->hasRoads);
  QCOMPARE(folder->childCount, 900);
  QVERIFY(largeTower->h > smallTower->h + 0.5f);
  QVERIFY(largeTower->h <= 4.4f);
  for (int i = 0; i < 96; ++i)
    QVERIFY(byName(collapsed, QStringLiteral("file-%1.bin").arg(i)));

  // Opening one folder keeps that branch platform in place and reveals its
  // complete immediate level without disturbing root siblings.
  const QVector<FsnLayout::Prim> expanded = FsnLayout::buildFromItems(
      QStringLiteral("wide"), root, items, FsnLayout::StrataVView,
      {partial.path});
  folder = byName(expanded, partial.name);
  child = byName(expanded, visibleChild.name);
  QVERIFY(folder);
  QVERIFY(child);
  QCOMPARE(folder->kind, int(FsnLayout::KindPlatform));
  QVERIFY(folder->expanded);
  QCOMPARE(child->sourceParent, partial.path);
  for (int i = 0; i < 96; ++i)
    QVERIFY(byName(expanded, QStringLiteral("file-%1.bin").arg(i)));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  FsnLayoutTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "fsn_layout_test.moc"
