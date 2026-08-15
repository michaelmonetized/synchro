#include "RecentStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

RecentStore::RecentStore(QObject *parent)
    : RecentStore(QString(), 500, 1000, parent) {}

RecentStore::RecentStore(const QString &filePath, int keep, int compactAt,
                         QObject *parent)
    : QObject(parent),
      m_path(filePath.isEmpty() ? defaultPath() : filePath),
      m_keep(keep > 0 ? keep : 500),
      m_compactAt(compactAt > m_keep ? compactAt : m_keep + 1) {}

QString RecentStore::defaultPath() {
  return QDir(QStandardPaths::writableLocation(
                  QStandardPaths::GenericDataLocation))
      .filePath(QStringLiteral("synchro/recent.jsonl"));
}

void RecentStore::record(const QString &path, const QString &mime) {
  if (path.isEmpty())
    return;

  const QString dir = QFileInfo(m_path).absolutePath();
  if (!QDir().mkpath(dir))
    return;

  QFile file(m_path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
    return;

  QJsonObject obj;
  const QDateTime now = QDateTime::currentDateTime();
  obj.insert(QStringLiteral("ts"),
             now.toOffsetFromUtc(now.offsetFromUtc()).toString(Qt::ISODate));
  obj.insert(QStringLiteral("path"), path);
  obj.insert(QStringLiteral("mime"), mime);
  obj.insert(QStringLiteral("ws"), QJsonValue::Null);
  file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.close();

  if (!m_counted) {
    m_lines = countLines();
    m_counted = true;
  } else {
    ++m_lines;
  }
  if (m_lines >= m_compactAt)
    compact();
}

QVector<RecentStore::Entry> RecentStore::entries() const {
  QVector<Entry> out;
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly))
    return out;
  QTextStream in(&file);
  while (!in.atEnd()) {
    const QString line = in.readLine().trimmed();
    if (line.isEmpty())
      continue;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
    if (!doc.isObject())
      continue;
    const QJsonObject obj = doc.object();
    Entry e;
    e.ts = obj.value(QStringLiteral("ts")).toString();
    e.path = obj.value(QStringLiteral("path")).toString();
    e.mime = obj.value(QStringLiteral("mime")).toString();
    if (!e.path.isEmpty())
      out.append(e);
  }
  return out;
}

int RecentStore::countLines() const {
  QFile file(m_path);
  if (!file.open(QIODevice::ReadOnly))
    return 0;
  int n = 0;
  QTextStream in(&file);
  while (!in.atEnd()) {
    if (!in.readLine().trimmed().isEmpty())
      ++n;
  }
  return n;
}

void RecentStore::compact() {
  const QVector<Entry> all = entries();
  QVector<Entry> kept;
  QSet<QString> seen;
  kept.reserve(qMin(all.size(), m_keep));
  for (int i = all.size() - 1; i >= 0; --i) {
    const Entry &e = all.at(i);
    if (seen.contains(e.path))
      continue;
    seen.insert(e.path);
    kept.prepend(e);
    if (kept.size() >= m_keep)
      break;
  }

  QSaveFile out(m_path);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return;
  for (const Entry &e : kept) {
    QJsonObject obj;
    obj.insert(QStringLiteral("ts"), e.ts);
    obj.insert(QStringLiteral("path"), e.path);
    obj.insert(QStringLiteral("mime"), e.mime);
    obj.insert(QStringLiteral("ws"), QJsonValue::Null);
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    out.write("\n");
  }
  if (!out.commit())
    return;
  m_lines = kept.size();
  m_counted = true;
}
