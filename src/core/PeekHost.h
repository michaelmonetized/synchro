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
  // Do-layer: verbs on the left, briefing / mounted params on the right.
  // Qt focus stays on the list so KeyMachine still owns Esc / Enter / W/S.
  virtual bool doParamsFocused() const { return false; }
  virtual void setDoParamsFocused(bool) {}
  virtual void doMove(int /*delta*/) {}
  virtual bool runDoVerb() { return false; }
  virtual bool deliverDoKey(int /*key*/, int /*modifiers*/) { return false; }
  // Folder peek is a temp drill layer over the current listing.
  virtual bool inFolderPeek() const { return false; }
  virtual bool folderListing() const { return false; }
  virtual void peekActivate() {}
  virtual void commitPeek() {}
  virtual void peekBack() {}
  virtual void peekMove(int /*delta*/) {}
  virtual int peekGridStride() const { return 1; }
  virtual void setPeekGridStride(int) {}
  // Logical focus inside an open file peek. Qt focus stays on the list
  // so KeyMachine still owns Esc / Space / Q.
  virtual bool peekPreviewFocused() const { return false; }
  virtual void setPeekPreviewFocused(bool) {}
  virtual bool deliverPeekKey(int /*key*/, int /*modifiers*/) { return false; }

signals:
  void openChanged();
  void actionOpenChanged();
  void peekFocusChanged();
};
