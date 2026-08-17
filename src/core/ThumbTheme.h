#pragma once

#include <QColor>
#include <QString>

// Snapshot of Omarchy colors.toml for C++ thumb painters. Matches ThemeBridge
// fallbacks so listing cards and mosaic tiles use the same palette as peek.
struct ThumbTheme {
  QColor foreground;
  QColor background;
  QColor muted;
  QColor accent;
  QColor border;
  QString fontFamily;

  QString cacheId() const;
  static ThumbTheme current();
};
