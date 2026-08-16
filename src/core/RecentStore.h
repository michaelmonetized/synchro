#pragma once

#include <QObject>
#include <QString>
#include <QVector>

// Append-only JSONL at ~/.local/share/synchro/recent.jsonl. Compact when
// the file grows past compactAt; keep the newest `keep` unique paths.
class RecentStore : public QObject {
  Q_OBJECT

public:
  struct Entry {
    QString ts;
    QString path;
    QString mime;
  };

  explicit RecentStore(QObject *parent = nullptr);
  RecentStore(const QString &filePath, int keep, int compactAt,
              QObject *parent = nullptr);

  QString filePath() const { return m_path; }
  void record(const QString &path, const QString &mime = QString());
  QVector<Entry> entries() const;
  // Newest-first unique paths (what recent:// shows).
  QVector<Entry> uniqueNewest() const;

  static QString defaultPath();

signals:
  void entriesChanged();

private:
  void compact();
  QString lockPath() const;

  QString m_path;
  int m_keep = 500;
  int m_compactAt = 1000;
  int m_lines = 0;
  bool m_counted = false;
};
