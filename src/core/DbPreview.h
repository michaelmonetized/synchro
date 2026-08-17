#pragma once

#include <QString>
#include <QVariantMap>

class DbPreview {
public:
  static bool looksLikeSqlite(const QString &path);
  static bool looksLikeDuckDb(const QString &path);
  // engine: "sqlite" or "duckdb". table empty → first table.
  static QVariantMap inspect(const QString &path, const QString &engine,
                             const QString &table = QString(), int offset = 0,
                             int limit = 40);
};
