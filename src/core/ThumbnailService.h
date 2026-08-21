#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QtGlobal>

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
};

class ThumbnailEngine;

class ThumbnailService : public QObject {
  Q_OBJECT

public:
  explicit ThumbnailService(QObject *parent = nullptr);
  ~ThumbnailService() override;

  Q_INVOKABLE void request(const QString &path, qint64 mtime, int sizePx);
  void request(const QVector<ThumbnailJob> &jobs);
  void requestVisible(const QVector<ThumbnailJob> &jobs);
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
  static QString ensureRasterPng(const QString &path, qint64 mtime, int maxEdge);
  // Immediate children only: 2x2, prefer images, at most one video.
  static bool renderFolderMosaic(const QString &dirPath, const QString &dest,
                                 int sizePx);
  static QImage renderFolderMosaicImage(const QString &dirPath, int sizePx);
  static QString packedUrl(const QString &path, qint64 mtime, int sizePx);

signals:
  void thumbnailReady(const QString &path, const QString &url);
  void submitted(const QVector<ThumbnailJob> &jobs, bool exclusive);
  void cancelRequested();
  void thumbnailerDirectoriesChanged(const QStringList &dirs);
  void handlerThumbnailersChanged(const QVector<ExecThumbnailer> &list);

private:
  QThread m_thread;
  ThumbnailEngine *m_engine = nullptr;
};

Q_DECLARE_METATYPE(ThumbnailJob)
Q_DECLARE_METATYPE(QVector<ThumbnailJob>)
Q_DECLARE_METATYPE(ExecThumbnailer)
Q_DECLARE_METATYPE(QVector<ExecThumbnailer>)
