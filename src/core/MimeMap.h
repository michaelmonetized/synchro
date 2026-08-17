#pragma once

#include <QMimeDatabase>
#include <QString>

// QMimeDatabase names now. Scan-first app/device icon index is later —
// unconstrained themed lookup mis-resolves names like "zoom".
class MimeMap {
public:
  QString mimeForFile(const QString &path) const;
  QString iconNameForFile(const QString &path) const;
  QString iconNameForMime(const QString &mimeName) const;
  // GUI-thread themed resolve: preferred → theme → application-x-executable.
  QString resolveIcon(const QString &iconName) const;
  // Suffix, well-known basename (Dockerfile, Makefile, …), or a UTF-8 sniff.
  static bool isProbablyText(const QString &path,
                             const QString &mimeHint = QString());

private:
  QMimeDatabase m_db;
};
