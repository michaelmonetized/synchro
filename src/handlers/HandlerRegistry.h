#pragma once

#include "Manifest.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// Scans first-party + user handler dirs, validates, matches selection →
// handlers. Later trees override earlier except reserved synchro.*/omarchy.*.
class HandlerRegistry {
public:
  struct Record {
    Manifest manifest;
    QString sourceDir;
    bool firstParty = false;
    bool enabled = false;
  };

  struct Match {
    QString id;
    QString sourceDir;
    Manifest manifest;
    int priority = 0;
    int specificity = 0;
    bool override = false;
  };

  HandlerRegistry();

  static QString defaultFirstPartyDir();
  static QString defaultUserDir();
  static QString defaultConfigPath();

  void setFirstPartyDir(const QString &dir) { m_firstPartyDir = dir; }
  void setUserDir(const QString &dir) { m_userDir = dir; }
  void setConfigPath(const QString &path) { m_configPath = path; }
  void setScanEnv(bool on) { m_scanEnv = on; }

  QString firstPartyDir() const { return m_firstPartyDir; }
  QString userDir() const { return m_userDir; }

  void scan();

  QVector<Record> handlers() const;
  Record handler(const QString &id) const;
  bool contains(const QString &id) const { return m_byId.contains(id); }

  QVector<Match> resolve(const QString &kind,
                         const QVector<Manifest::Item> &items) const;

private:
  void loadConfig();
  void scanTree(const QString &root, bool firstParty);
  void loadOne(const QString &dir, bool firstParty);
  bool isEnabled(const QString &id, bool firstParty) const;

  QString m_firstPartyDir;
  QString m_userDir;
  QString m_configPath;
  bool m_scanEnv = true;

  QHash<QString, Record> m_byId;
  QStringList m_disabled;
  QStringList m_enabled;
  QHash<QString, QString> m_openOverrides;
};
