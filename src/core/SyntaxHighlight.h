#pragma once

#include <QColor>
#include <QString>

struct HighlightedText {
  bool ok = false;
  QString language;
  QString html;
};

class SyntaxHighlight {
public:
  static HighlightedText highlight(const QString &path, const QString &text);
  // Paint find hits as background spans. Existing color spans stay so
  // syntax highlighting is not replaced by inverted "TUI" marks.
  static QString markFinds(const QString &html, const QString &plain,
                           const QString &needle, int currentLocal,
                           const QColor &matchFill, const QColor &currentFill);
};
