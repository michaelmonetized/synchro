#include "FsnLayout.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <vector>

// Layout constants follow fsv 0.9's geometry.c, normalized so that one world
// unit equals fsv's TREEV_LEAF_NODE_EDGE (256):
//   MapV:  dir height 384 -> 1.5   leaf height 128 -> 0.5
//   TreeV: platform height 158.2 -> 0.62   ring spacing 2048 -> 8
//          leaf height sqrt(bytes)/256     core radius 8192 -> 32

namespace {

constexpr double kPi = 3.14159265358979323846;

// MapV
constexpr double kMapDirHeight = 1.5;
constexpr double kMapLeafHeight = 0.5;
constexpr double kMapRootAspect = 1.2;
constexpr double kMapBorderProportion = 0.01;
constexpr double kMapSpan = 26.0;

// TreeV
constexpr double kTreePlatformHeight = 0.62;
constexpr double kTreeRingSpacing = 8.0;
constexpr double kTreeCoreRadius = 32.0;
constexpr double kTreeCoreGrow = 1.25;
constexpr double kTreeMaxArc = 225.0;
constexpr double kTreeLeafCell = 1.4;
constexpr double kTreePlatformPad = 1.6;
constexpr double kTreeRoadHalf = 0.5;
constexpr double kTreeCurveStep = 6.0; // degrees per arc segment

// Side slant ratios per node type (fsv mapv_side_slant_ratios)
double slantRatio(int ntype) {
  switch (ntype) {
  case FsnLayout::TypeDir:
    return 0.032;
  case FsnLayout::TypeSymlink:
    return 0.333;
  default:
    return 0.064;
  }
}

struct Node {
  QString name;
  QString path;
  bool isDir = false;
  bool isLink = false;
  qint64 bytes = 1;
  bool truncated = false; // dir whose contents were not scanned
  std::vector<Node> kids; // dirs first, then files
};

int nodeType(const Node &n) {
  if (n.isLink)
    return FsnLayout::TypeSymlink;
  return n.isDir ? FsnLayout::TypeDir : FsnLayout::TypeFile;
}

bool skipDirName(const QString &name) {
  return name == QLatin1String(".git") || name == QLatin1String("node_modules") ||
         name == QLatin1String("__pycache__") || name == QLatin1String(".svn") ||
         name == QLatin1String(".hg") || name == QLatin1String("lost+found");
}

// Breadth-first scan so shallow directories fill in before deep ones eat the
// node budget (a DFS would let the first big subtree starve its siblings).
void fillDir(Node *n, int depth, int *budget, bool hidden,
             std::vector<std::pair<Node *, int>> *queue) {
  QDir dir(n->path);
  QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;
  if (hidden)
    filters |= QDir::Hidden;
  const QFileInfoList list =
      dir.entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

  QFileInfoList dirs;
  QFileInfoList files;
  for (const QFileInfo &fi : list) {
    if (fi.isDir() && !fi.isSymLink()) {
      if (!skipDirName(fi.fileName()))
        dirs.append(fi);
    } else {
      files.append(fi);
    }
  }
  // Keep some slots for files even in dir-crowded folders.
  const int fileReserve = std::min<int>(12, files.size());
  const int dirCap = std::min<int>(dirs.size(), FsnLayout::kMaxChildren - fileReserve);

  for (int i = 0; i < dirCap && *budget > 0; ++i) {
    const QFileInfo &fi = dirs.at(i);
    Node d;
    d.name = fi.fileName();
    d.path = fi.absoluteFilePath();
    d.isDir = true;
    d.truncated = true; // cleared if the queue gets to it
    d.bytes = 32768;
    n->kids.push_back(std::move(d));
    --*budget;
  }
  const int dirCount = int(n->kids.size());
  for (const QFileInfo &fi : files) {
    if (int(n->kids.size()) >= FsnLayout::kMaxChildren || *budget <= 0)
      break;
    Node f;
    f.name = fi.fileName();
    f.path = fi.absoluteFilePath();
    f.isLink = fi.isSymLink();
    f.isDir = fi.isDir();
    if (f.isDir) {
      f.truncated = true;
      f.bytes = 32768;
    } else {
      const qint64 sz = fi.size();
      f.bytes = sz > 0 ? sz : 1;
    }
    n->kids.push_back(std::move(f));
    --*budget;
  }
  // Only enqueue once this node's kid vector is final: queued pointers must
  // stay stable.
  if (depth < FsnLayout::kMaxDepth) {
    for (int i = 0; i < dirCount; ++i)
      queue->push_back({&n->kids[i], depth + 1});
  }
}

qint64 sumBytes(Node *n) {
  if (!n->kids.empty()) {
    n->bytes = 0;
    for (Node &k : n->kids)
      n->bytes += sumBytes(&k);
    n->bytes = std::max(n->bytes, qint64(4096));
  }
  return n->bytes;
}

Node walk(const QString &path, bool hidden) {
  Node root;
  const QFileInfo self(path);
  root.name = self.fileName().isEmpty() ? path : self.fileName();
  root.path = self.absoluteFilePath();
  root.isDir = true;
  root.bytes = 4096;

  int budget = FsnLayout::kMaxNodes;
  std::vector<std::pair<Node *, int>> queue;
  queue.push_back({&root, 0});
  for (size_t i = 0; i < queue.size() && budget > 0; ++i) {
    Node *n = queue[i].first;
    n->truncated = false;
    fillDir(n, queue[i].second, &budget, hidden, &queue);
  }
  sumBytes(&root);
  return root;
}

// ---------------------------------------------------------------- primitives

void pushPt(QVector<float> *v, double x, double z) {
  v->push_back(float(x));
  v->push_back(float(z));
}

// Axis-aligned box, first edge on the low-z (camera-facing) side.
FsnLayout::Prim rectPrim(double x0, double z0, double x1, double z1, double y0,
                         double h, double slantK) {
  FsnLayout::Prim p;
  p.y0 = float(y0);
  p.h = float(h);
  const double w = x1 - x0;
  const double d = z1 - z0;
  const double tx = std::clamp(std::min(h, slantK * w), 0.0, 0.45 * w);
  const double tz = std::clamp(std::min(h, slantK * d), 0.0, 0.45 * d);
  pushPt(&p.base, x0, z0);
  pushPt(&p.base, x1, z0);
  pushPt(&p.base, x1, z1);
  pushPt(&p.base, x0, z1);
  pushPt(&p.top, x0 + tx, z0 + tz);
  pushPt(&p.top, x1 - tx, z0 + tz);
  pushPt(&p.top, x1 - tx, z1 - tz);
  pushPt(&p.top, x0 + tx, z1 - tz);
  return p;
}

double deg2rad(double a) { return a * kPi / 180.0; }

// Polar point: theta in degrees, 0 along +x, 90 along +z.
void pushPolar(QVector<float> *v, double r, double thetaDeg) {
  const double a = deg2rad(thetaDeg);
  pushPt(v, r * std::cos(a), r * std::sin(a));
}

// Annular sector between r0..r1 and aLo..aHi degrees. The first edge runs
// along the inner arc from aHi (screen left) to aLo (screen right).
FsnLayout::Prim sectorPrim(double r0, double r1, double aLo, double aHi,
                           double y0, double h, double slantK) {
  FsnLayout::Prim p;
  p.y0 = float(y0);
  p.h = float(h);
  const int segs =
      std::max(1, int(std::ceil((aHi - aLo) / kTreeCurveStep)));
  const double t =
      std::clamp(std::min(h, slantK * std::min(r1 - r0, 8.0)), 0.0,
                 0.45 * (r1 - r0));
  const double dIn = std::min(t / std::max(r0, 0.5) * 180.0 / kPi,
                              0.45 * (aHi - aLo));
  const double dOut = std::min(t / std::max(r1 - t, 0.5) * 180.0 / kPi,
                               0.45 * (aHi - aLo));
  for (int i = 0; i <= segs; ++i) {
    const double a = aHi - (aHi - aLo) * double(i) / segs;
    pushPolar(&p.base, r0, a);
    const double at = (aHi - dIn) - ((aHi - dIn) - (aLo + dIn)) * double(i) / segs;
    pushPolar(&p.top, r0 + t, at);
  }
  for (int i = 0; i <= segs; ++i) {
    const double a = aLo + (aHi - aLo) * double(i) / segs;
    pushPolar(&p.base, r1, a);
    const double at = (aLo + dOut) + ((aHi - dOut) - (aLo + dOut)) * double(i) / segs;
    pushPolar(&p.top, r1 - t, at);
  }
  return p;
}

// Rotated square box on a platform. theta locates the box; halfW spans the
// tangential direction, halfD the radial one. First edge faces inward.
FsnLayout::Prim polarBoxPrim(double r, double thetaDeg, double halfW,
                             double halfD, double y0, double h,
                             double slantK) {
  FsnLayout::Prim p;
  p.y0 = float(y0);
  p.h = float(h);
  const double a = deg2rad(thetaDeg);
  const double rx = std::cos(a), rz = std::sin(a);   // radial (outward)
  const double tx = -std::sin(a), tz = std::cos(a);  // tangential (+theta)
  const double cx = r * rx, cz = r * rz;
  const double t = std::clamp(std::min(h, slantK * 2.0 * std::min(halfW, halfD)),
                              0.0, 0.9 * std::min(halfW, halfD));
  auto corner = [&](QVector<float> *v, double sR, double sT, double inset) {
    const double dR = (halfD - inset) * sR;
    const double dT = (halfW - inset) * sT;
    pushPt(v, cx + rx * dR + tx * dT, cz + rz * dR + tz * dT);
  };
  corner(&p.base, -1, +1, 0); // inner left
  corner(&p.base, -1, -1, 0); // inner right
  corner(&p.base, +1, -1, 0); // outer right
  corner(&p.base, +1, +1, 0); // outer left
  corner(&p.top, -1, +1, t);
  corner(&p.top, -1, -1, t);
  corner(&p.top, +1, -1, t);
  corner(&p.top, +1, +1, t);
  return p;
}

void tagPrim(FsnLayout::Prim *p, const Node &n, int kind) {
  p->name = n.name;
  p->path = n.path;
  p->kind = kind;
  p->ntype = nodeType(n);
  p->isDir = n.isDir;
  p->bytes = n.bytes;
}

// --------------------------------------------------------------------- MapV
// fsv's mapv_init_recursive: children become blocks of area
// (sqrt(size) + border)^2, normalized to the parent's top face area and laid
// out in rows starting at the rear-right corner.

struct MapCtx {
  QVector<FsnLayout::Prim> *out;
  double scale = 1.0; // world units per sqrt(byte)
};

void mapLayoutChildren(MapCtx *ctx, const Node &dir, double x0, double z0,
                       double x1, double z1, double topY, int depth);

void mapEmitNode(MapCtx *ctx, const Node &n, double x0, double z0, double x1,
                 double z1, double y0, int depth, bool isRoot) {
  const bool platform = n.isDir;
  const double h = platform ? kMapDirHeight : kMapLeafHeight;
  FsnLayout::Prim p =
      rectPrim(x0, z0, x1, z1, y0, h, slantRatio(nodeType(n)));
  tagPrim(&p, n,
          platform ? FsnLayout::KindPlatform : FsnLayout::KindLeaf);
  p.root = isRoot;
  ctx->out->push_back(p);
  if (!platform || n.kids.empty() || depth > FsnLayout::kMaxDepth)
    return;
  // Top face, minus the slanted rim.
  const double w = x1 - x0, d = z1 - z0;
  const double tx = std::clamp(std::min(h, 0.032 * w), 0.0, 0.45 * w);
  const double tz = std::clamp(std::min(h, 0.032 * d), 0.0, 0.45 * d);
  mapLayoutChildren(ctx, n, x0 + tx, z0 + tz, x1 - tx, z1 - tz, y0 + h,
                    depth + 1);
}

void mapLayoutChildren(MapCtx *ctx, const Node &dir, double x0, double z0,
                       double x1, double z1, double topY, int depth) {
  double w = x1 - x0, d = z1 - z0;
  if (w <= 0.05 || d <= 0.05 || dir.kids.empty())
    return;
  const double a = kMapBorderProportion * std::sqrt(w * d);
  const double border = std::min(a, std::min(w, d) / 3.0);
  // Trim half a border off the perimeter.
  x0 += 0.5 * border;
  z0 += 0.5 * border;
  w -= border;
  d -= border;
  if (w <= 0.03 || d <= 0.03)
    return;
  const double dirArea = w * d;

  struct Block {
    const Node *node;
    double area;
  };
  std::vector<Block> blocks;
  blocks.reserve(dir.kids.size());
  double total = 0.0;
  for (const Node &k : dir.kids) {
    const double edge =
        std::sqrt(double(std::max(k.bytes, qint64(256)))) * ctx->scale + border;
    Block b{&k, edge * edge};
    total += b.area;
    blocks.push_back(b);
  }
  const double sf = dirArea / total;
  for (Block &b : blocks)
    b.area *= sf;

  // Greedy rows, fsv style: a row closes once its newest block would be
  // deeper than it is wide.
  struct Row {
    int first = 0;
    int count = 0;
    double area = 0.0;
  };
  std::vector<Row> rows;
  Row row;
  for (int i = 0; i < int(blocks.size()); ++i) {
    if (row.count == 0)
      row.first = i;
    row.area += blocks[i].area;
    row.count += 1;
    const double rowDepth = row.area / w;
    const double bw = blocks[i].area / rowDepth;
    if (bw / rowDepth < 1.0) {
      rows.push_back(row);
      row = Row{};
    }
  }
  if (row.count > 0)
    rows.push_back(row);

  // Lay rows from the rear-right corner, advancing toward the front.
  double posZ = z0 + d;
  for (const Row &r : rows) {
    const double rowDepth = std::min(r.area / w, posZ - z0);
    if (rowDepth <= 1e-4)
      break;
    double posX = x0 + w;
    for (int i = r.first; i < r.first + r.count; ++i) {
      const double bw = blocks[i].area / (r.area / w);
      const double bx1 = posX;
      const double bx0 = std::max(x0, posX - bw);
      posX -= bw;
      const double inset = std::min(0.5 * border,
                                    0.3 * std::min(bw, rowDepth));
      const double nx0 = bx0 + inset, nx1 = bx1 - inset;
      const double nz0 = posZ - rowDepth + inset, nz1 = posZ - inset;
      if (nx1 - nx0 <= 1e-3 || nz1 - nz0 <= 1e-3)
        continue;
      mapEmitNode(ctx, *blocks[i].node, nx0, nz0, nx1, nz1, topY, depth,
                  false);
    }
    posZ -= rowDepth;
  }
}

QVector<FsnLayout::Prim> mapBuild(const Node &root) {
  QVector<FsnLayout::Prim> out;
  // Root footprint: area == subtree size, aspect 1.2, scaled to kMapSpan.
  const double rawZ =
      std::sqrt(double(std::max(root.bytes, qint64(4096))) / kMapRootAspect);
  const double rawX = kMapRootAspect * rawZ;
  const double s = kMapSpan / std::max(rawX, rawZ);
  MapCtx ctx{&out, s};
  const double wx = rawX * s, wz = rawZ * s;
  mapEmitNode(&ctx, root, -0.5 * wx, -0.5 * wz, 0.5 * wx, 0.5 * wz, 0.0, 0,
              true);
  return out;
}

// -------------------------------------------------------------------- TreeV
// fsv's treev geometry: expanded directories are annular-sector platforms in
// concentric rings; leaves stand on platforms in a polar grid; dark-red
// roads (trunk, cross-arc, stubs) tie each platform to its children.

double treeLeafHeight(qint64 bytes) {
  const double h = std::sqrt(double(std::max(bytes, qint64(1)))) / 256.0;
  return std::clamp(h, 0.12, 12.0);
}

struct TNode {
  const Node *node = nullptr;
  bool isRoot = false;
  double r0 = 0, depth = 0;
  double arcOwn = 0, arcSub = 0, kidsArc = 0;
  double theta = 90.0;
  int cols = 0, rows = 0;
  double arcLen = 0;
  std::vector<TNode> kids;         // expanded child platforms
  std::vector<const Node *> leaves; // boxes standing on this platform
};

double treeGapDeg(double r) { return 2.0 / std::max(r, 1.0) * 180.0 / kPi; }

// Breadth-first choice of which directories deploy as platforms, so every
// ring-1 directory gets its platform before a deep subtree hogs the budget.
std::vector<const Node *> markExpanded(const Node &root, bool leavesOnly) {
  std::vector<const Node *> out;
  if (leavesOnly)
    return out;
  std::vector<std::pair<const Node *, int>> queue{{&root, 0}};
  int budget = FsnLayout::kMaxPlatforms;
  for (size_t i = 0; i < queue.size(); ++i) {
    const Node *n = queue[i].first;
    const int ring = queue[i].second;
    if (ring >= FsnLayout::kMaxTreeRings)
      continue;
    // Deeper rings: cap platforms per parent so one bushy subtree can't
    // hog the whole deployment budget; the rest stay as gray leaf blocks.
    const int perParent = ring == 0 ? FsnLayout::kMaxPlatforms : 6;
    int taken = 0;
    for (const Node &k : n->kids) {
      if (budget <= 0)
        return out;
      if (taken >= perParent)
        break;
      if (k.isDir && !k.isLink && !k.truncated && !k.kids.empty()) {
        out.push_back(&k);
        --budget;
        ++taken;
        queue.push_back({&k, ring + 1});
      }
    }
  }
  return out;
}

bool isExpanded(const std::vector<const Node *> &expanded, const Node *n) {
  return std::find(expanded.begin(), expanded.end(), n) != expanded.end();
}

TNode treeMeasure(const Node &n, double r0,
                  const std::vector<const Node *> &expanded) {
  TNode t;
  t.node = &n;
  t.r0 = r0;
  for (const Node &k : n.kids) {
    if (isExpanded(expanded, &k)) {
      TNode placeholder;
      placeholder.node = &k; // measured below, once this ring is sized
      t.kids.push_back(placeholder);
    } else {
      t.leaves.push_back(&k);
    }
  }
  const int nl = int(t.leaves.size());
  t.cols = nl > 0 ? std::clamp(int(std::ceil(std::sqrt(nl * 1.7))), 1, nl) : 0;
  t.rows = t.cols > 0 ? (nl + t.cols - 1) / t.cols : 0;
  t.depth = std::max(3.0, t.rows * kTreeLeafCell + kTreePlatformPad);
  t.arcLen = std::max(3.0, t.cols * kTreeLeafCell + kTreePlatformPad);
  t.arcOwn = t.arcLen / std::max(r0, 1.0) * 180.0 / kPi;
  const double r1 = r0 + t.depth + kTreeRingSpacing;
  double sum = 0.0;
  for (TNode &kid : t.kids) {
    kid = treeMeasure(*kid.node, r1, expanded);
    sum += kid.arcSub;
  }
  if (t.kids.size() > 1)
    sum += treeGapDeg(r1) * double(t.kids.size() - 1);
  t.kidsArc = sum;
  t.arcSub = std::max(t.arcOwn, sum);
  return t;
}

void treePlace(TNode *t, double theta) {
  t->theta = theta;
  double cursor = theta + 0.5 * t->kidsArc;
  const double r1 = t->r0 + t->depth + kTreeRingSpacing;
  for (TNode &kid : t->kids) {
    treePlace(&kid, cursor - 0.5 * kid.arcSub);
    cursor -= kid.arcSub + treeGapDeg(r1);
  }
}

void treeGroundLabel(FsnLayout::Prim *p, const QString &name, double r0,
                     double theta, double arcLen, bool isRoot) {
  const int len = std::max(3, int(name.size()));
  const double maxSize = isRoot ? 4.2 : 1.8;
  const double size =
      std::clamp((isRoot ? 1.9 : 1.6) * arcLen / len, 0.55, maxSize);
  const double r = std::max(1.0, r0 - 0.35 - size);
  const double a = deg2rad(theta);
  p->hasLabel = true;
  p->labelX = float(r * std::cos(a));
  p->labelZ = float(r * std::sin(a));
  // Baseline reads left-to-right for a camera looking inward at theta.
  p->labelAngle = float(std::atan2(-std::cos(a), std::sin(a)));
  p->labelSize = float(size);
}

void treeRoad(QVector<FsnLayout::Prim> *out, double rIn, double rOut,
              double aLo, double aHi) {
  if (rOut - rIn <= 1e-3 || aHi - aLo <= 1e-4)
    return;
  FsnLayout::Prim p = sectorPrim(rIn, rOut, aLo, aHi, 0.0, 0.0, 0.0);
  p.kind = FsnLayout::KindRoad;
  out->push_back(p);
}

void treeEmit(QVector<FsnLayout::Prim> *out, const TNode &t,
              const QString &parentPath) {
  const Node &n = *t.node;
  const double aLo = t.theta - 0.5 * t.arcOwn;
  const double aHi = t.theta + 0.5 * t.arcOwn;
  const double rOut = t.r0 + t.depth;

  FsnLayout::Prim plat =
      sectorPrim(t.r0, rOut, aLo, aHi, 0.0, kTreePlatformHeight, 0.032);
  tagPrim(&plat, n, FsnLayout::KindPlatform);
  plat.root = t.isRoot;
  treeGroundLabel(&plat, n.name, t.r0, t.theta, t.arcLen, t.isRoot);
  const double aRad = deg2rad(t.theta);
  plat.hasRoads = true;
  plat.gateX = float(t.r0 * std::cos(aRad));
  plat.gateZ = float(t.r0 * std::sin(aRad));
  plat.exitX = float(rOut * std::cos(aRad));
  plat.exitZ = float(rOut * std::sin(aRad));
  plat.parentPath = parentPath;
  out->push_back(plat);

  // Leaf grid: rows march outward, columns run left (high theta) to right.
  const double innerPad = 0.5 * kTreePlatformPad;
  for (int i = 0; i < int(t.leaves.size()); ++i) {
    const Node &leaf = *t.leaves[i];
    const int row = t.cols > 0 ? i / t.cols : 0;
    const int col = t.cols > 0 ? i % t.cols : 0;
    const int inRow =
        std::min(t.cols, int(t.leaves.size()) - row * t.cols);
    const double r =
        t.r0 + innerPad + (row + 0.5) * kTreeLeafCell;
    const double cellDeg = kTreeLeafCell / std::max(r, 1.0) * 180.0 / kPi;
    const double theta =
        t.theta + (0.5 * double(inRow - 1) - col) * cellDeg;
    FsnLayout::Prim box =
        polarBoxPrim(r, theta, 0.5, 0.5, kTreePlatformHeight,
                     treeLeafHeight(leaf.bytes), slantRatio(nodeType(leaf)));
    tagPrim(&box, leaf,
            leaf.isDir ? FsnLayout::KindDirLeaf : FsnLayout::KindLeaf);
    out->push_back(box);
  }

  // Roads to children: trunk out the back, an arc across, stubs inward.
  if (!t.kids.empty()) {
    const double rArc = rOut + 0.5 * kTreeRingSpacing;
    const double trunkHalf = kTreeRoadHalf / rArc * 180.0 / kPi;
    treeRoad(out, rOut - 0.2, rArc + kTreeRoadHalf, t.theta - trunkHalf,
             t.theta + trunkHalf);
    double kidLo = t.kids.front().theta, kidHi = t.kids.front().theta;
    for (const TNode &kid : t.kids) {
      kidLo = std::min(kidLo, kid.theta);
      kidHi = std::max(kidHi, kid.theta);
    }
    if (t.kids.size() > 1)
      treeRoad(out, rArc - kTreeRoadHalf, rArc + kTreeRoadHalf,
               kidLo - trunkHalf, kidHi + trunkHalf);
    for (const TNode &kid : t.kids) {
      const double stubHalf = kTreeRoadHalf / rArc * 180.0 / kPi;
      treeRoad(out, rArc, kid.r0 + 0.2, kid.theta - stubHalf,
               kid.theta + stubHalf);
      treeEmit(out, kid, n.path);
    }
  }

  // The root's own road runs inward, toward the viewer, under its label.
  if (t.isRoot) {
    const double trunkHalf = kTreeRoadHalf / std::max(t.r0, 1.0) * 180.0 / kPi;
    treeRoad(out, std::max(0.5, t.r0 - 6.5), t.r0 + 0.2,
             t.theta - trunkHalf, t.theta + trunkHalf);
  }
}

QVector<FsnLayout::Prim> treeBuild(const Node &root, bool leavesOnly) {
  QVector<FsnLayout::Prim> out;
  const std::vector<const Node *> expanded = markExpanded(root, leavesOnly);
  double core = kTreeCoreRadius;
  TNode t;
  for (int iter = 0; iter < 8; ++iter) {
    t = treeMeasure(root, core, expanded);
    if (t.arcSub <= kTreeMaxArc)
      break;
    core *= kTreeCoreGrow;
  }
  t.isRoot = true;
  treePlace(&t, 90.0);
  treeEmit(&out, t, QString());
  return out;
}

Node rootFromItems(const QString &rootName, const QString &rootPath,
                   const QVector<FsnLayout::Item> &items) {
  Node root;
  root.name = rootName;
  root.path = rootPath;
  root.isDir = true;
  root.bytes = 0;
  const int cap = 120;
  for (const FsnLayout::Item &it : items) {
    if (int(root.kids.size()) >= cap)
      break;
    Node k;
    k.name = it.name;
    k.path = it.path;
    k.isDir = it.isDir;
    k.truncated = it.isDir;
    k.bytes = std::max(it.bytes, qint64(it.isDir ? 32768 : 1));
    root.kids.push_back(std::move(k));
  }
  std::stable_sort(root.kids.begin(), root.kids.end(),
                   [](const Node &a, const Node &b) {
                     return a.isDir && !b.isDir;
                   });
  for (const Node &k : root.kids)
    root.bytes += k.bytes;
  root.bytes = std::max(root.bytes, qint64(4096));
  return root;
}

} // namespace

QVector<FsnLayout::Prim> FsnLayout::build(const QString &root, bool hidden,
                                          View view) {
  QVector<Prim> out;
  const QFileInfo fi(root);
  if (root.isEmpty() || !fi.exists() || !fi.isDir())
    return out;
  const Node tree = walk(fi.absoluteFilePath(), hidden);
  return view == MapView ? mapBuild(tree) : treeBuild(tree, false);
}

QVector<FsnLayout::Prim> FsnLayout::buildFromItems(const QString &rootName,
                                                   const QString &rootPath,
                                                   const QVector<Item> &items,
                                                   View view) {
  const Node root = rootFromItems(rootName, rootPath, items);
  return view == MapView ? mapBuild(root) : treeBuild(root, true);
}

QVariantList FsnLayout::toVariantList(const QVector<Prim> &prims) {
  QVariantList out;
  out.reserve(prims.size());
  for (const Prim &p : prims) {
    QVariantMap m;
    m.insert(QStringLiteral("name"), p.name);
    m.insert(QStringLiteral("path"), p.path);
    m.insert(QStringLiteral("kind"), p.kind);
    m.insert(QStringLiteral("ntype"), p.ntype);
    m.insert(QStringLiteral("isDir"), p.isDir);
    m.insert(QStringLiteral("root"), p.root);
    m.insert(QStringLiteral("bytes"), QVariant::fromValue(p.bytes));
    m.insert(QStringLiteral("y0"), p.y0);
    m.insert(QStringLiteral("h"), p.h);
    QVariantList base;
    base.reserve(p.base.size());
    for (float v : p.base)
      base.append(double(v));
    QVariantList top;
    top.reserve(p.top.size());
    for (float v : p.top)
      top.append(double(v));
    m.insert(QStringLiteral("base"), base);
    m.insert(QStringLiteral("top"), top);
    if (p.hasLabel) {
      m.insert(QStringLiteral("labelX"), p.labelX);
      m.insert(QStringLiteral("labelZ"), p.labelZ);
      m.insert(QStringLiteral("labelAngle"), p.labelAngle);
      m.insert(QStringLiteral("labelSize"), p.labelSize);
    }
    if (p.hasRoads) {
      m.insert(QStringLiteral("gateX"), p.gateX);
      m.insert(QStringLiteral("gateZ"), p.gateZ);
      m.insert(QStringLiteral("exitX"), p.exitX);
      m.insert(QStringLiteral("exitZ"), p.exitZ);
      m.insert(QStringLiteral("parentPath"), p.parentPath);
    }
    out.append(m);
  }
  return out;
}
