#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>

#include <atomic>

struct ExecThumbnailer {
  QStringList mimes;
  QString exec;
  QString tryExec;
};

struct ThumbnailJob {
  QString path;
  QString mime;
  qint64 mtime = 0;
  int sizePx = 128;
  int priority = 0;
  QStringList mosaicPaths;
  QString mosaicLabel;
};

struct ThumbnailResult {
  QString path;
  QString url;
};

class ThumbnailEngine;

class ThumbnailService : public QObject {
  Q_OBJECT

public:
  explicit ThumbnailService(QObject *parent = nullptr);
  ~ThumbnailService() override;

  Q_INVOKABLE void request(const QString &path, qint64 mtime, int sizePx);
  void request(const QVector<ThumbnailJob> &jobs);
  // Non-exclusive worker submission for refresh/stat races. Unlike request(),
  // cache probing never runs on the caller thread and warm hits are batched.
  void requestBackground(const QVector<ThumbnailJob> &jobs);
  void requestVisible(const QVector<ThumbnailJob> &jobs);
  void invalidate(const QString &path);
  void cancelAll();
  void setThumbnailerDirectories(const QStringList &dirs);
  void setHandlerThumbnailers(const QVector<ExecThumbnailer> &list);

  static QString xdgCacheHome();
  static QString synchroThumbsDir();
  static QString canonicalPath(const QString &path);
  static QString canonicalFileUri(const QString &path);
  static QString md5Hex(const QByteArray &data);
  static QString synchroThumbPath(const QString &path, qint64 mtime,
                                  int sizePx);
  static QString xdgSizeDir(int sizePx);
  static QString xdgThumbPath(const QString &path, int sizePx);
  static QHash<QString, QString> readPngText(const QString &pngPath);
  static bool isValidXdgThumbnail(const QString &pngPath, const QString &uri,
                                  qint64 mtimeSec);
  static QStringList splitExec(const QString &exec);
  static QString expandFieldCodes(const QString &token,
                                  const QString &inputPath, const QString &uri,
                                  const QString &outputPath, int sizePx);
  static QStringList parseExec(const QString &exec, const QString &inputPath,
                               const QString &outputPath, int sizePx);
  static QString fileUrl(const QString &path);
  // Decode via Qt plugins, then libwebp (this distro ships no Qt WebP plugin).
  static QImage decodeRaster(const QString &path, int maxEdge = 0);
  // Cheap, deterministic visual facts derived from pixels already decoded for
  // a thumbnail. The returned values are intentionally local and versionable;
  // no MIME sniff, model, or network call is involved.
  static QVariantMap deterministicImageFacts(const QString &path,
                                              const QImage &image);
  static QString ensureRasterPng(const QString &path, qint64 mtime, int maxEdge);
  // Immediate children, with progressively denser layouts up to ten tiles.
  static bool renderFolderMosaic(const QString &dirPath, const QString &dest,
                                 int sizePx);
  static QImage renderFolderMosaicImage(const QString &dirPath, int sizePx);
  static QImage renderPathMosaicImage(const QStringList &paths,
                                      const QString &label, int sizePx);
  static QString packedUrl(const QString &path, qint64 mtime, int sizePx);
  // Export an already-cached thumbnail as a short-lived file URL for external
  // QML processes such as the Omarchy shell. This never generates a preview.
  static QString cachedFileUrl(const QString &path, qint64 mtime, int sizePx);
  // Raw image/video thumbnails are content-derived and survive theme changes;
  // folder mosaics and generated file cards contain palette colors.
  static bool thumbnailDependsOnTheme(const QString &path,
                                      const QString &mime, bool isDir);
  static void invalidateThemeCache();

signals:
  void thumbnailReady(const QString &path, const QString &url);
  // Warm viewport hits arrive together so the model can publish one compact
  // update instead of repainting once for every cached tile.
  void thumbnailsReady(const QVector<ThumbnailResult> &results);
  void imageFactsReady(const QString &path, qint64 mtime,
                       const QVariantMap &facts);
  void submitted(const QVector<ThumbnailJob> &jobs, bool exclusive);
  void cancelRequested();
  void invalidateRequested(const QString &path);
  void thumbnailerDirectoriesChanged(const QStringList &dirs);
  void handlerThumbnailersChanged(const QVector<ExecThumbnailer> &list);

private:
  QString displayUrl(const QString &path, const QString &url) const;

  QThread m_thread;
  ThumbnailEngine *m_engine = nullptr;
  QHash<QString, quint64> m_displayRevisions;
  // Scrollbar drags can enqueue many intermediate viewport pages while the
  // worker is still probing the packed cache. Only the newest page matters.
  std::atomic<quint64> m_visibleGeneration{0};
};

Q_DECLARE_METATYPE(ThumbnailJob)
Q_DECLARE_METATYPE(QVector<ThumbnailJob>)
Q_DECLARE_METATYPE(ThumbnailResult)
Q_DECLARE_METATYPE(QVector<ThumbnailResult>)
Q_DECLARE_METATYPE(ExecThumbnailer)
Q_DECLARE_METATYPE(QVector<ExecThumbnailer>)
