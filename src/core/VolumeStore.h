#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <functional>

class QFileSystemWatcher;
class QTimer;

// User-facing mounts. Not GVFS: /proc/self/mountinfo + statvfs + sysfs.
class VolumeStore : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList extraChips READ extraChips NOTIFY changed)

public:
  struct Volume {
    QString label;
    QString mountPoint;
    QString source;
    QString fstype;
    QString device;
    qint64 total = 0;
    qint64 free = 0;
    qint64 used = 0;
    bool removable = false;
    bool extra = false;

    bool operator==(const Volume &other) const = default;
  };

  using ScanHook = std::function<QVector<Volume>()>;
  using EjectHook = std::function<bool(const Volume &, QString *error)>;

  static VolumeStore &instance();

  QVector<Volume> volumes() const { return m_volumes; }
  QVariantList extraChips() const;
  Volume containing(const QString &path) const;
  Volume extraRoot(const QString &path) const;
  Volume findMount(const QString &mountPoint) const;
  bool eject(const QString &target, QString *error);

  void refresh();
  void startWatching();
  void setScanHook(ScanHook hook);
  void setEjectHook(EjectHook hook);
  void setInventoryForTest(const QVector<Volume> &vols);

  static QString formatBytes(qint64 n);
  static QString detailText(const Volume &v);
  static QString chipLabel(const Volume &v);
  static QVector<Volume> parseMountinfo(const QString &text,
                                        const QString &homePath);
  static bool isIgnoredFs(const QString &fstype);
  static bool isIgnoredMount(const QString &mount);
  static bool looksLikeDiskFs(const QString &fstype);
  static QString unescapeMountField(const QString &field);

signals:
  void changed();

private:
  explicit VolumeStore(QObject *parent = nullptr);
  QVector<Volume> scanLive() const;
  static void fillStat(Volume *v);
  static bool sysfsRemovable(int major, int minor);
  static QString blockDevice(int major, int minor);
  static QString volumeLabel(const Volume &v, const QString &homePath,
                             const QString &homeMount);
  bool ejectLive(const Volume &v, QString *error);

  QVector<Volume> m_volumes;
  ScanHook m_scanHook;
  EjectHook m_ejectHook;
  QFileSystemWatcher *m_watch = nullptr;
  QTimer *m_debounce = nullptr;
  bool m_watching = false;
};
