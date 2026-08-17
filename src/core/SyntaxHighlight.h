#pragma once

#include <QString>

struct HighlightedText {
  bool ok = false;
  QString language;
  QString html;
};

class SyntaxHighlight {
public:
  static HighlightedText highlight(const QString &path, const QString &text);
};
