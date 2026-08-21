#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

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
  Q_PROPERTY(QString lastPath READ lastPath WRITE setLastPath NOTIFY
                 lastPathChanged)
  Q_PROPERTY(QString panelSide READ panelSide WRITE setPanelSide NOTIFY
                 panelChanged)
  Q_PROPERTY(int panelSize READ panelSize WRITE setPanelSize NOTIFY
                 panelChanged)
  Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY
                 panelChanged)

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
  QString lastPath() const { return m_lastPath; }
  QString panelSide() const { return m_panelSide; }
  int panelSize() const { return m_panelSize; }
  bool panelOpen() const { return m_panelOpen; }

  Q_INVOKABLE void setShowHidden(bool show);
  Q_INVOKABLE void setView(const QString &view);
  Q_INVOKABLE void setSortRole(const QString &role);
  Q_INVOKABLE void setSortOrder(const QString &order);
  Q_INVOKABLE void setLocationChips(const QStringList &ids);
  Q_INVOKABLE void setPins(const QStringList &paths);
  Q_INVOKABLE void setLastPath(const QString &path);
  Q_INVOKABLE void setPanelSide(const QString &side);
  Q_INVOKABLE void setPanelSize(int px);
  Q_INVOKABLE void setPanelOpen(bool open);
  static QString normalizePin(const QString &path);

  bool load();
  bool save() const;

signals:
  void showHiddenChanged();
  void viewChanged();
  void sortChanged();
  void locationChipsChanged();
  void pinsChanged();
  void lastPathChanged();
  void panelChanged();

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
  QString m_lastPath;
  QString m_panelSide = QStringLiteral("bottom");
  int m_panelSize = 260;
  bool m_panelOpen = false;
};
