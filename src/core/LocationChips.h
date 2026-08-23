#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Config;
class DirectoryModel;
class HandlerRegistry;
class NavStack;

// Location chips from first-party manifests + config.locationChips.
// path runtime jumps; core adapters bind trash:// / recent://.
class LocationChips : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList chips READ chips NOTIFY chipsChanged)
  Q_PROPERTY(QVariantList placeChips READ placeChips NOTIFY chipsChanged)
  Q_PROPERTY(QVariantList diskChips READ diskChips NOTIFY chipsChanged)
  Q_PROPERTY(QVariantMap volumesChip READ volumesChip NOTIFY chipsChanged)
  Q_PROPERTY(bool chooserMode READ chooserMode WRITE setChooserMode NOTIFY
                 chooserModeChanged)

public:
  explicit LocationChips(QObject *parent = nullptr);

  void setRegistry(HandlerRegistry *registry);
  void setConfig(Config *config);
  void setNav(NavStack *nav);
  void setDirectoryModel(DirectoryModel *model);

  QVariantList chips() const { return m_chips; }
  QVariantList placeChips() const;
  QVariantList diskChips() const;
  QVariantMap volumesChip() const;
  bool chooserMode() const { return m_chooserMode; }
  Q_INVOKABLE void setChooserMode(bool on);

  Q_INVOKABLE void activate(const QString &id);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE bool pin(const QString &path);
  Q_INVOKABLE bool unpin(const QString &path);
  Q_INVOKABLE bool isPinned(const QString &path) const;
  Q_INVOKABLE bool removeSqlBookmark(const QString &id);
  static QString pinId(const QString &path);
  static bool isPinId(const QString &id);
  static QString sqlBookmarkId(const QString &id);
  static bool isSqlBookmarkId(const QString &id);

  static QString expandPath(const QString &path);
  static bool allowedInChooser(const QString &adapter, const QString &runtime);

signals:
  void chipsChanged();
  void chooserModeChanged();
  void sqlBookmarkActivated(const QString &name, const QString &sql,
                            const QString &cwd, const QString &id);

private:
  QVariantMap chipMap(const QString &id) const;
  QVariantMap pinChipMap(const QString &path) const;
  QVariantMap sqlBookmarkChipMap(const QString &id) const;
  bool chipActive(const QVariantMap &chip) const;
  QVariantList chipsInGroup(const QString &group) const;
  void persistPins();
  void rebuild();

  HandlerRegistry *m_registry = nullptr;
  Config *m_config = nullptr;
  NavStack *m_nav = nullptr;
  DirectoryModel *m_model = nullptr;
  QVariantList m_chips;
  bool m_chooserMode = false;
};
