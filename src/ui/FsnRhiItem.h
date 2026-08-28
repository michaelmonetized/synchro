#pragma once

#include <QColor>
#include <QQuickRhiItem>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// A single batched, backend-neutral 3D surface for StrataV/MapV. QML owns the
// browser interaction and overlays; this item owns depth-tested geometry.
class FsnRhiItem : public QQuickRhiItem {
  Q_OBJECT
  Q_PROPERTY(QVariantList primitives READ primitives WRITE setPrimitives NOTIFY
                 primitivesChanged)
  Q_PROPERTY(QVariantMap scenePalette READ scenePalette WRITE setScenePalette
                 NOTIFY scenePaletteChanged)
  Q_PROPERTY(QString selectedPath READ selectedPath WRITE setSelectedPath
                 NOTIFY selectedPathChanged)
  Q_PROPERTY(float cameraX READ cameraX WRITE setCameraX NOTIFY cameraChanged)
  Q_PROPERTY(float cameraY READ cameraY WRITE setCameraY NOTIFY cameraChanged)
  Q_PROPERTY(float cameraZ READ cameraZ WRITE setCameraZ NOTIFY cameraChanged)
  Q_PROPERTY(float yaw READ yaw WRITE setYaw NOTIFY cameraChanged)
  Q_PROPERTY(float pitch READ pitch WRITE setPitch NOTIFY cameraChanged)
  Q_PROPERTY(float distance READ distance WRITE setDistance NOTIFY cameraChanged)
  Q_PROPERTY(int primitiveCount READ primitiveCount NOTIFY primitivesChanged)

public:
  explicit FsnRhiItem(QQuickItem *parent = nullptr);

  QVariantList primitives() const { return m_primitives; }
  QVariantMap scenePalette() const { return m_scenePalette; }
  QString selectedPath() const { return m_selectedPath; }
  float cameraX() const { return m_cameraX; }
  float cameraY() const { return m_cameraY; }
  float cameraZ() const { return m_cameraZ; }
  float yaw() const { return m_yaw; }
  float pitch() const { return m_pitch; }
  float distance() const { return m_distance; }
  int primitiveCount() const { return m_primitives.size(); }
  quint64 geometryRevision() const { return m_geometryRevision; }

  Q_INVOKABLE QVariantMap projectPoint(float worldX, float worldY,
                                       float worldZ) const;

  void setPrimitives(const QVariantList &value);
  void setScenePalette(const QVariantMap &value);
  void setSelectedPath(const QString &value);
  void setCameraX(float value);
  void setCameraY(float value);
  void setCameraZ(float value);
  void setYaw(float value);
  void setPitch(float value);
  void setDistance(float value);

signals:
  void primitivesChanged();
  void scenePaletteChanged();
  void selectedPathChanged();
  void cameraChanged();

protected:
  QQuickRhiItemRenderer *createRenderer() override;

private:
  void touchCamera(float *member, float value);

  QVariantList m_primitives;
  QVariantMap m_scenePalette;
  QString m_selectedPath;
  float m_cameraX = 0.0f;
  float m_cameraY = 0.0f;
  float m_cameraZ = 32.0f;
  float m_yaw = 0.0f;
  float m_pitch = 0.42f;
  float m_distance = 60.0f;
  quint64 m_geometryRevision = 1;
};
