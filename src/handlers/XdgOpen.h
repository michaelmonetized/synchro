#pragma once

#include "HandlerExec.h"

#include <QString>

// Minimal first-party open: reads handlers/synchro.open.xdg/manifest.json.
// Full HandlerRegistry is later — this is enough to Enter-open a file.
class XdgOpen {
public:
  bool load(const QString &firstPartyDir = QString());
  bool isLoaded() const { return m_loaded; }

  bool open(const QString &path, const QString &mime, const QString &cwd);

  HandlerExec &exec() { return m_exec; }
  QString lastError() const { return m_error; }
  QString execLine() const { return m_execLine; }
  QString sourceDir() const { return m_sourceDir; }
  QString handlerId() const { return m_id; }

  static QString defaultFirstPartyDir();

private:
  HandlerExec m_exec;
  QString m_id;
  QString m_execLine;
  QString m_tryExec;
  QString m_sourceDir;
  QString m_error;
  bool m_loaded = false;
};
