#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Name search via `fd` argv (no shell). Spaces become .* so "bax jpg"
// hits Baxter.jpg; metacharacters in each token stay literal.
class SearchService : public QObject {
  Q_OBJECT

public:
  static constexpr int kMaxResults = 5000;

  explicit SearchService(QObject *parent = nullptr);
  ~SearchService() override;

  static QString executable();
  static QString fuzzyPattern(const QString &query);
  static QStringList arguments(const QString &query, const QString &root,
                              bool hidden);

  void start(const QString &query, const QString &root, bool hidden);
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
  void onFinished(int exitCode, int status);
  void emitLine(const QByteArray &raw);
  void finish(bool ok, const QString &error);
  void destroyProcess();

  QProcess *m_proc = nullptr;
  QByteArray m_buf;
  QElapsedTimer m_timer;
  quint64 m_gen = 0;
  qint64 m_firstLineMs = -1;
  int m_hits = 0;
  bool m_running = false;
};
