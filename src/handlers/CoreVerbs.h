#pragma once

#include <QString>
#include <QStringList>

// Core action verbs invoked by action.runtime == "core" wrappers
// (synchro.action.trash). FileOpEngine (PR 6/7) can replace this later;
// the XDG write is the same trash Nautilus uses.
class CoreVerbs {
public:
  static QString defaultTrashRoot();
  static void setTrashRootOverride(const QString &root);
  static QString trashRoot();

  static bool isForbiddenTrashPath(const QString &path);

  static bool trash(const QStringList &paths, QString *error = nullptr);

private:
  static QString uniqueName(const QString &filesDir, const QString &infoDir,
                            const QString &name);
  static bool writeInfo(const QString &infoPath, const QString &origPath,
                        QString *error);
};
