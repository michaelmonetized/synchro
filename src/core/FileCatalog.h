#pragma once

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

class DirectoryModel;

// Durable metadata catalog for the SQL workbench. DirectoryModel remains the
// source of truth for browsing; this database is a queryable, disposable index.
class FileCatalog : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool indexing READ indexing NOTIFY statusChanged)
  Q_PROPERTY(QString indexedRoot READ indexedRoot NOTIFY statusChanged)
  Q_PROPERTY(int indexedCount READ indexedCount NOTIFY statusChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
  Q_PROPERTY(QStringList fields READ fields CONSTANT)
  Q_PROPERTY(bool analyzing READ analyzing NOTIFY analysisChanged)
  Q_PROPERTY(QString analysisStatus READ analysisStatus NOTIFY analysisChanged)

public:
  explicit FileCatalog(DirectoryModel *model, QObject *parent = nullptr);
  ~FileCatalog() override;

  static QString dbPath();
  // Headless query seam shared by the SQL panel, CLI, and MCP server. It
  // reads only the durable catalog and does not require a GUI model.
  static QVariantMap querySync(const QString &sql, const QString &cwd,
                               const QStringList &selection = {},
                               int maxRows = 200);
  static QString sourceRelationForQuery(const QString &sql);

  bool indexing() const { return m_indexing; }
  QString indexedRoot() const { return m_indexedRoot; }
  int indexedCount() const { return m_indexedCount; }
  QString statusText() const { return m_statusText; }
  bool analyzing() const { return m_analyzing; }
  QString analysisStatus() const { return m_analysisStatus; }
  QStringList fields() const;

  Q_INVOKABLE QVariantMap status() const;
  Q_INVOKABLE quint64 query(const QString &sql, const QString &cwd,
                            const QStringList &selection = {},
                            int maxRows = 200);
  Q_INVOKABLE QString sourceRelation(const QString &sql) const;
  Q_INVOKABLE bool coversTree(const QString &root) const;
  // maxEntries <= 0 means a complete recursive scan. Positive limits remain
  // useful for explicit bounded scans and tests.
  Q_INVOKABLE void scanTree(const QString &root, int maxEntries = 0);
  Q_INVOKABLE void refreshCurrent();
  // Analyze a deliberately bounded image batch below root. Visible thumbnail
  // work fills the same facts table automatically.
  Q_INVOKABLE void analyzeImages(const QString &root, int maxFiles = 500);
  void recordImageFacts(const QString &path, qint64 mtime,
                        const QVariantMap &facts);

signals:
  void queryFinished(quint64 requestId, const QVariantMap &result);
  void statusChanged();
  void analysisChanged();

private:
  void scheduleSnapshot();
  void captureSnapshot();
  void enqueueUpsert(const QStringList &paths);
  void enqueueDelete(const QStringList &paths);
  void restoreScanState();
  void rememberCompleteRoot(const QString &root);
  void setStatus(bool indexing, const QString &root, int count,
                 const QString &text);

  DirectoryModel *m_model = nullptr;
  QTimer m_snapshotTimer;
  QThreadPool m_scanPool;
  QThreadPool m_writerPool;
  QThreadPool m_queryPool;
  QThreadPool m_analysisPool;
  std::shared_ptr<std::atomic_bool> m_scanCancel;
  quint64 m_scanGeneration = 0;
  quint64 m_nextRequest = 0;
  bool m_indexing = false;
  QString m_indexedRoot;
  QStringList m_completeRoots;
  int m_indexedCount = 0;
  QString m_statusText = QStringLiteral("current folder is live");
  bool m_analyzing = false;
  QString m_analysisStatus;
};
