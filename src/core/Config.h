#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QProcess;

// ~/.config/synchro/config.json. Unknown future versions are read-only
// so we never clobber a newer file.
class Config : public QObject {
  Q_OBJECT
  Q_PROPERTY(int version READ version CONSTANT)
  Q_PROPERTY(bool writable READ writable CONSTANT)
  Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY
                 showHiddenChanged)
  Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
  Q_PROPERTY(QString sortRole READ sortRole WRITE setSortRole NOTIFY sortChanged)
  Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY
                 sortChanged)
  Q_PROPERTY(QStringList locationChips READ locationChips WRITE setLocationChips
                 NOTIFY locationChipsChanged)
  Q_PROPERTY(QStringList pins READ pins WRITE setPins NOTIFY pinsChanged)
  Q_PROPERTY(QVariantList sqlBookmarks READ sqlBookmarks NOTIFY
                 sqlBookmarksChanged)
  Q_PROPERTY(QString lastPath READ lastPath WRITE setLastPath NOTIFY
                 lastPathChanged)
  Q_PROPERTY(QString panelSide READ panelSide WRITE setPanelSide NOTIFY
                 panelChanged)
  Q_PROPERTY(int panelSize READ panelSize WRITE setPanelSize NOTIFY
                 panelChanged)
  Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY
                 panelChanged)
  Q_PROPERTY(QString panelApp READ panelApp WRITE setPanelApp NOTIFY
                 panelChanged)
  Q_PROPERTY(bool panelLookOpen READ panelLookOpen WRITE setPanelLookOpen NOTIFY
                 panelChanged)
  Q_PROPERTY(double panelLookRatio READ panelLookRatio WRITE setPanelLookRatio
                 NOTIFY panelChanged)
  Q_PROPERTY(int lookSize READ lookSize WRITE setLookSize NOTIFY panelChanged)
  Q_PROPERTY(QString lookSide READ lookSide WRITE setLookSide NOTIFY
                 panelChanged)
  Q_PROPERTY(int gridSize READ gridSize WRITE setGridSize NOTIFY gridSizeChanged)
  Q_PROPERTY(int foregroundThumbnailWorkers READ foregroundThumbnailWorkers
                 NOTIFY indexersChanged)
  Q_PROPERTY(bool foregroundImageFacts READ foregroundImageFacts NOTIFY
                 indexersChanged)
  Q_PROPERTY(bool backgroundCatalogEnabled READ backgroundCatalogEnabled NOTIFY
                 indexersChanged)
  Q_PROPERTY(bool backgroundRecursiveScan READ backgroundRecursiveScan NOTIFY
                 indexersChanged)
  Q_PROPERTY(int backgroundScanIntervalMinutes READ
                 backgroundScanIntervalMinutes NOTIFY indexersChanged)
  Q_PROPERTY(int backgroundWatchDebounceMs READ backgroundWatchDebounceMs
                 NOTIFY indexersChanged)
  Q_PROPERTY(int backgroundWatchNeighborhood READ backgroundWatchNeighborhood
                 NOTIFY indexersChanged)
  Q_PROPERTY(int backgroundMaxWatches READ backgroundMaxWatches NOTIFY
                 indexersChanged)
  Q_PROPERTY(bool semanticImageEmbeddings READ semanticImageEmbeddings NOTIFY
                 indexersChanged)
  Q_PROPERTY(int semanticBatchSize READ semanticBatchSize NOTIFY indexersChanged)
  Q_PROPERTY(int semanticIntervalSeconds READ semanticIntervalSeconds NOTIFY
                 indexersChanged)
  Q_PROPERTY(QString semanticImageModel READ semanticImageModel CONSTANT)
  Q_PROPERTY(QVariantMap currentIndexerStatus READ currentIndexerStatus NOTIFY
                 indexerStatusChanged)
  Q_PROPERTY(bool indexerStatusLoading READ indexerStatusLoading NOTIFY
                 indexerStatusChanged)

public:
  explicit Config(QObject *parent = nullptr);
  explicit Config(const QString &filePath, QObject *parent = nullptr);

  static QString defaultPath();
  static QStringList defaultLocationChips();

  QString filePath() const { return m_path; }
  int version() const { return m_version; }
  bool writable() const { return m_writable; }

  bool showHidden() const { return m_showHidden; }
  QString view() const { return m_view; }
  QString sortRole() const { return m_sortRole; }
  QString sortOrder() const { return m_sortOrder; }
  QStringList locationChips() const { return m_chips; }
  QStringList pins() const { return m_pins; }
  QVariantList sqlBookmarks() const { return m_sqlBookmarks; }
  QString lastPath() const { return m_lastPath; }
  QString panelSide() const { return m_panelSide; }
  int panelSize() const { return m_panelSize; }
  bool panelOpen() const { return m_panelOpen; }
  QString panelApp() const { return m_panelApp; }
  bool panelLookOpen() const { return m_panelLookOpen; }
  double panelLookRatio() const { return m_panelLookRatio; }
  int lookSize() const { return m_lookSize; }
  QString lookSide() const { return m_lookSide; }
  int gridSize() const { return m_gridSize; }
  int foregroundThumbnailWorkers() const {
    return m_foregroundThumbnailWorkers;
  }
  bool foregroundImageFacts() const { return m_foregroundImageFacts; }
  bool backgroundCatalogEnabled() const { return m_backgroundCatalogEnabled; }
  bool backgroundRecursiveScan() const { return m_backgroundRecursiveScan; }
  int backgroundScanIntervalMinutes() const {
    return m_backgroundScanIntervalMinutes;
  }
  int backgroundWatchDebounceMs() const {
    return m_backgroundWatchDebounceMs;
  }
  int backgroundWatchNeighborhood() const {
    return m_backgroundWatchNeighborhood;
  }
  int backgroundMaxWatches() const { return m_backgroundMaxWatches; }
  bool semanticImageEmbeddings() const { return m_semanticImageEmbeddings; }
  int semanticBatchSize() const { return m_semanticBatchSize; }
  int semanticIntervalSeconds() const { return m_semanticIntervalSeconds; }
  QString semanticImageModel() const {
    return QStringLiteral("Qdrant/clip-ViT-B-32-vision");
  }
  QVariantMap currentIndexerStatus() const { return m_indexerStatus; }
  bool indexerStatusLoading() const { return m_indexerStatusLoading; }

  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setView(const QString &view);
  Q_INVOKABLE void setSortRole(const QString &role);
  Q_INVOKABLE void setSortOrder(const QString &order);
  Q_INVOKABLE void setLocationChips(const QStringList &ids);
  Q_INVOKABLE void setPins(const QStringList &paths);
  Q_INVOKABLE QString saveSqlBookmark(const QString &name,
                                      const QString &sql,
                                      const QString &cwd);
  Q_INVOKABLE bool removeSqlBookmark(const QString &id);
  Q_INVOKABLE void setLastPath(const QString &path);
  Q_INVOKABLE void setPanelSide(const QString &side);
  Q_INVOKABLE void setPanelSize(int px);
  Q_INVOKABLE void setPanelOpen(bool open);
  Q_INVOKABLE void setPanelApp(const QString &id);
  Q_INVOKABLE void setPanelLookOpen(bool open);
  Q_INVOKABLE void setPanelLookRatio(double ratio);
  Q_INVOKABLE void setLookSize(int px);
  Q_INVOKABLE void setLookSide(const QString &side);
  Q_INVOKABLE void setGridSize(int px);
  Q_INVOKABLE QVariantMap indexerSettings() const;
  Q_INVOKABLE bool applyIndexerSettings(const QVariantMap &settings);
  Q_INVOKABLE QVariantMap indexerStatus() const;
  Q_INVOKABLE void refreshIndexerStatus();
  static QString normalizePin(const QString &path);

  bool load();
  bool save() const;

signals:
  void showHiddenChanged();
  void viewChanged();
  void sortChanged();
  void locationChipsChanged();
  void pinsChanged();
  void sqlBookmarksChanged();
  void lastPathChanged();
  void panelChanged();
  void gridSizeChanged();
  void indexersChanged();
  void indexerStatusChanged();

private:
  void applyDefaults();

  QString m_path;
  int m_version = 1;
  bool m_writable = true;
  bool m_showHidden = false;
  QString m_view = QStringLiteral("list");
  QString m_sortRole = QStringLiteral("name");
  QString m_sortOrder = QStringLiteral("asc");
  QStringList m_chips;
  QStringList m_pins;
  QVariantList m_sqlBookmarks;
  QString m_lastPath;
  QString m_panelSide = QStringLiteral("bottom");
  int m_panelSize = 260;
  bool m_panelOpen = false;
  QString m_panelApp = QStringLiteral("synchro.panel.terminal");
  bool m_panelLookOpen = true;
  double m_panelLookRatio = 0.34;
  int m_lookSize = 360;
  QString m_lookSide;
  int m_gridSize = 132;
  int m_foregroundThumbnailWorkers = 2;
  bool m_foregroundImageFacts = true;
  bool m_backgroundCatalogEnabled = true;
  bool m_backgroundRecursiveScan = true;
  int m_backgroundScanIntervalMinutes = 360;
  int m_backgroundWatchDebounceMs = 650;
  int m_backgroundWatchNeighborhood = 128;
  int m_backgroundMaxWatches = 2048;
  bool m_semanticImageEmbeddings = false;
  int m_semanticBatchSize = 16;
  int m_semanticIntervalSeconds = 60;
  QVariantMap m_indexerStatus;
  bool m_indexerStatusLoading = false;
  QProcess *m_indexerStatusProcess = nullptr;
};
