#pragma once

#include "UndoStack.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

class QMimeData;

class DirectoryModel;
class SelectionModel;

// Copy / move / rename / duplicate / mkdir / trash / restore / unlink.
// Default delete is XDG trash; unlink is confirm-only.
class FileOpEngine : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
  Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
  Q_PROPERTY(QString clipboardMode READ clipboardMode NOTIFY clipboardChanged)
  Q_PROPERTY(int clipboardCount READ clipboardCount NOTIFY clipboardChanged)
  Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
  Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)

public:
  explicit FileOpEngine(QObject *parent = nullptr);
  ~FileOpEngine() override;

  void setSelection(SelectionModel *sel);
  void setDirectoryModel(DirectoryModel *model);

  bool busy() const { return m_busy; }
  QString errorString() const { return m_error; }
  QString lastMessage() const { return m_lastMessage; }
  bool canUndo() const { return m_undo.canUndo(); }
  QString clipboardMode() const;
  int clipboardCount() const;
  QStringList clipboardPaths() const;
  int progress() const { return m_progress; }
  QString progressText() const { return m_progressText; }

  static bool readClipboardMime(const QMimeData *mime, QStringList *paths,
                                QString *mode);
  static QString uriListFromPaths(const QStringList &paths);
  static QString gnomeCopiedFromPaths(const QStringList &paths,
                                      const QString &mode);
  static bool sameDevice(const QString &a, const QString &b);
  int undoCount() const { return m_undo.count(); }

  Q_INVOKABLE QVariantMap dragMime(const QStringList &paths) const;
  Q_INVOKABLE QStringList pathsFromDrop(const QVariantList &urls,
                                        const QString &uriList,
                                        const QString &gnome) const;
  Q_INVOKABLE bool canDropOn(const QString &destDir) const;
  Q_INVOKABLE bool canAcceptDrop(const QStringList &srcs,
                                 const QString &destDir) const;
  Q_INVOKABLE void dropOn(const QStringList &srcs, const QString &destDir,
                          const QString &action);

  Q_INVOKABLE void copySelection();
  Q_INVOKABLE void cutSelection();
  Q_INVOKABLE void paste();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void renameCursor(const QString &newName);
  Q_INVOKABLE void mkdirHere(const QString &name);

  Q_INVOKABLE void copyPaths(const QStringList &srcs, const QString &destDir);
  Q_INVOKABLE void movePaths(const QStringList &srcs, const QString &destDir);
  Q_INVOKABLE void renamePath(const QString &src, const QString &newName);
  Q_INVOKABLE void makeDir(const QString &parent, const QString &name);
  Q_INVOKABLE void duplicatePaths(const QStringList &srcs);
  Q_INVOKABLE void trashSelection();
  Q_INVOKABLE void restoreSelection();
  Q_INVOKABLE void unlinkSelection();
  Q_INVOKABLE void emptyTrash();
  Q_INVOKABLE void revealCursor();

  Q_INVOKABLE void trashPaths(const QStringList &srcs);
  Q_INVOKABLE void restorePaths(const QStringList &trashFiles,
                                bool reveal = false);
  Q_INVOKABLE void unlinkPaths(const QStringList &srcs);

  static bool isForbiddenPath(const QString &path);
  static bool isProtectedUnlink(const QString &path);
  static bool isSameOrDescendant(const QString &root, const QString &path);
  static QString collisionName(const QString &dir, const QString &name);

signals:
  void busyChanged();
  void errorStringChanged();
  void lastMessageChanged();
  void undoChanged();
  void clipboardChanged();
  void progressChanged();

private:
  enum class ClipMode { None, Copy, Cut };
  enum class Verb {
    Copy,
    Move,
    Rename,
    Mkdir,
    Duplicate,
    Trash,
    Restore,
    Unlink,
    EmptyTrash,
    RemoveCreated,
    RemoveEmptyDir,
    MoveBack
  };

  struct TransferProgress {
    std::atomic<qint64> bytesDone{0};
    std::atomic<qint64> bytesTotal{0};
    std::atomic<int> itemsDone{0};
    std::atomic<int> itemsTotal{0};
  };

  struct Request {
    Verb verb = Verb::Copy;
    QStringList sources;
    QStringList dests;
    QString destDir;
    QString destName;
    bool reveal = false;
    std::atomic<bool> *cancel = nullptr;
    TransferProgress *progress = nullptr;
  };

  struct Result {
    bool ok = false;
    Verb verb = Verb::Copy;
    QString error;
    QString message;
    QStringList sources;
    QStringList dests;
    bool reveal = false;
  };

  void setClipboard(const QStringList &paths, ClipMode mode);
  void clearClipboard();
  void publishClipboard();
  void hydrateClipboardFromOs();
  void refreshProgress();
  void resetProgress();
  QStringList selectionPaths() const;
  QStringList effectiveSelectionPaths() const;
  bool inTrash() const;
  void enqueue(const Request &req);
  void maybeReveal(const Result &r);
  void onFinished();
  void setError(const QString &error);
  void setMessage(const QString &message);
  void pushCompletedUndo(const Result &r);
  bool checkDestDir(const QString &dir, QString *err) const;
  bool checkSources(const QStringList &srcs, bool moving, QString *err) const;
  static bool canceled(const Request &req);

  static Result perform(const Request &req);
  static void addTreeStats(const QString &path, qint64 *bytes, int *items);
  static bool copyTree(const QString &src, const QString &dest, QString *err,
                       TransferProgress *prog = nullptr,
                       std::atomic<bool> *cancel = nullptr);
  static bool removeTree(const QString &path, QString *err);
  static bool moveOne(const QString &src, const QString &dest, QString *err,
                      TransferProgress *prog = nullptr,
                      std::atomic<bool> *cancel = nullptr);
  static bool destParentOk(const QString &destFile, QString *err);
  static bool validBaseName(const QString &name, QString *err);

  SelectionModel *m_sel = nullptr;
  DirectoryModel *m_model = nullptr;
  UndoStack m_undo;
  QFuture<Result> m_inFlight;
  QFutureWatcher<Result> m_watcher;
  UndoRecord m_pendingUndo;
  QStringList m_clipPaths;
  QString m_error;
  QString m_lastMessage;
  ClipMode m_clipMode = ClipMode::None;
  TransferProgress m_xfer;
  QTimer m_progressTimer;
  int m_progress = 0;
  QString m_progressText;
  QString m_busyLabel;
  bool m_busy = false;
  bool m_applyingUndo = false;
  std::atomic<bool> m_cancel{false};
};
