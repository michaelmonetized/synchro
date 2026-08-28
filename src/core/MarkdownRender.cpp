#include "MarkdownRender.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTextTableFormat>
#include <QUrl>

#include <algorithm>

namespace {

QString stringValue(const QVariantMap &style, const QString &key,
                    const QString &fallback) {
  const QString value = style.value(key).toString();
  return value.isEmpty() ? fallback : value;
}

int intValue(const QVariantMap &style, const QString &key, int fallback) {
  bool ok = false;
  const int value = style.value(key).toInt(&ok);
  return ok && value > 0 ? value : fallback;
}

QColor colorValue(const QVariantMap &style, const QString &key,
                  const QColor &fallback) {
  const QVariant value = style.value(key);
  if (value.canConvert<QColor>()) {
    const QColor color = value.value<QColor>();
    if (color.isValid())
      return color;
  }
  const QColor color(value.toString());
  return color.isValid() ? color : fallback;
}

QColor blend(const QColor &base, const QColor &tint, qreal amount) {
  const qreal mix = qBound(qreal(0), amount, qreal(1));
  return QColor::fromRgbF(base.redF() * (1 - mix) + tint.redF() * mix,
                          base.greenF() * (1 - mix) + tint.greenF() * mix,
                          base.blueF() * (1 - mix) + tint.blueF() * mix);
}

bool isCodeBlock(const QTextBlock &block) {
  const QTextBlockFormat format = block.blockFormat();
  return format.hasProperty(QTextFormat::BlockCodeFence) ||
         format.hasProperty(QTextFormat::BlockCodeLanguage);
}

void rewriteImages(QTextDocument &document, const QString &sourcePath,
                   const QColor &muted) {
  struct ImageRef {
    int position = 0;
    int length = 0;
    QTextImageFormat format;
  };
  QVector<ImageRef> images;
  for (QTextBlock block = document.begin(); block.isValid();
       block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (!fragment.isValid() || !fragment.charFormat().isImageFormat())
        continue;
      images.append({fragment.position(), fragment.length(),
                     fragment.charFormat().toImageFormat()});
    }
  }
  std::sort(images.begin(), images.end(),
            [](const ImageRef &a, const ImageRef &b) {
              return a.position > b.position;
            });
  const QDir base(QFileInfo(sourcePath).absolutePath());
  for (const ImageRef &image : std::as_const(images)) {
    const QUrl source(image.format.name());
    QString localPath;
    if (source.isRelative())
      localPath = base.absoluteFilePath(source.toString());
    else if (source.isLocalFile())
      localPath = source.toLocalFile();
    QTextCursor cursor(&document);
    cursor.setPosition(image.position);
    cursor.setPosition(image.position + image.length, QTextCursor::KeepAnchor);
    if (!localPath.isEmpty() && QFileInfo(localPath).isFile()) {
      QTextImageFormat local = image.format;
      local.setName(QUrl::fromLocalFile(QFileInfo(localPath).absoluteFilePath())
                        .toString());
      local.setMaximumWidth(QTextLength(QTextLength::PercentageLength, 100));
      cursor.setCharFormat(local);
      continue;
    }
    QString alt = image.format.stringProperty(QTextFormat::ImageAltText);
    if (alt.isEmpty())
      alt = QStringLiteral("image");
    QTextCharFormat placeholder;
    placeholder.setForeground(muted);
    placeholder.setFontItalic(true);
    cursor.insertText(QStringLiteral("[%1]").arg(alt), placeholder);
  }
}

void styleTables(QTextFrame *frame, int bodyPx, const QColor &border,
                 const QColor &headerBackground) {
  if (!frame)
    return;
  for (QTextFrame::iterator it = frame->begin(); !it.atEnd(); ++it) {
    QTextFrame *child = it.currentFrame();
    if (!child)
      continue;
    if (auto *table = dynamic_cast<QTextTable *>(child)) {
      QTextTableFormat format = table->format();
      format.setBorder(1);
      format.setBorderBrush(border);
      format.setBorderCollapse(true);
      format.setCellPadding(qMax(5, bodyPx / 2));
      format.setCellSpacing(0);
      format.setTopMargin(bodyPx);
      format.setBottomMargin(bodyPx);
      table->setFormat(format);
      if (table->rows() > 0) {
        for (int column = 0; column < table->columns(); ++column) {
          QTextTableCell cell = table->cellAt(0, column);
          QTextCharFormat cellFormat = cell.format();
          cellFormat.setBackground(headerBackground);
          cellFormat.setFontWeight(QFont::DemiBold);
          cell.setFormat(cellFormat);
        }
      }
    }
    styleTables(child, bodyPx, border, headerBackground);
  }
}

} // namespace

QString MarkdownRender::toHtml(const QString &markdown,
                               const QString &sourcePath,
                               const QVariantMap &style) {
  if (markdown.isEmpty())
    return {};

  const int bodyPx = intValue(style, QStringLiteral("bodyPx"), 15);
  const QString sans = stringValue(style, QStringLiteral("sansFamily"),
                                   QStringLiteral("Sans Serif"));
  const QString mono = stringValue(style, QStringLiteral("monoFamily"),
                                   QStringLiteral("Monospace"));
  const QColor foreground = colorValue(style, QStringLiteral("foreground"),
                                       QColor(QStringLiteral("#e6e6e6")));
  const QColor muted = colorValue(style, QStringLiteral("muted"),
                                  QColor(QStringLiteral("#9a9a9a")));
  const QColor accent = colorValue(style, QStringLiteral("accent"),
                                   QColor(QStringLiteral("#8aa8ff")));
  const QColor surface = colorValue(style, QStringLiteral("surface"),
                                    QColor(QStringLiteral("#181b22")));
  const QColor border = colorValue(style, QStringLiteral("border"),
                                   QColor(QStringLiteral("#434957")));

  QTextDocument document;
  document.setUndoRedoEnabled(false);
  document.setDocumentMargin(0);
  document.setIndentWidth(qMax(20, bodyPx * 2));
  QFont bodyFont(sans);
  bodyFont.setPixelSize(bodyPx);
  document.setDefaultFont(bodyFont);
  const auto markdownFeatures =
      QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
      QTextDocument::MarkdownNoHTML;
  document.setMarkdown(markdown, markdownFeatures);
  rewriteImages(document, sourcePath, muted);

  QVector<QTextBlock> blocks;
  for (QTextBlock block = document.begin(); block.isValid();
       block = block.next())
    blocks.append(block);

  for (int index = 0; index < blocks.size(); ++index) {
    QTextBlock block = blocks.at(index);
    QTextBlockFormat blockFormat = block.blockFormat();
    const int heading = blockFormat.headingLevel();
    const int quote = blockFormat.intProperty(QTextFormat::BlockQuoteLevel);
    const bool code = isCodeBlock(block);
    const bool previousCode = index > 0 && isCodeBlock(blocks.at(index - 1));
    const bool nextCode =
        index + 1 < blocks.size() && isCodeBlock(blocks.at(index + 1));
    const bool list = block.textList() != nullptr;

    blockFormat.setLineHeight(code ? 128 : 138,
                              QTextBlockFormat::ProportionalHeight);
    if (heading > 0) {
      const qreal top = heading == 1 ? bodyPx * 1.65 : bodyPx * 1.30;
      blockFormat.setTopMargin(index == 0 ? 0 : top);
      blockFormat.setBottomMargin(bodyPx * (heading <= 2 ? 0.62 : 0.42));
    } else if (code) {
      blockFormat.setBackground(surface);
      blockFormat.setLeftMargin(bodyPx * 0.9);
      blockFormat.setRightMargin(bodyPx * 0.9);
      blockFormat.setTopMargin(previousCode ? 0 : bodyPx * 0.85);
      blockFormat.setBottomMargin(nextCode ? 0 : bodyPx * 0.85);
    } else if (quote > 0) {
      blockFormat.setBackground(blend(surface, accent, 0.11));
      blockFormat.setLeftMargin(bodyPx * (1.1 + quote * 0.45));
      blockFormat.setRightMargin(bodyPx * 0.5);
      blockFormat.setTopMargin(bodyPx * 0.45);
      blockFormat.setBottomMargin(bodyPx * 0.60);
    } else if (list) {
      blockFormat.setBottomMargin(bodyPx * 0.32);
    } else {
      blockFormat.setBottomMargin(bodyPx * 0.72);
    }
    QTextCursor cursor(block);
    cursor.setBlockFormat(blockFormat);
    cursor.select(QTextCursor::BlockUnderCursor);
    QTextCharFormat textFormat;
    textFormat.setFontFamilies({sans});
    textFormat.setProperty(QTextFormat::FontPixelSize, bodyPx);
    textFormat.setForeground(quote > 0 ? muted : foreground);
    if (heading > 0) {
      static constexpr qreal scales[] = {0.0,  1.85, 1.48, 1.24,
                                         1.10, 1.0,  0.95};
      textFormat.setProperty(QTextFormat::FontPixelSize,
                             bodyPx * scales[qBound(1, heading, 6)]);
      textFormat.setFontWeight(heading <= 2 ? QFont::Bold : QFont::DemiBold);
      textFormat.setForeground(heading == 1 ? foreground : accent);
    } else if (code) {
      textFormat.setFontFamilies({mono});
      textFormat.setProperty(QTextFormat::FontPixelSize, qMax(11, bodyPx - 1));
      textFormat.setForeground(foreground);
    }
    cursor.mergeCharFormat(textFormat);

    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (!fragment.isValid())
        continue;
      const QTextCharFormat original = fragment.charFormat();
      QTextCharFormat fragmentStyle;
      bool apply = false;
      if (original.fontFixedPitch()) {
        fragmentStyle.setFontFamilies({mono});
        fragmentStyle.setProperty(QTextFormat::FontPixelSize,
                                  qMax(11, bodyPx - 1));
        if (!code)
          fragmentStyle.setBackground(surface);
        apply = true;
      }
      if (original.isAnchor()) {
        fragmentStyle.setForeground(accent);
        fragmentStyle.setFontUnderline(false);
        apply = true;
      }
      if (!apply)
        continue;
      QTextCursor fragmentCursor(&document);
      fragmentCursor.setPosition(fragment.position());
      fragmentCursor.setPosition(fragment.position() + fragment.length(),
                                 QTextCursor::KeepAnchor);
      fragmentCursor.mergeCharFormat(fragmentStyle);
    }
  }

  styleTables(document.rootFrame(), bodyPx, border,
              blend(surface, accent, 0.10));
  QString html = document.toHtml();
  // QTextDocument represents each source line in a fenced block as its own
  // QTextBlock and serializes those as adjacent <pre> elements. Join only
  // immediately adjacent code elements so a fence reads as one calm field
  // rather than a stack of highlighted rows.
  static const QRegularExpression adjacentCode(
      QStringLiteral("</span></pre>\\s*<pre[^>]*><span[^>]*>"));
  html.replace(adjacentCode, QStringLiteral("<br />"));
  return html;
}
