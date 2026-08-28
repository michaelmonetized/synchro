#pragma once

#include <QString>
#include <QVariantMap>

namespace MarkdownRender {

// Render a bounded Markdown preview into Qt rich text. The style map is a
// snapshot of the active QML theme so this function remains reentrant and can
// run on the preview worker without touching GUI objects.
QString toHtml(const QString &markdown, const QString &sourcePath,
               const QVariantMap &style = {});

} // namespace MarkdownRender
