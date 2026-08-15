#pragma once

#include <QObject>

// KeyMachine talks to peek through this so list tests do not link QtQuick.
class PeekHost : public QObject {
  Q_OBJECT
public:
  explicit PeekHost(QObject *parent = nullptr) : QObject(parent) {}
  ~PeekHost() override;

  virtual bool isOpen() const = 0;
  virtual bool toggle() = 0;
  virtual bool openCurrent() = 0;
  virtual void close() = 0;
  virtual void step(int delta) = 0;

signals:
  void openChanged();
};
