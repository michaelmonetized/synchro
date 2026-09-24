#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <atomic>

class DirectoryModel;
class QSocketNotifier;

// Live git marks, the diff rail, and shell questions for the browser.
class GitLane : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
  Q_PROPERTY(bool diffOpen READ diffOpen NOTIFY diffChanged)
  Q_PROPERTY(QString diffTitle READ diffTitle NOTIFY diffChanged)
  Q_PROPERTY(QVariantList diffFiles READ diffFiles NOTIFY diffChanged)
  Q_PROPERTY(int diffFile READ diffFile NOTIFY diffChanged)
  Q_PROPERTY(QStringList diffLines READ diffLines NOTIFY diffChanged)
  Q_PROPERTY(int diffLine READ diffLine NOTIFY diffChanged)
  Q_PROPERTY(int diffCol READ diffCol NOTIFY diffChanged)
  Q_PROPERTY(bool diffBusy READ diffBusy NOTIFY diffChanged)

public:
  explicit GitLane(DirectoryModel *model, QObject *parent = nullptr);
  ~GitLane() override;

  int revision() const { return m_revision; }
  Q_INVOKABLE QString mark(const QString &path) const;

  bool diffOpen() const { return m_diffOpen; }
  QString diffTitle() const { return m_diffTitle; }
  QVariantList diffFiles() const { return m_diffFiles; }
  int diffFile() const { return m_diffFile; }
  QStringList diffLines() const { return m_diffLines; }
  int diffLine() const { return m_diffLine; }
  int diffCol() const { return m_diffCol; }
  bool diffBusy() const { return m_diffBusy; }

  Q_INVOKABLE void openDiff(const QString &path);
  Q_INVOKABLE void closeDiff();
  Q_INVOKABLE void moveDiffLine(int delta);
  Q_INVOKABLE void pageDiffLine(int direction, int page);
  Q_INVOKABLE void edgeDiffLine(int direction);
  Q_INVOKABLE void moveDiffCol(int delta);
  Q_INVOKABLE void edgeDiffCol(int direction);
  Q_INVOKABLE void moveDiffFile(int delta);

  void commit(const QString &path, const QString &message);
  void push(const QString &path);
  void gitcp(const QString &path, const QString &message);
  void openNvim(const QString &path);
  void openT3(const QString &path, bool isFile);

  Q_INVOKABLE void answer(const QString &text);
  Q_INVOKABLE void cancelCommand();

signals:
  void revisionChanged();
  void diffChanged();
  void question(const QString &text);
  void note(const QString &text);
  void commandFinished();

private:
  void scheduleScan();
  void scan();
  void publishMarks(int generation, const QHash<QString, QString> &marks);
  void loadDiffFiles();
  void publishDiffFiles(int generation, const QVariantList &files);
  void loadDiffBody();
  void publishDiffBody(int generation, const QStringList &lines);
  void startProgram(const QString &cwd, const QString &program,
                    const QStringList &args);
  void startShell(const QString &cwd, const QString &script,
                  const QStringList &args);
  QString gitcpProgram() const;
  void onPty();
  void considerPrompt();
  void reap(bool killed);
  QString shellTail() const;

  DirectoryModel *m_model = nullptr;
  QTimer m_scanTimer;
  QTimer m_promptTimer;
  QHash<QString, QString> m_marks;
  int m_revision = 0;
  std::atomic<int> m_generation{0};
  bool m_scanning = false;
  bool m_scanAgain = false;

  bool m_diffOpen = false;
  bool m_diffBusy = false;
  QString m_diffRoot;
  QString m_diffScope;
  QString m_diffTitle;
  QVariantList m_diffFiles;
  QStringList m_diffLines;
  int m_diffFile = 0;
  int m_diffLine = 0;
  int m_diffCol = 0;
  int m_diffGeneration = 0;

  int m_master = -1;
  int m_child = -1;
  QSocketNotifier *m_pty = nullptr;
  QString m_pending;
  QString m_log;
  bool m_waiting = false;
};
