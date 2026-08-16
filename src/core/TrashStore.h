#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

// XDG Trash spec: $XDG_DATA_HOME/Trash/{files,info} (default ~/.local/share).
// Shared with Nautilus. This is the spec backend, not a POSIX listing of files/.
class TrashStore {
public:
  static constexpr const char kUrl[] = "trash://";

  struct Item {
    QString name;
    QString trashFile;
    QString infoFile;
    QString origPath;
    QDateTime deletedAt;
    bool isDir = false;
    bool isSymlink = false;
    qint64 size = -1;
  };

  static QString dataHome();
  static QString root();
  static QString filesDir();
  static QString infoDir();

  static bool isTrashUrl(const QString &path);
  static QString normalizeUrl(const QString &path);
  // Trash/files or a descendant — open as the virtual trash view.
  static bool mapsToTrashView(const QString &path);

  // Trash dir itself and Trash/{files,info} — never a files/ child.
  static bool isProtectedTree(const QString &path);
  // Any path at or under a trash root (home trash and $XDG_DATA_HOME/Trash).
  static bool isInsideTrash(const QString &path);
  // Immediate child of Trash/files (a top-level trash item).
  static bool isTrashItem(const QString &path);

  static bool canTrash(const QString &path, QString *err);
  static bool ensureDirs(QString *err);

  // Write .trashinfo first; dest is files/<unique>. Caller then rename/copy.
  static bool prepareTrash(const QString &src, QString *trashFile,
                           QString *err);
  static void abandonPrepared(const QString &trashFile);

  static bool itemForName(const QString &name, Item *out);
  static bool itemForTrashFile(const QString &trashFile, Item *out);
  static QVector<Item> list();

  static void removeInfoFor(const QString &trashFile);
  static void purgeOrphanInfos();

  static QString encodeOrigPath(const QString &path);
  static QString decodeOrigPath(const QString &encoded);

  // Restore one files/ child to origPath (auto-suffix on collision).
  static bool restore(const QString &trashFile, QString *restoredPath,
                      QString *err);
  // Unlink every files/ child. Confirm lives in the UI.
  static bool empty(QString *err);
  static QString uniqueDest(const QString &dir, const QString &name);
};
