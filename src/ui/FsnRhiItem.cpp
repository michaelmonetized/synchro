#include "FsnRhiItem.h"

#include <QFile>
#include <QMatrix4x4>
#include <QVector3D>
#include <QtQml/qqml.h>
#include <QtMath>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {

void registerFsnRhiType() {
  qmlRegisterType<FsnRhiItem>("Synchro", 1, 0, "FsnRhiView");
}
Q_COREAPP_STARTUP_FUNCTION(registerFsnRhiType)

struct Vertex {
  float x, y, z;
  float r, g, b;
};

QShader shader(const QString &path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly)
             ? QShader::fromSerialized(file.readAll())
             : QShader();
}

QColor colorValue(const QVariantMap &palette, const QString &key,
                  const QColor &fallback) {
  const QVariant value = palette.value(key);
  if (value.canConvert<QColor>()) {
    const QColor color = value.value<QColor>();
    if (color.isValid())
      return color;
  }
  const QColor color(value.toString());
  return color.isValid() ? color : fallback;
}

QColor mixed(const QColor &a, const QColor &b, float amount) {
  const float t = std::clamp(amount, 0.0f, 1.0f);
  return QColor::fromRgbF(a.redF() * (1.0f - t) + b.redF() * t,
                          a.greenF() * (1.0f - t) + b.greenF() * t,
                          a.blueF() * (1.0f - t) + b.blueF() * t, 1.0f);
}

QColor shaded(const QColor &color, float amount) {
  return QColor::fromRgbF(std::clamp(float(color.redF()) * amount, 0.0f, 1.0f),
                          std::clamp(float(color.greenF()) * amount, 0.0f, 1.0f),
                          std::clamp(float(color.blueF()) * amount, 0.0f, 1.0f),
                          1.0f);
}

void appendVertex(std::vector<Vertex> *out, const QVector3D &p,
                  const QColor &color) {
  out->push_back({p.x(), p.y(), p.z(), float(color.redF()),
                  float(color.greenF()), float(color.blueF())});
}

void appendTriangle(std::vector<Vertex> *out, const QVector3D &a,
                    const QVector3D &b, const QVector3D &c,
                    const QColor &color) {
  appendVertex(out, a, color);
  appendVertex(out, b, color);
  appendVertex(out, c, color);
}

void appendLine(std::vector<Vertex> *out, const QVector3D &a,
                const QVector3D &b, const QColor &color) {
  appendVertex(out, a, color);
  appendVertex(out, b, color);
}

QColor primitiveColor(const QVariantMap &prim, const QVariantMap &palette) {
  const int kind = prim.value(QStringLiteral("kind")).toInt();
  const int type = prim.value(QStringLiteral("ntype")).toInt();
  if (kind == 3)
    return colorValue(palette, QStringLiteral("road"), QColor("#5e3547"));
  if (kind == 0) {
    QColor color =
        colorValue(palette, QStringLiteral("platform"), QColor("#34313a"));
    const float score =
        std::clamp(prim.value(QStringLiteral("sizeScore"), 0.5).toFloat(),
                   0.0f, 1.0f);
    color = shaded(color, 0.80f + 0.28f * score);
    const int level = prim.value(QStringLiteral("depthLevel")).toInt();
    return shaded(color, std::max(0.68f, 1.0f - 0.055f * level));
  }
  if (type == 1)
    return colorValue(palette, QStringLiteral("directory"), QColor("#b6a99a"));
  if (type == 3)
    return colorValue(palette, QStringLiteral("symlink"), QColor("#8da7a2"));
  const QString category = prim.value(QStringLiteral("category")).toString();
  if (!category.isEmpty() && palette.contains(category))
    return colorValue(palette, category, QColor("#a99f95"));
  return colorValue(palette, QStringLiteral("file"), QColor("#a99f95"));
}

class FsnRhiRenderer final : public QQuickRhiItemRenderer {
public:
  void initialize(QRhiCommandBuffer *cb) override;
  void synchronize(QQuickRhiItem *item) override;
  void render(QRhiCommandBuffer *cb) override;

private:
  void releasePipelines();
  void rebuildGeometry(const FsnRhiItem *item);
  void rebuildBuffers(QRhiResourceUpdateBatch *updates);
  std::unique_ptr<QRhiGraphicsPipeline> makePipeline(
      QRhiGraphicsPipeline::Topology topology, bool depthWrite);

  QRhi *m_rhi = nullptr;
  QRhiRenderPassDescriptor *m_renderPass = nullptr;
  int m_sampleCount = 1;
  std::unique_ptr<QRhiBuffer> m_triangles;
  std::unique_ptr<QRhiBuffer> m_lines;
  std::unique_ptr<QRhiBuffer> m_uniforms;
  std::unique_ptr<QRhiShaderResourceBindings> m_srb;
  std::unique_ptr<QRhiGraphicsPipeline> m_trianglePipeline;
  std::unique_ptr<QRhiGraphicsPipeline> m_linePipeline;
  std::vector<Vertex> m_triangleData;
  std::vector<Vertex> m_lineData;
  QMatrix4x4 m_mvp;
  QSize m_outputSize;
  quint64 m_geometryRevision = 0;
  bool m_buffersDirty = true;
};

void FsnRhiRenderer::releasePipelines() {
  m_trianglePipeline.reset();
  m_linePipeline.reset();
  m_srb.reset();
  m_uniforms.reset();
  m_triangles.reset();
  m_lines.reset();
  m_buffersDirty = true;
}

std::unique_ptr<QRhiGraphicsPipeline> FsnRhiRenderer::makePipeline(
    QRhiGraphicsPipeline::Topology topology, bool depthWrite) {
  auto pipeline = std::unique_ptr<QRhiGraphicsPipeline>(m_rhi->newGraphicsPipeline());
  pipeline->setShaderStages({
      {QRhiShaderStage::Vertex, shader(QStringLiteral(":/shaders/fsn.vert.qsb"))},
      {QRhiShaderStage::Fragment, shader(QStringLiteral(":/shaders/fsn.frag.qsb"))},
  });
  QRhiVertexInputLayout input;
  input.setBindings({{sizeof(Vertex)}});
  input.setAttributes({
      {0, 0, QRhiVertexInputAttribute::Float3, offsetof(Vertex, x)},
      {0, 1, QRhiVertexInputAttribute::Float3, offsetof(Vertex, r)},
  });
  pipeline->setVertexInputLayout(input);
  pipeline->setShaderResourceBindings(m_srb.get());
  pipeline->setRenderPassDescriptor(m_renderPass);
  pipeline->setSampleCount(m_sampleCount);
  pipeline->setTopology(topology);
  pipeline->setDepthTest(true);
  pipeline->setDepthWrite(depthWrite);
  pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
  pipeline->setCullMode(QRhiGraphicsPipeline::None);
  if (topology == QRhiGraphicsPipeline::Lines)
    pipeline->setLineWidth(1.0f);
  if (!pipeline->create())
    return {};
  return pipeline;
}

void FsnRhiRenderer::initialize(QRhiCommandBuffer *cb) {
  Q_UNUSED(cb)
  QRhiRenderTarget *target = renderTarget();
  if (m_rhi != rhi() || m_renderPass != target->renderPassDescriptor() ||
      m_sampleCount != target->sampleCount()) {
    releasePipelines();
    m_rhi = rhi();
    m_renderPass = target->renderPassDescriptor();
    m_sampleCount = target->sampleCount();
  }
  if (m_trianglePipeline)
    return;

  m_uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic,
                                    QRhiBuffer::UniformBuffer, 64));
  m_uniforms->create();
  m_srb.reset(m_rhi->newShaderResourceBindings());
  m_srb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
      0, QRhiShaderResourceBinding::VertexStage, m_uniforms.get())});
  m_srb->create();
  m_trianglePipeline =
      makePipeline(QRhiGraphicsPipeline::Triangles, true);
  m_linePipeline = makePipeline(QRhiGraphicsPipeline::Lines, false);
}

void FsnRhiRenderer::rebuildGeometry(const FsnRhiItem *item) {
  m_triangleData.clear();
  m_lineData.clear();
  const QVariantMap palette = item->scenePalette();
  const QColor edge =
      colorValue(palette, QStringLiteral("edge"), QColor("#80766f"));
  const QColor selected =
      colorValue(palette, QStringLiteral("selection"), QColor("#8eb7ff"));
  const QColor roadEdge =
      colorValue(palette, QStringLiteral("roadEdge"), edge);
  const QColor platform =
      colorValue(palette, QStringLiteral("platform"), QColor("#34313a"));

  for (const QVariant &value : item->primitives()) {
    const QVariantMap prim = value.toMap();
    const QVariantList baseValues = prim.value(QStringLiteral("base")).toList();
    const QVariantList topValues = prim.value(QStringLiteral("top")).toList();
    const int count = std::min(baseValues.size(), topValues.size()) / 2;
    if (count < 3)
      continue;
    const float y0 = prim.value(QStringLiteral("y0")).toFloat();
    const float y1 = y0 + prim.value(QStringLiteral("h")).toFloat();
    const bool road = prim.value(QStringLiteral("kind")).toInt() == 3;
    const bool rooftop =
        !prim.value(QStringLiteral("ownerPath")).toString().isEmpty();
    const bool isSelected = !item->selectedPath().isEmpty() &&
                            prim.value(QStringLiteral("path")).toString() ==
                                item->selectedPath();
    QColor color = primitiveColor(prim, palette);
    if (isSelected)
      color = mixed(color, selected, 0.58f);
    const QColor outline = isSelected ? selected : (road ? roadEdge : edge);
    std::vector<QVector3D> base;
    std::vector<QVector3D> top;
    base.reserve(count);
    top.reserve(count);
    for (int i = 0; i < count; ++i) {
      base.emplace_back(baseValues.at(2 * i).toFloat(), y0,
                        baseValues.at(2 * i + 1).toFloat());
      // Bridge segments carry their own elevation and thickness so StrataV's
      // stepped paths can descend with the directory hierarchy.
      top.emplace_back(topValues.at(2 * i).toFloat(), y1,
                       topValues.at(2 * i + 1).toFloat());
    }

    // StrataV's nested MapV cells carry category color most strongly on their
    // roof. Quieter, district-colored walls keep a dense folder cohesive while
    // the overhead composition remains legible at a glance.
    const QColor sideBase = rooftop ? mixed(platform, color, 0.60f) : color;
    const QColor topColor =
        shaded(color, road ? 0.92f : (rooftop ? 1.18f : 1.10f));
    for (int i = 1; i + 1 < count; ++i)
      appendTriangle(&m_triangleData, top[0], top[i], top[i + 1], topColor);

    if (!road) {
      const QVector3D light = QVector3D(0.35f, 0.7f, -0.62f).normalized();
      for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        const QVector3D side = top[j] - top[i];
        QVector3D normal = QVector3D::crossProduct(side, QVector3D(0, 1, 0));
        if (!normal.isNull())
          normal.normalize();
        const float amount = 0.66f + 0.24f * std::abs(QVector3D::dotProduct(normal, light));
        const QColor sideColor = shaded(sideBase, amount);
        appendTriangle(&m_triangleData, base[i], base[j], top[j], sideColor);
        appendTriangle(&m_triangleData, base[i], top[j], top[i], sideColor);
      }
    }

    for (int i = 0; i < count; ++i) {
      const int j = (i + 1) % count;
      appendLine(&m_lineData, top[i], top[j], outline);
      if (!road) {
        appendLine(&m_lineData, base[i], base[j], shaded(outline, 0.72f));
        appendLine(&m_lineData, base[i], top[i], outline);
      }
    }
  }
  m_buffersDirty = true;
}

void FsnRhiRenderer::synchronize(QQuickRhiItem *rhiItem) {
  const auto *item = static_cast<FsnRhiItem *>(rhiItem);
  if (m_geometryRevision != item->geometryRevision()) {
    m_geometryRevision = item->geometryRevision();
    rebuildGeometry(item);
  }

  m_outputSize = item->effectiveColorBufferSize();
  const float width = std::max(1, m_outputSize.width());
  const float height = std::max(1, m_outputSize.height());
  const float focal = std::min(width, height) * 1.15f;
  const float fov = qRadiansToDegrees(2.0f * std::atan(height / (2.0f * focal)));
  const float cp = std::cos(item->pitch());
  const float sp = std::sin(item->pitch());
  const float cy = std::cos(item->yaw());
  const float sy = std::sin(item->yaw());
  const QVector3D target(item->cameraX(), item->cameraY(), item->cameraZ());
  const QVector3D forward(cp * sy, -sp, cp * cy);
  const QVector3D up(sp * sy, cp, sp * cy);
  const QVector3D eye = target - forward * item->distance();
  QMatrix4x4 view;
  view.lookAt(eye, target, up);
  QMatrix4x4 projection;
  projection.perspective(fov, width / height, 0.1f, 20000.0f);
  m_mvp = m_rhi->clipSpaceCorrMatrix() * projection * view;
}

void FsnRhiRenderer::rebuildBuffers(QRhiResourceUpdateBatch *updates) {
  m_triangles.reset();
  m_lines.reset();
  if (!m_triangleData.empty()) {
    const quint32 bytes = quint32(m_triangleData.size() * sizeof(Vertex));
    m_triangles.reset(m_rhi->newBuffer(QRhiBuffer::Static,
                                       QRhiBuffer::VertexBuffer, bytes));
    m_triangles->create();
    updates->uploadStaticBuffer(m_triangles.get(), m_triangleData.data());
  }
  if (!m_lineData.empty()) {
    const quint32 bytes = quint32(m_lineData.size() * sizeof(Vertex));
    m_lines.reset(m_rhi->newBuffer(QRhiBuffer::Static,
                                   QRhiBuffer::VertexBuffer, bytes));
    m_lines->create();
    updates->uploadStaticBuffer(m_lines.get(), m_lineData.data());
  }
  m_buffersDirty = false;
}

void FsnRhiRenderer::render(QRhiCommandBuffer *cb) {
  if (!m_rhi || !m_trianglePipeline)
    return;
  QRhiResourceUpdateBatch *updates = m_rhi->nextResourceUpdateBatch();
  updates->updateDynamicBuffer(m_uniforms.get(), 0, 64, m_mvp.constData());
  if (m_buffersDirty)
    rebuildBuffers(updates);
  cb->beginPass(renderTarget(), QColor::fromRgbF(0, 0, 0, 0), {1.0f, 0},
                updates);
  const QSize outputSize = renderTarget()->pixelSize();
  if (m_triangles && !m_triangleData.empty()) {
    cb->setGraphicsPipeline(m_trianglePipeline.get());
    cb->setViewport(
        QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput binding(m_triangles.get(), 0);
    cb->setVertexInput(0, 1, &binding);
    cb->draw(quint32(m_triangleData.size()));
  }
  if (m_lines && m_linePipeline && !m_lineData.empty()) {
    cb->setGraphicsPipeline(m_linePipeline.get());
    cb->setViewport(
        QRhiViewport(0, 0, outputSize.width(), outputSize.height()));
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput binding(m_lines.get(), 0);
    cb->setVertexInput(0, 1, &binding);
    cb->draw(quint32(m_lineData.size()));
  }
  cb->endPass();
}

} // namespace

FsnRhiItem::FsnRhiItem(QQuickItem *parent) : QQuickRhiItem(parent) {
  setAlphaBlending(true);
  setSampleCount(4);
}

QQuickRhiItemRenderer *FsnRhiItem::createRenderer() {
  return new FsnRhiRenderer;
}

QVariantMap FsnRhiItem::projectPoint(float worldX, float worldY,
                                     float worldZ) const {
  const float dx = worldX - m_cameraX;
  const float dy = worldY - m_cameraY;
  const float dz = worldZ - m_cameraZ;
  const float cp = std::cos(m_pitch);
  const float sp = std::sin(m_pitch);
  const float cy = std::cos(m_yaw);
  const float sy = std::sin(m_yaw);
  const float x1 = dx * cy - dz * sy;
  const float z1 = dx * sy + dz * cy;
  const float cameraX = -x1;
  const float cameraY = dy * cp + z1 * sp;
  const float depth = z1 * cp - dy * sp + m_distance;
  if (depth < 0.6f || width() <= 0.0 || height() <= 0.0)
    return {};
  const float focal = float(std::min(width(), height())) * 1.15f;
  return {{QStringLiteral("x"), width() * 0.5 + cameraX * focal / depth},
          {QStringLiteral("y"), height() * 0.5 - cameraY * focal / depth},
          {QStringLiteral("d"), depth}};
}

void FsnRhiItem::setPrimitives(const QVariantList &value) {
  if (m_primitives == value)
    return;
  m_primitives = value;
  ++m_geometryRevision;
  emit primitivesChanged();
  update();
}

void FsnRhiItem::setScenePalette(const QVariantMap &value) {
  if (m_scenePalette == value)
    return;
  m_scenePalette = value;
  ++m_geometryRevision;
  emit scenePaletteChanged();
  update();
}

void FsnRhiItem::setSelectedPath(const QString &value) {
  if (m_selectedPath == value)
    return;
  m_selectedPath = value;
  ++m_geometryRevision;
  emit selectedPathChanged();
  update();
}

void FsnRhiItem::touchCamera(float *member, float value) {
  if (qFuzzyCompare(*member, value))
    return;
  *member = value;
  emit cameraChanged();
  update();
}

void FsnRhiItem::setCameraX(float value) { touchCamera(&m_cameraX, value); }
void FsnRhiItem::setCameraY(float value) { touchCamera(&m_cameraY, value); }
void FsnRhiItem::setCameraZ(float value) { touchCamera(&m_cameraZ, value); }
void FsnRhiItem::setYaw(float value) { touchCamera(&m_yaw, value); }
void FsnRhiItem::setPitch(float value) { touchCamera(&m_pitch, value); }
void FsnRhiItem::setDistance(float value) { touchCamera(&m_distance, value); }
