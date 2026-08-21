#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Name search via `fd` argv (no shell). Spaces become .* so "bax jpg"
// hits Baxter.jpg; metacharacters in each token stay literal.
// Content search via `rg -F --files-with-matches` (fixed string, one path
// per file). `??` in the field.
class SearchService : public QObject {
  Q_OBJECT

public:
  enum class Kind { Name, Content };
  Q_ENUM(Kind)

  static constexpr int kMaxResults = 5000;
  // Content hits are denser; keep the UI ingest budget small.
  static constexpr int kMaxContentResults = 250;

  explicit SearchService(QObject *parent = nullptr);
  ~SearchService() override;

  static QString executable();
  static QString executable(Kind kind);
  static QString fuzzyPattern(const QString &query);
  static QStringList arguments(const QString &query, const QString &root,
                              bool hidden, Kind kind = Kind::Name);
  static QString resolveRoot(const QString &root);
  static bool isUnderRoot(const QString &root, const QString &path);

  Kind kind() const { return m_kind; }
  void start(const QString &query, const QString &root, bool hidden,
             Kind kind = Kind::Name);
  void cancel();
  bool running() const;
  qint64 firstLineMs() const { return m_firstLineMs; }
  int hitCount() const { return m_hits; }

signals:
  void hit(const QString &path);
  void firstHit(qint64 elapsedMs);
  void finished(bool ok, const QString &error);

private:
  void onReadyRead();
  void drainHits();
  void onFinished(int exitCode, int status);
  void completeFinish();
  void emitLine(const QByteArray &raw);
  void finish(bool ok, const QString &error);
  void destroyProcess();
  int maxHits() const;

  QProcess *m_proc = nullptr;
  QByteArray m_buf;
  QElapsedTimer m_timer;
  quint64 m_gen = 0;
  qint64 m_firstLineMs = -1;
  int m_hits = 0;
  int m_pendingExit = 0;
  int m_pendingStatus = 0;
  Kind m_kind = Kind::Name;
  bool m_running = false;
  bool m_drainScheduled = false;
  bool m_finishPending = false;
  QString m_root;
  QString m_pendingError;
};
