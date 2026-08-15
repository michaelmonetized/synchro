#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// Survives omarchy-theme-set replacing theme/ (inode death).
class ThemeBridge : public QObject {
  Q_OBJECT
  QML_ELEMENT

  Q_PROPERTY(QColor foreground READ foreground NOTIFY themeChanged)
  Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
  Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
  Q_PROPERTY(QColor urgent READ urgent NOTIFY themeChanged)
  Q_PROPERTY(QColor muted READ muted NOTIFY themeChanged)
  Q_PROPERTY(QColor selectedFill READ selectedFill NOTIFY themeChanged)
  Q_PROPERTY(QColor hoverFill READ hoverFill NOTIFY themeChanged)
  Q_PROPERTY(QColor normalBorder READ normalBorder NOTIFY themeChanged)
  Q_PROPERTY(int radius READ radius NOTIFY themeChanged)
  Q_PROPERTY(int gapsOut READ gapsOut NOTIFY themeChanged)
  Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY themeChanged)
  Q_PROPERTY(int fontBody READ fontBody NOTIFY themeChanged)
  Q_PROPERTY(int fontBaseSize READ fontBaseSize NOTIFY themeChanged)
  Q_PROPERTY(qreal spacingScale READ spacingScale NOTIFY themeChanged)
  Q_PROPERTY(
      bool spacingScaleWithFont READ spacingScaleWithFont NOTIFY themeChanged)

public:
  explicit ThemeBridge(QObject *parent = nullptr);

  QColor foreground() const { return m_foreground; }
  QColor background() const { return m_background; }
  QColor accent() const { return m_accent; }
  QColor urgent() const { return m_urgent; }
  QColor muted() const { return m_muted; }
  QColor selectedFill() const { return m_selectedFill; }
  QColor hoverFill() const { return m_hoverFill; }
  QColor normalBorder() const { return m_normalBorder; }
  int radius() const { return m_radius; }
  int gapsOut() const { return m_gapsOut; }
  QString fontFamily() const { return m_fontFamily; }
  int fontBody() const { return m_fontBody; }
  int fontBaseSize() const { return m_fontBaseSize; }
  qreal spacingScale() const { return m_spacingScale; }
  bool spacingScaleWithFont() const { return m_spacingScaleWithFont; }

  Q_INVOKABLE int space(int px) const;

signals:
  void themeChanged();

private:
  void applyFallbackPalette();
  void setupWatchers();
  void rearmWatches();
  void scheduleReload();
  void reloadNow();
  bool bothThemeFilesReadable() const;
  bool loadThemeFiles();
  void loadUserShell();
  void loadColors(const QString &raw);
  QHash<QString, QString> parseShell(const QString &raw) const;
  void applyShellValues(const QHash<QString, QString> &values);
  void composeDerived();
  void publish();
  void refreshHyprland();
  void applyRoundingOutput(const QByteArray &raw);
  void applyGapsOutOutput(const QByteArray &raw);
  QColor resolveColorToken(const QString &token, const QColor &fallback) const;
  static QColor withAlpha(const QColor &c, qreal alpha);
  static bool fileReadable(const QString &path);
  static QString readUtf8(const QString &path, bool *ok = nullptr);
  static bool parseBoolToken(const QString &value, bool fallback);
  void runHyprctl(const QStringList &args,
                  void (ThemeBridge::*slot)(const QByteArray &));

  QFileSystemWatcher m_watcher;
  QTimer m_reloadTimer;

  QString m_currentDir;
  QString m_themeDir;
  QString m_themeNamePath;
  QString m_colorsPath;
  QString m_themeShellPath;
  QString m_userConfigDir;
  QString m_userShellPath;

  QHash<QString, QString> m_themeShell;
  QHash<QString, QString> m_userShell;

  QColor m_foreground;
  QColor m_background;
  QColor m_accent;
  QColor m_urgent;
  QColor m_muted;
  QColor m_selectedFill;
  QColor m_hoverFill;
  QColor m_normalBorder;

  QString m_fontFamily = QStringLiteral("monospace");
  int m_radius = 0;
  int m_gapsOut = 5;
  int m_fontBaseSize = 12;
  int m_fontBody = 12;
  int m_fontBodyOverride = 0;
  qreal m_spacingScale = 1.0;
  bool m_spacingScaleWithFont = true;

  qreal m_hoverFillAlpha = 0.08;
  qreal m_selectedFillAlpha = 0.18;
  qreal m_normalBorderAlpha = 0.4;
  QString m_hoverColorToken = QStringLiteral("foreground");
  QString m_selectedColorToken = QStringLiteral("foreground");
  QString m_normalColorToken = QStringLiteral("foreground");

  int m_missingRetries = 0;
  bool m_appliedAny = false;
  bool m_hadThemeFiles = false;
};
