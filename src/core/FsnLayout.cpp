#include "FsnLayout.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

// Layout constants follow fsv 0.9's geometry.c, normalized so that one world
// unit equals fsv's TREEV_LEAF_NODE_EDGE (256):
//   MapV:  dir height 384 -> 1.5   leaf height 128 -> 0.5
//   StrataV: sibling directories become bounded MapV buildings in a
//            deterministic neighborhood; current-folder files occupy its lots.

namespace {

// MapV
constexpr double kMapDirHeight = 1.5;
constexpr double kMapLeafHeight = 0.5;
constexpr double kMapRootAspect = 1.2;
constexpr double kMapBorderProportion = 0.01;
constexpr double kMapSpan = 26.0;

// StrataV neighborhood
constexpr double kStrataBridgeHalf = 0.34;
constexpr int kStrataBridgeSteps = 5;
constexpr double kGoldenAngle = 2.39996322972865332;
constexpr double kDistrictSpacing = 8.4;

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
  QString sourceParent;
  QString extension;
  QString mime;
  QString category;
  QString ageBucket;
  bool isDir = false;
  bool isLink = false;
  bool hidden = false;
  bool aggregate = false;
  bool expanded = false;
  bool previewChildren = false;
  qint64 bytes = 1;
  // Exact catalog/file value for labels and selection metadata. bytes may be
  // raised to a small geometry floor while laying out tiny directories.
  qint64 reportedBytes = -1;
  qint64 mtime = 0;
  int childCount = 0;
  int fileCount = 0;
  int dirCount = 0;
  bool truncated = false; // dir whose contents were not scanned
  std::vector<Node> kids; // dirs first, then files
};

int nodeType(const Node &n) {
  if (n.isLink)
    return FsnLayout::TypeSymlink;
  return n.isDir ? FsnLayout::TypeDir : FsnLayout::TypeFile;
}

// Read one complete directory level. Descendants are loaded only when their
// path is explicitly expanded; completeness is structural rather than a
// global sample budget, matching fsv's expand/collapse semantics.
void fillDir(Node *n, bool hidden, const QSet<QString> &expanded,
             std::vector<Node *> *queue) {
  QDir dir(n->path);
  QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;
  if (hidden)
    filters |= QDir::Hidden;
  const QFileInfoList list =
      dir.entryInfoList(filters, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

  QFileInfoList dirs;
  QFileInfoList files;
  for (const QFileInfo &fi : list) {
    if (fi.isDir() && !fi.isSymLink())
      dirs.append(fi);
    else
      files.append(fi);
  }

  for (const QFileInfo &fi : std::as_const(dirs)) {
    Node d;
    d.name = fi.fileName();
    d.path = fi.absoluteFilePath();
    d.sourceParent = n->path;
    d.isDir = true;
    d.expanded = expanded.contains(d.path);
    d.aggregate = true;
    d.truncated = !d.expanded;
    d.bytes = 32768;
    n->kids.push_back(std::move(d));
  }
  const int dirCount = int(n->kids.size());
  for (const QFileInfo &fi : std::as_const(files)) {
    Node f;
    f.name = fi.fileName();
    f.path = fi.absoluteFilePath();
    f.sourceParent = n->path;
    f.isLink = fi.isSymLink();
    f.isDir = fi.isDir();
    if (f.isDir) {
      f.truncated = true;
      f.bytes = 32768;
    } else {
      const qint64 sz = fi.size();
      f.bytes = sz > 0 ? sz : 1;
      f.reportedBytes = std::max(sz, qint64(0));
    }
    n->kids.push_back(std::move(f));
  }
  // Only enqueue once this node's kid vector is final: queued pointers stay
  // stable while descendant levels are populated.
  for (int i = 0; i < dirCount; ++i) {
    if (n->kids[i].expanded)
      queue->push_back(&n->kids[i]);
  }
}

qint64 sumBytes(Node *n) {
  if (!n->kids.empty()) {
    qint64 visibleBytes = 0;
    for (Node &k : n->kids)
      visibleBytes += sumBytes(&k);
    // Catalog scenes may know a directory's aggregate direct-child weight
    // even when the visual node budget only admits a representative subset.
    n->bytes = std::max({n->bytes, visibleBytes, qint64(4096)});
  }
  return n->bytes;
}

Node walk(const QString &path, bool hidden, const QSet<QString> &expanded) {
  Node root;
  const QFileInfo self(path);
  root.name = self.fileName().isEmpty() ? path : self.fileName();
  root.path = self.absoluteFilePath();
  root.isDir = true;
  root.expanded = true;
  root.bytes = 4096;

  std::vector<Node *> queue{&root};
  for (size_t i = 0; i < queue.size(); ++i) {
    Node *n = queue[i];
    n->truncated = false;
    fillDir(n, hidden, expanded, &queue);
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

void tagPrim(FsnLayout::Prim *p, const Node &n, int kind) {
  p->name = n.name;
  p->path = n.path;
  p->kind = kind;
  p->ntype = nodeType(n);
  p->isDir = n.isDir;
  p->bytes = n.reportedBytes >= 0 ? n.reportedBytes : n.bytes;
  p->sourceParent = n.sourceParent;
  p->extension = n.extension;
  p->mime = n.mime;
  p->category = n.category;
  p->ageBucket = n.ageBucket;
  p->mtime = n.mtime;
  p->childCount = n.childCount;
  p->fileCount = n.fileCount;
  p->dirCount = n.dirCount;
  p->hidden = n.hidden;
  p->isLink = n.isLink;
  p->aggregate = n.aggregate || n.truncated;
  p->expanded = n.expanded;
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

// ------------------------------------------------------------------ StrataV
// The current directory is intentionally absent as geometry. Its child
// directories become MapV buildings in a deterministic neighborhood, linked by
// a minimum-spanning street graph with a few local loops. Root files fill the
// open lots as small structures. This uses all three dimensions without letting
// one ISO/AppImage turn the view into a bar chart.

quint64 stableHash(const QString &value) {
  const QByteArray bytes = value.toUtf8();
  quint64 hash = 1469598103934665603ULL;
  for (const char byte : bytes) {
    hash ^= quint8(byte);
    hash *= 1099511628211ULL;
  }
  return hash;
}

double hashUnit(quint64 hash, int shift) {
  return double((hash >> shift) & 0xffffULL) / 65535.0;
}

double strataFileHeight(qint64 bytes) {
  const double units = double(std::max(bytes, qint64(1))) / 4096.0;
  return std::clamp(0.30 + 0.22 * std::log2(1.0 + units), 0.30, 4.4);
}

struct District {
  const Node *node = nullptr;
  quint64 hash = 0;
  double x = 0, z = 0, y = 0;
  double w = 5, d = 5, h = 1.2;
  double sizeScore = 0.5;
  std::vector<int> links;
};

struct Lot {
  double x = 0, z = 0, halfW = 0, halfD = 0;
};

bool overlapsLot(double x, double z, double halfW, double halfD,
                 const std::vector<Lot> &lots, double gap) {
  for (const Lot &lot : lots) {
    if (std::abs(x - lot.x) < halfW + lot.halfW + gap &&
        std::abs(z - lot.z) < halfD + lot.halfD + gap)
      return true;
  }
  return false;
}

void measureDistrict(District *district) {
  const Node &n = *district->node;
  const double countSignal = std::log2(1.0 + std::max(0, n.childCount));
  const double score = std::clamp(district->sizeScore, 0.0, 1.0);
  // Footprint and height both carry size, but from a sibling-relative log
  // axis. The fixed floor keeps tiny folders useful; the capped ceiling keeps
  // one backup tree from flattening the rest of the neighborhood.
  const double area = 16.0 + 67.0 * std::pow(score, 0.82) +
                      std::clamp(countSignal * 0.55, 0.0, 4.0);
  const double aspect = 0.78 + hashUnit(district->hash, 17) * 0.52;
  district->w = std::sqrt(area * aspect);
  district->d = std::sqrt(area / aspect);
  district->w = std::max(
      district->w,
      std::clamp(2.8 + 0.27 * double(n.name.size()), 4.8, 10.5));
  district->h = 0.72 + 4.05 * std::pow(score, 0.90);
  district->y = 0.10 + hashUnit(district->hash, 39) * 0.46;
}

void scoreDistrictSizes(std::vector<District> *districts) {
  if (districts->empty())
    return;
  if (districts->size() == 1) {
    districts->front().sizeScore = 0.58;
    measureDistrict(&districts->front());
    return;
  }
  std::vector<double> logs;
  logs.reserve(districts->size());
  for (const District &district : *districts)
    logs.push_back(std::log2(double(std::max(district.node->bytes,
                                             qint64(4096)))));
  std::sort(logs.begin(), logs.end());
  const auto percentile = [&](double q) {
    const double position = q * double(logs.size() - 1);
    const int lo = int(std::floor(position));
    const int hi = int(std::ceil(position));
    const double t = position - lo;
    return logs[lo] * (1.0 - t) + logs[hi] * t;
  };
  const double low = percentile(0.10);
  const double high = percentile(0.90);
  const double rawSpread = high - low;
  const double spread = std::max(0.25, rawSpread);
  for (District &district : *districts) {
    const double value =
        std::log2(double(std::max(district.node->bytes, qint64(4096))));
    const double magnitude = std::clamp((value - low) / spread, 0.0, 1.0);
    const auto position = std::lower_bound(logs.begin(), logs.end(), value);
    const double rank = double(position - logs.begin()) /
                        double(std::max<size_t>(1, logs.size() - 1));
    district.sizeScore = rawSpread < 0.25
                             ? 0.5
                             : 0.82 * magnitude + 0.18 * rank;
    measureDistrict(&district);
  }
}

void placeDistricts(std::vector<District> *districts) {
  std::sort(districts->begin(), districts->end(),
            [](const District &a, const District &b) {
              return a.hash < b.hash;
            });
  std::vector<Lot> placed;
  placed.reserve(districts->size());
  for (int i = 0; i < int(districts->size()); ++i) {
    District &district = (*districts)[i];
    const double jitter = (hashUnit(district.hash, 3) - 0.5) * 0.72;
    double angle = double(i) * kGoldenAngle + jitter;
    double radius = i == 0 ? 0.0 : kDistrictSpacing * std::sqrt(double(i));
    for (int attempt = 0; attempt < 96; ++attempt) {
      district.x = radius * std::cos(angle);
      district.z = radius * std::sin(angle);
      if (!overlapsLot(district.x, district.z, 0.5 * district.w,
                       0.5 * district.d, placed, 1.5))
        break;
      radius += 0.85;
      angle += 0.09;
    }
    placed.push_back(
        {district.x, district.z, 0.5 * district.w, 0.5 * district.d});
  }
  if (districts->empty())
    return;
  double minX = 1e18, maxX = -1e18, minZ = 1e18, maxZ = -1e18;
  for (const District &district : *districts) {
    minX = std::min(minX, district.x - 0.5 * district.w);
    maxX = std::max(maxX, district.x + 0.5 * district.w);
    minZ = std::min(minZ, district.z - 0.5 * district.d);
    maxZ = std::max(maxZ, district.z + 0.5 * district.d);
  }
  const double ox = 0.5 * (minX + maxX);
  const double oz = 0.5 * (minZ + maxZ);
  for (District &district : *districts) {
    district.x -= ox;
    district.z -= oz;
  }
}

bool linked(const District &district, int other) {
  return std::find(district.links.begin(), district.links.end(), other) !=
         district.links.end();
}

void addLink(std::vector<District> *districts, int a, int b) {
  if (a == b || linked((*districts)[a], b))
    return;
  (*districts)[a].links.push_back(b);
  (*districts)[b].links.push_back(a);
}

double districtDistance2(const District &a, const District &b) {
  const double dx = b.x - a.x;
  const double dz = b.z - a.z;
  return dx * dx + dz * dz;
}

void connectDistricts(std::vector<District> *districts) {
  const int n = int(districts->size());
  if (n < 2)
    return;
  std::vector<bool> inTree(n, false);
  inTree[0] = true;
  for (int edge = 1; edge < n; ++edge) {
    int bestA = -1, bestB = -1;
    double best = 1e100;
    for (int a = 0; a < n; ++a) {
      if (!inTree[a])
        continue;
      for (int b = 0; b < n; ++b) {
        if (inTree[b])
          continue;
        const double distance = districtDistance2((*districts)[a],
                                                  (*districts)[b]);
        if (distance < best) {
          best = distance;
          bestA = a;
          bestB = b;
        }
      }
    }
    if (bestA < 0)
      break;
    addLink(districts, bestA, bestB);
    inTree[bestB] = true;
  }
  // Sparse nearest-neighbor loops keep the neighborhood from reading as a
  // single branching diagram while retaining an understandable street graph.
  for (int i = 0; i < n; ++i) {
    if ((stableHash((*districts)[i].node->path) % 3ULL) != 0)
      continue;
    int nearest = -1;
    double best = 1e100;
    for (int j = 0; j < n; ++j) {
      if (i == j || linked((*districts)[i], j))
        continue;
      const double distance = districtDistance2((*districts)[i],
                                                (*districts)[j]);
      if (distance < best) {
        best = distance;
        nearest = j;
      }
    }
    if (nearest >= 0)
      addLink(districts, i, nearest);
  }
}

FsnLayout::Prim bridgeSegment(double ax, double az, double bx, double bz,
                              double y) {
  const double dx = bx - ax;
  const double dz = bz - az;
  const double len = std::hypot(dx, dz);
  if (len <= 1e-5)
    return {};
  const double nx = -dz / len * kStrataBridgeHalf;
  const double nz = dx / len * kStrataBridgeHalf;
  FsnLayout::Prim p;
  p.kind = FsnLayout::KindRoad;
  p.y0 = float(y);
  p.h = 0.10f;
  pushPt(&p.base, ax + nx, az + nz);
  pushPt(&p.base, bx + nx, bz + nz);
  pushPt(&p.base, bx - nx, bz - nz);
  pushPt(&p.base, ax - nx, az - nz);
  p.top = p.base;
  return p;
}

void strataBridge(QVector<FsnLayout::Prim> *out, double ax, double az,
                   double ay, double bx, double bz, double by) {
  if (std::hypot(bx - ax, bz - az) <= 1e-4)
    return;
  for (int i = 0; i < kStrataBridgeSteps; ++i) {
    const double t0 = double(i) / kStrataBridgeSteps;
    const double t1 = double(i + 1) / kStrataBridgeSteps;
    const double tm = 0.5 * (t0 + t1);
    out->push_back(bridgeSegment(ax + (bx - ax) * t0,
                                 az + (bz - az) * t0,
                                 ax + (bx - ax) * t1,
                                 az + (bz - az) * t1,
                                 ay + (by - ay) * tm));
  }
}

void roadBetween(QVector<FsnLayout::Prim> *out, const District &a,
                 const District &b) {
  const double dx = b.x - a.x;
  const double dz = b.z - a.z;
  const double distance = std::hypot(dx, dz);
  if (distance <= 1e-4)
    return;
  const double ux = dx / distance, uz = dz / distance;
  const double ta = std::min(0.5 * a.w / std::max(std::abs(ux), 0.01),
                             0.5 * a.d / std::max(std::abs(uz), 0.01));
  const double tb = std::min(0.5 * b.w / std::max(std::abs(ux), 0.01),
                             0.5 * b.d / std::max(std::abs(uz), 0.01));
  const double ax = a.x + ux * ta, az = a.z + uz * ta;
  const double bx = b.x - ux * tb, bz = b.z - uz * tb;
  const double sign = ((a.hash ^ b.hash) & 1ULL) ? 1.0 : -1.0;
  const double bend = std::min(2.8, distance * 0.12) * sign;
  const double mx = 0.5 * (ax + bx) - uz * bend;
  const double mz = 0.5 * (az + bz) + ux * bend;
  const double ay = a.y + 0.08;
  const double by = b.y + 0.08;
  const double my = std::min(ay, by) - 0.04;
  strataBridge(out, ax, az, ay, mx, mz, my);
  strataBridge(out, mx, mz, my, bx, bz, by);
}

void emitDistrict(QVector<FsnLayout::Prim> *out, const District &district,
                  const std::vector<District> &districts) {
  const Node &n = *district.node;
  const double x0 = district.x - 0.5 * district.w;
  const double x1 = district.x + 0.5 * district.w;
  const double z0 = district.z - 0.5 * district.d;
  const double z1 = district.z + 0.5 * district.d;
  FsnLayout::Prim building =
      rectPrim(x0, z0, x1, z1, district.y, district.h, 0.025);
  tagPrim(&building, n, FsnLayout::KindPlatform);
  building.hasLabel = true;
  building.labelX = float(district.x);
  building.labelZ = float(z0 + 0.50);
  building.labelSize = 0.96f;
  building.hasRoads = true;
  building.sizeScore = float(district.sizeScore);
  building.gateX = building.exitX = float(district.x);
  building.gateZ = building.exitZ = float(district.z);
  building.parentPath.clear();
  for (const int link : district.links)
    building.neighbors.append(districts[link].node->path);
  out->push_back(building);

  if (n.kids.empty())
    return;
  const double insetX = std::min(0.18, district.w * 0.04);
  const double insetZ = std::min(0.18, district.d * 0.04);
  const int firstChild = out->size();
  const double scale = std::max(district.w, district.d) /
                       std::sqrt(double(std::max(n.bytes, qint64(4096))));
  MapCtx ctx{out, scale};
  mapLayoutChildren(&ctx, n, x0 + insetX, z0 + insetZ, x1 - insetX,
                    z1 - insetZ, district.y + district.h, 1);
  for (int i = firstChild; i < out->size(); ++i) {
    FsnLayout::Prim &child = (*out)[i];
    child.depthLevel = 1;
    child.ownerPath = n.path;
    if (child.kind == FsnLayout::KindPlatform)
      child.parentPath = child.sourceParent;
  }
}

void emitLooseFiles(QVector<FsnLayout::Prim> *out, const Node &root,
                    const std::vector<District> &districts) {
  std::vector<const Node *> files;
  for (const Node &kid : root.kids) {
    if (!kid.isDir || kid.isLink)
      files.push_back(&kid);
  }
  std::sort(files.begin(), files.end(), [](const Node *a, const Node *b) {
    const quint64 ah = stableHash(a->path);
    const quint64 bh = stableHash(b->path);
    return ah == bh ? a->path < b->path : ah < bh;
  });
  std::vector<Lot> occupied;
  occupied.reserve(districts.size() + files.size());
  double minX = 1e18, maxX = -1e18, minZ = 1e18, maxZ = -1e18;
  for (const District &district : districts) {
    occupied.push_back(
        {district.x, district.z, 0.5 * district.w, 0.5 * district.d});
    minX = std::min(minX, district.x - 0.5 * district.w);
    maxX = std::max(maxX, district.x + 0.5 * district.w);
    minZ = std::min(minZ, district.z - 0.5 * district.d);
    maxZ = std::max(maxZ, district.z + 0.5 * district.d);
  }
  if (districts.empty()) {
    const double half = std::max(5.0, 0.72 * std::sqrt(double(files.size())));
    minX = minZ = -half;
    maxX = maxZ = half;
  } else {
    const double margin =
        std::clamp(3.8 + 0.15 * std::sqrt(double(files.size())), 4.0, 8.5);
    minX -= margin;
    maxX += margin;
    minZ -= margin;
    maxZ += margin;
  }
  const double lotW = std::max(1.0, maxX - minX);
  const double lotD = std::max(1.0, maxZ - minZ);
  for (int i = 0; i < int(files.size()); ++i) {
    const Node &file = *files[i];
    const quint64 hash = stableHash(file.path);
    const double height = strataFileHeight(file.bytes);
    const double half = 0.36 + std::min(0.18, height * 0.022);
    double x = 0, z = 0;
    for (int attempt = 0; attempt < 256; ++attempt) {
      // Two irrational strides produce a stable low-discrepancy scatter over
      // the whole neighborhood. Hash offsets keep similarly named folders
      // from inheriting the same visual pattern.
      const double sequence = double(i + 1) +
                              double(attempt) * double(files.size() + 1);
      const double u = std::fmod(hashUnit(hash, 5) +
                                     sequence * 0.6180339887498948,
                                 1.0);
      const double v = std::fmod(hashUnit(hash, 29) +
                                     sequence * 0.7548776662466927,
                                 1.0);
      x = minX + u * lotW;
      z = minZ + v * lotD;
      if (!overlapsLot(x, z, half, half, occupied, 0.16))
        break;
    }
    const double y = 0.08 + hashUnit(hash, 43) * 0.18;
    FsnLayout::Prim structure =
        rectPrim(x - half, z - half, x + half, z + half, y, height,
                 slantRatio(nodeType(file)));
    tagPrim(&structure, file, FsnLayout::KindLeaf);
    out->push_back(structure);
    occupied.push_back({x, z, half, half});
  }
}

QVector<FsnLayout::Prim> strataBuild(const Node &root) {
  QVector<FsnLayout::Prim> out;
  std::vector<District> districts;
  for (const Node &kid : root.kids) {
    if (!kid.isDir || kid.isLink)
      continue;
    District district;
    district.node = &kid;
    district.hash = stableHash(kid.path);
    districts.push_back(district);
  }
  scoreDistrictSizes(&districts);
  placeDistricts(&districts);
  connectDistricts(&districts);
  for (int i = 0; i < int(districts.size()); ++i) {
    for (const int link : districts[i].links) {
      if (i < link)
        roadBetween(&out, districts[i], districts[link]);
    }
  }
  for (const District &district : districts)
    emitDistrict(&out, district, districts);
  emitLooseFiles(&out, root, districts);
  return out;
}

Node rootFromItems(const QString &rootName, const QString &rootPath,
                   const QVector<FsnLayout::Item> &items,
                   const QSet<QString> &expandedPaths) {
  Node root;
  root.name = rootName;
  root.path = rootPath;
  root.isDir = true;
  root.expanded = true;
  root.bytes = 4096;
  root.category = QStringLiteral("folder");

  QHash<QString, QVector<FsnLayout::Item>> byParent;
  QHash<QString, bool> knownPaths;
  for (const FsnLayout::Item &it : items) {
    knownPaths.insert(it.path, true);
    const QString parent = it.parentPath.isEmpty() ? rootPath : it.parentPath;
    byParent[parent].append(it);
  }

  // Preserve the old flat-item behavior when a synthetic result has no
  // parent information, while attaching any orphaned rows to the root rather
  // than silently losing them.
  for (const FsnLayout::Item &it : items) {
    if (!it.parentPath.isEmpty() && it.parentPath != rootPath &&
        !knownPaths.contains(it.parentPath))
      byParent[rootPath].append(it);
  }

  std::function<Node(const FsnLayout::Item &, int)> makeNode;
  makeNode = [&](const FsnLayout::Item &it, int depth) {
    Node n;
    n.name = it.name;
    n.path = it.path;
    n.sourceParent = it.parentPath;
    n.extension = it.extension;
    n.mime = it.mime;
    n.category = it.category;
    n.ageBucket = it.ageBucket;
    n.isDir = it.isDir;
    n.isLink = it.isLink;
    n.hidden = it.hidden;
    n.aggregate = it.aggregate;
    n.expanded = it.expanded || expandedPaths.contains(it.path);
    n.previewChildren = it.previewChildren;
    n.reportedBytes = std::max(it.bytes, qint64(0));
    n.bytes = std::max(it.bytes, qint64(it.isDir ? 4096 : 1));
    n.mtime = it.mtime;
    n.childCount = it.childCount;
    n.fileCount = it.fileCount;
    n.dirCount = it.dirCount;

    const QVector<FsnLayout::Item> children = byParent.value(it.path);
    if (it.isDir && (n.expanded || n.previewChildren) &&
        depth < FsnLayout::kMaxDepth) {
      for (const FsnLayout::Item &child : children)
        n.kids.push_back(makeNode(child, depth + 1));
    }
    if (n.previewChildren && it.childCount > int(n.kids.size())) {
      Node remainder;
      const int omitted = it.childCount - int(n.kids.size());
      remainder.name = QStringLiteral("+%L1 more").arg(omitted);
      remainder.path = n.path + QStringLiteral("/.synchro-folded");
      remainder.sourceParent = n.path;
      remainder.isDir = true;
      remainder.aggregate = true;
      remainder.truncated = true;
      qint64 visibleBytes = 0;
      for (const Node &child : std::as_const(n.kids))
        visibleBytes += child.bytes;
      remainder.bytes = std::max(qint64(1), n.bytes - visibleBytes);
      remainder.reportedBytes = remainder.bytes;
      remainder.childCount = omitted;
      n.kids.push_back(std::move(remainder));
    }
    n.truncated = it.isDir &&
                  (it.childCount > int(n.kids.size()) ||
                   (depth >= FsnLayout::kMaxDepth && it.childCount > 0));
    n.aggregate = n.aggregate || n.truncated;
    return n;
  };

  const QVector<FsnLayout::Item> top = byParent.value(rootPath);
  for (const FsnLayout::Item &item : top)
    root.kids.push_back(makeNode(item, 1));
  root.childCount = top.size();
  root.truncated = top.size() > qsizetype(root.kids.size());
  root.aggregate = root.truncated;
  sumBytes(&root);
  return root;
}

} // namespace

QVector<FsnLayout::Prim> FsnLayout::build(const QString &root, bool hidden,
                                          View view) {
  return build(root, hidden, view, {});
}

QVector<FsnLayout::Prim> FsnLayout::build(const QString &root, bool hidden,
                                          View view,
                                          const QStringList &expandedPaths) {
  QVector<Prim> out;
  const QFileInfo fi(root);
  if (root.isEmpty() || !fi.exists() || !fi.isDir())
    return out;
  const Node tree = walk(fi.absoluteFilePath(), hidden,
                         QSet<QString>(expandedPaths.begin(),
                                       expandedPaths.end()));
  return view == MapView ? mapBuild(tree) : strataBuild(tree);
}

QVector<FsnLayout::Prim> FsnLayout::buildFromItems(const QString &rootName,
                                                   const QString &rootPath,
                                                   const QVector<Item> &items,
                                                   View view,
                                                   const QStringList &expandedPaths) {
  const QSet<QString> expanded(expandedPaths.begin(), expandedPaths.end());
  const Node root = rootFromItems(rootName, rootPath, items, expanded);
  return view == MapView ? mapBuild(root) : strataBuild(root);
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
    m.insert(QStringLiteral("sourceParent"), p.sourceParent);
    m.insert(QStringLiteral("extension"), p.extension);
    m.insert(QStringLiteral("mime"), p.mime);
    m.insert(QStringLiteral("category"), p.category);
    m.insert(QStringLiteral("ageBucket"), p.ageBucket);
    m.insert(QStringLiteral("mtime"), QVariant::fromValue(p.mtime));
    m.insert(QStringLiteral("childCount"), p.childCount);
    m.insert(QStringLiteral("fileCount"), p.fileCount);
    m.insert(QStringLiteral("dirCount"), p.dirCount);
    m.insert(QStringLiteral("hidden"), p.hidden);
    m.insert(QStringLiteral("isLink"), p.isLink);
    m.insert(QStringLiteral("aggregate"), p.aggregate);
    m.insert(QStringLiteral("expanded"), p.expanded);
    m.insert(QStringLiteral("depthLevel"), p.depthLevel);
    m.insert(QStringLiteral("sizeScore"), p.sizeScore);
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
    }
    if (!p.parentPath.isEmpty())
      m.insert(QStringLiteral("parentPath"), p.parentPath);
    if (!p.neighbors.isEmpty())
      m.insert(QStringLiteral("neighbors"), p.neighbors);
    if (!p.ownerPath.isEmpty())
      m.insert(QStringLiteral("ownerPath"), p.ownerPath);
    out.append(m);
  }
  return out;
}
