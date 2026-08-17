#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

struct ParquetColumn {
  QString name;
  QString type;
  QString repetition;
};

struct ParquetInfo {
  bool ok = false;
  QString error;
  qint64 numRows = 0;
  int rowGroups = 0;
  QString createdBy;
  QString codec;
  QVector<ParquetColumn> columns;
  QVariantList sample;
  QString sampleNote;
};

class ParquetMeta {
public:
  static bool looksLike(const QString &path, const QString &mimeHint = QString());
  static ParquetInfo parse(const QString &path);
  static ParquetInfo parseWithSample(const QString &path, int maxRows = 12);
  static QVariantMap toMap(const ParquetInfo &info);
};
