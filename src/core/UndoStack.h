#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct UndoRecord {
  enum class Kind { Copy, Move, Rename, Mkdir, Duplicate, Trash, Restore };
  Kind kind = Kind::Copy;
  QStringList sources;
  QStringList dests;
};

// Last 32 inverse ops. Survives navigation, not process death.
class UndoStack : public QObject {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)

public:
  static constexpr int kMax = 32;

  explicit UndoStack(QObject *parent = nullptr);

  int count() const { return m_items.size(); }
  bool canUndo() const { return !m_items.isEmpty(); }

  void push(const UndoRecord &rec);
  UndoRecord pop();
  void clear();

signals:
  void changed();

private:
  QVector<UndoRecord> m_items;
};
