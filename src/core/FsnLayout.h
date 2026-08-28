#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

// fsv-style 3D filesystem views (the :fsv easter egg), after fsv 0.9
// (fsv.sourceforge.net), itself an homage to SGI's fsn of Jurassic Park fame.
//
//  - MapV: nested slabs; footprint area proportional to size (sqrt(size)
//    block edges, row-packed from the rear-right corner like fsv's
//    mapv_init_recursive), constant heights (dirs tall, files short),
//    slightly tapered tops.
//  - StrataV: the current folder's directories become deterministic MapV
//    buildings in a road-connected neighborhood. Current-folder files occupy
//    the leftover lots with bounded log-size height; expanded directories show
//    their real MapV contents on the building roof.
//
// World scale retains the old fsv normalization for MapV geometry, while
// StrataV uses bounded neighborhood-scale units.
class FsnLayout {
public:
  enum View {
    MapView = 0,
    StrataVView = 1,
    // Source compatibility for the existing key/config plumbing. The visible
    // browser presentation is StrataV now.
    TreeVView = StrataVView
  };
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
    QString sourceParent;
    QString extension;
    QString mime;
    QString category;
    QString ageBucket;
    qint64 mtime = 0;
    int childCount = 0;
    int fileCount = 0;
    int dirCount = 0;
    bool hidden = false;
    bool isLink = false;
    bool aggregate = false;
    bool expanded = false;
    int depthLevel = 0;
    // Neighborhood-relative, winsorized log-size position. This is visual
    // metadata rather than a replacement for the exact byte count.
    float sizeScore = 0.5f;
    // Base and top outlines in the xz plane, as x0,z0,x1,z1,...
    // Same point count; side walls connect base[i]..base[i+1]..top[i+1]..top[i].
    // The first edge (points 0 -> 1) faces the default camera.
    QVector<float> base;
    QVector<float> top;
    float y0 = 0;
    float h = 0;
    // World-space label anchor for named structures.
    bool hasLabel = false;
    float labelX = 0;
    float labelZ = 0;
    float labelAngle = 0; // baseline direction in the xz plane, radians
    float labelSize = 0;  // glyph height, world units
    // Navigable topology: gates/exits support MapV-style nested routes;
    // neighbors contains the StrataV street graph for top-level districts.
    bool hasRoads = false;
    float gateX = 0;
    float gateZ = 0;
    float exitX = 0;
    float exitZ = 0;
    QString parentPath;
    QStringList neighbors;
    // StrataV rooftop MapV geometry delegates all interaction to this owning
    // top-level district rather than exposing nested objects as selections.
    QString ownerPath;
  };

  struct Item {
    QString name;
    QString path;
    QString parentPath;
    QString extension;
    QString mime;
    QString category;
    QString ageBucket;
    bool isDir = false;
    bool hidden = false;
    bool isLink = false;
    bool aggregate = false;
    bool expanded = false;
    bool previewChildren = false;
    qint64 bytes = 0;
    qint64 mtime = 0;
    int childCount = 0;
    int fileCount = 0;
    int dirCount = 0;
  };

  // Expansion depth is a UI guardrail. Siblings at every opened level are
  // complete: StrataV never samples or silently drops entries from a folder.
  static constexpr int kMaxDepth = 8;

  static QVector<Prim> build(const QString &root, bool hidden, View view);
  static QVector<Prim> build(const QString &root, bool hidden, View view,
                             const QStringList &expandedPaths);
  static QVector<Prim> buildFromItems(const QString &rootName,
                                      const QString &rootPath,
                                      const QVector<Item> &items, View view,
                                      const QStringList &expandedPaths = {});
  static QVariantList toVariantList(const QVector<Prim> &prims);
};
