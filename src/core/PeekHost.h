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
  // Open-with (and later action) overlays. Default no-op so list tests
  // can keep a stub peek host.
  virtual bool actionOpen() const { return false; }
  virtual void closeAction() {}

signals:
  void openChanged();
  void actionOpenChanged();
};
