#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

// fsv-style 3D filesystem views (the :fsv easter egg), after fsv 0.9
// (fsv.sourceforge.net), itself an homage to SGI's fsn of Jurassic Park fame.
//
//  - MapV: nested slabs; footprint area proportional to size (sqrt(size)
//    block edges, row-packed from the rear-right corner like fsv's
//    mapv_init_recursive), constant heights (dirs tall, files short),
//    slightly tapered tops.
//  - TreeV: radial rings of annular-sector platforms connected by dark-red
//    roads; files stand on platforms as boxes with height ~ sqrt(size).
//
// World scale: 1 unit == 256 fsv units (the TreeV leaf edge).
class FsnLayout {
public:
  enum View { MapView = 0, TreeVView = 1 };
  enum Kind { KindPlatform = 0, KindLeaf = 1, KindDirLeaf = 2, KindRoad = 3 };
  enum NodeType { TypeDir = 1, TypeFile = 2, TypeSymlink = 3 };

  struct Prim {
    QString name;
    QString path;
    int kind = KindLeaf;
    int ntype = TypeFile;
    bool isDir = false;
    bool root = false;
    qint64 bytes = 0;
    // Base and top outlines in the xz plane, as x0,z0,x1,z1,...
    // Same point count; side walls connect base[i]..base[i+1]..top[i+1]..top[i].
    // The first edge (points 0 -> 1) faces the default camera.
    QVector<float> base;
    QVector<float> top;
    float y0 = 0;
    float h = 0;
    // Ground label (TreeV platforms): white text painted flat on the ground
    // in front of the platform, fsn style.
    bool hasLabel = false;
    float labelX = 0;
    float labelZ = 0;
    float labelAngle = 0; // baseline direction in the xz plane, radians
    float labelSize = 0;  // glyph height, world units
    // Road topology (TreeV platforms): where the inward road meets the
    // platform (gate), where the outward trunk leaves (exit), and the path
    // of the parent platform — enough to route camera flights along roads.
    bool hasRoads = false;
    float gateX = 0;
    float gateZ = 0;
    float exitX = 0;
    float exitZ = 0;
    QString parentPath;
  };

  struct Item {
    QString name;
    QString path;
    bool isDir = false;
    qint64 bytes = 0;
  };

  static constexpr int kMaxNodes = 640;
  static constexpr int kMaxDepth = 4;     // MapV nesting depth
  static constexpr int kMaxChildren = 36; // per directory
  static constexpr int kMaxTreeRings = 2; // platform rings below the root
  static constexpr int kMaxPlatforms = 40;

  static QVector<Prim> build(const QString &root, bool hidden, View view);
  static QVector<Prim> buildFromItems(const QString &rootName,
                                      const QString &rootPath,
                                      const QVector<Item> &items, View view);
  static QVariantList toVariantList(const QVector<Prim> &prims);
};
