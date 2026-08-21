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

double centerRadius(const FsnLayout::Prim &p) {
  const Bounds b = boundsOf(p);
  return std::hypot(b.cx(), b.cz());
}

QTemporaryDir *makeTree() {
  auto *tmp = new QTemporaryDir;
  if (!tmp->isValid())
    return tmp;
  QDir(tmp->path()).mkdir(QStringLiteral("nested"));
  writeBytes(tmp->filePath(QStringLiteral("big.bin")), 40000);
  writeBytes(tmp->filePath(QStringLiteral("small.txt")), 20);
  writeBytes(tmp->filePath(QStringLiteral("nested/inner.bin")), 8000);
  writeBytes(tmp->filePath(QStringLiteral("nested/tiny.txt")), 20);
  return tmp;
}

} // namespace

class FsnLayoutTest : public QObject {
  Q_OBJECT

private slots:
  void mapvAreasAndNesting();
  void treevRingsRoadsAndHeights();
};

// MapV, after fsv: footprint area tracks size, heights are constant
// (dirs tall, files short), children sit on their parent's top face.
void FsnLayoutTest::mapvAreasAndNesting() {
  QScopedPointer<QTemporaryDir> tmp(makeTree());
  QVERIFY(tmp->isValid());

  const QVector<FsnLayout::Prim> prims =
      FsnLayout::build(tmp->path(), false, FsnLayout::MapView);
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

// TreeV, after fsv/fsn: the root is a labeled platform, expanded dirs move
// out to the next ring joined by roads, leaves rise as sqrt(size) towers.
void FsnLayoutTest::treevRingsRoadsAndHeights() {
  QScopedPointer<QTemporaryDir> tmp(makeTree());
  QVERIFY(tmp->isValid());

  const QVector<FsnLayout::Prim> prims =
      FsnLayout::build(tmp->path(), false, FsnLayout::TreeVView);
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
  QVERIFY(root);
  QVERIFY(platforms >= 2); // root + nested
  QVERIFY(roads >= 2);     // root's inward trunk + branch to nested

  const FsnLayout::Prim *nested = byName(prims, QStringLiteral("nested"));
  const FsnLayout::Prim *big = byName(prims, QStringLiteral("big.bin"));
  const FsnLayout::Prim *small = byName(prims, QStringLiteral("small.txt"));
  const FsnLayout::Prim *inner = byName(prims, QStringLiteral("inner.bin"));
  QVERIFY(nested);
  QVERIFY(big);
  QVERIFY(small);
  QVERIFY(inner);

  // nested is an expanded platform one ring out from the root.
  QCOMPARE(nested->kind, int(FsnLayout::KindPlatform));
  QVERIFY(centerRadius(*nested) > centerRadius(*root) + 1.0);

  // Road topology for camera flights: platforms know their gate (inner
  // edge), exit (outer edge), and parent platform.
  QVERIFY(root->hasRoads);
  QVERIFY(root->parentPath.isEmpty());
  QVERIFY(nested->hasRoads);
  QCOMPARE(nested->parentPath, root->path);
  const double nestedGateR = std::hypot(nested->gateX, nested->gateZ);
  const double nestedExitR = std::hypot(nested->exitX, nested->exitZ);
  const double rootExitR = std::hypot(root->exitX, root->exitZ);
  QVERIFY(nestedGateR < nestedExitR);
  QVERIFY(nestedGateR > rootExitR); // child ring sits beyond the parent

  // Files stand on their platform; height tracks sqrt(size).
  QCOMPARE(big->kind, int(FsnLayout::KindLeaf));
  QVERIFY(big->h > small->h);
  QVERIFY(std::abs(double(big->y0) - 0.62) < 0.05);
  QVERIFY(big->h > 0.5); // sqrt(40000)/256 ~ 0.78
  QVERIFY(inner->y0 > 0.5f);
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  FsnLayoutTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "fsn_layout_test.moc"
