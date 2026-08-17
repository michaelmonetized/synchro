#include "UndoStack.h"

UndoStack::UndoStack(QObject *parent) : QObject(parent) {}

void UndoStack::push(const UndoRecord &rec) {
  m_items.append(rec);
  while (m_items.size() > kMax)
    m_items.removeFirst();
  emit changed();
}

UndoRecord UndoStack::pop() {
  UndoRecord rec;
  if (m_items.isEmpty())
    return rec;
  rec = m_items.takeLast();
  emit changed();
  return rec;
}

void UndoStack::clear() {
  if (m_items.isEmpty())
    return;
  m_items.clear();
  emit changed();
}
