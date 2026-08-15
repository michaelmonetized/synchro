#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

// Handler manifest parse + validate. Safety copies PluginRegistry +
// omarchy-plugin-validate (newline-safe relative entry points, no reserved
// third-party ids, no folder.replaceListing).
class Manifest {
public:
  struct Match {
    QStringList mime;
    QString mimeMode;
    QStringList suffix;
    QStringList pathGlob;
    QString pathMode;
    QStringList folderContains;
    int minItems = -1;
    int maxItems = -1;
    QString host;
  };

  struct Item {
    QString path;
    QUrl uri;
    QString mime;
    bool isDir = false;
  };

  int schemaVersion = 0;
  QString id;
  QString name;
  QString version;
  QString author;
  QString license;
  QString description;
  QStringList kinds;
  QMap<QString, QString> entryPoints;
  int priority = 50;
  bool keepLoaded = false;
  Match match;
  QJsonObject open;
  QJsonObject preview;
  QJsonObject folder;
  QJsonObject action;
  QJsonObject location;
  QStringList permissions;
  QString sourceDir;
  bool firstParty = false;

  static bool isSafeEntryPoint(const QString &value);
  // Lexical + symlink + canonical prefix. resolvedOut is the real path.
  static bool confineEntryPoint(const QString &sourceDir, const QString &rel,
                                QString *resolvedOut = nullptr,
                                QString *error = nullptr);
  static bool isValidId(const QString &id);
  static bool isReservedId(const QString &id);
  static bool matchMime(const QString &mime, const QString &pattern);
  static bool matchPathGlob(const QString &path, const QString &pattern);
  static bool matchSuffix(const QString &path, const QString &suffix);

  static Manifest fromJson(const QJsonObject &obj);

  QVariantMap toVariantMap() const;
  bool hasKind(const QString &kind) const;
  QString tryExec(const QString &kind) const;
  QString execLine(const QString &kind) const;
  QString runtime(const QString &kind) const;
  QString coreVerb(const QString &kind) const;
};

struct ManifestValidation {
  bool ok = false;
  QStringList errors;
  Manifest manifest;
};

ManifestValidation validateManifestDir(const QString &dir, bool firstParty);

bool manifestMatches(const Manifest &m, const QString &kind,
                     const QVector<Manifest::Item> &items);
int manifestSpecificity(const Manifest &m, const QVector<Manifest::Item> &items);
