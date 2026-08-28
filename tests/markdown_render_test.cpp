#include "MarkdownRender.h"

#include <QColor>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>

class MarkdownRenderTest : public QObject {
  Q_OBJECT

private slots:
  void rendersReadableHierarchy();
  void keepsPreviewResourcesLocal();
  void boundedDocumentBenchmark();
};

namespace {

QVariantMap testStyle() {
  return {{QStringLiteral("format"), QStringLiteral("markdown")},
          {QStringLiteral("foreground"), QColor(QStringLiteral("#e8e2da"))},
          {QStringLiteral("muted"), QColor(QStringLiteral("#928b84"))},
          {QStringLiteral("accent"), QColor(QStringLiteral("#81a9ff"))},
          {QStringLiteral("surface"), QColor(QStringLiteral("#11151d"))},
          {QStringLiteral("border"), QColor(QStringLiteral("#445066"))},
          {QStringLiteral("sansFamily"), QStringLiteral("Inter")},
          {QStringLiteral("monoFamily"), QStringLiteral("JetBrains Mono")},
          {QStringLiteral("bodyPx"), 15}};
}

QTextBlock blockContaining(QTextDocument &document, const QString &needle) {
  for (QTextBlock block = document.begin(); block.isValid();
       block = block.next()) {
    if (block.text().contains(needle))
      return block;
  }
  return {};
}

bool blockUsesFamily(const QTextBlock &block, const QString &family) {
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const QTextFragment fragment = it.fragment();
    if (fragment.isValid() &&
        fragment.charFormat().fontFamilies().toStringList().contains(family))
      return true;
  }
  return false;
}

} // namespace

void MarkdownRenderTest::rendersReadableHierarchy() {
  const QString markdown =
      QStringLiteral("# Project guide\n\n"
                     "A readable paragraph with `inline code` and [a "
                     "link](https://example.com).\n\n"
                     "## Setup\n\n"
                     "> Keep this close at hand.\n\n"
                     "- first item\n- second item\n\n"
                     "```sh\nprintf 'hello'\nprintf 'again'\n```\n\n"
                     "| Name | State |\n| --- | --- |\n| index | ready |\n");
  const QString html = MarkdownRender::toHtml(
      markdown, QStringLiteral("/tmp/README.md"), testStyle());
  QVERIFY(!html.isEmpty());
  QVERIFY(!html.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
  QCOMPARE(html.count(QStringLiteral("<pre")), 1);

  QTextDocument rendered;
  rendered.setHtml(html);
  const QTextBlock title =
      blockContaining(rendered, QStringLiteral("Project guide"));
  const QTextBlock paragraph =
      blockContaining(rendered, QStringLiteral("readable paragraph"));
  const QTextBlock quote =
      blockContaining(rendered, QStringLiteral("close at hand"));
  const QTextBlock code =
      blockContaining(rendered, QStringLiteral("printf 'hello'"));
  QVERIFY(title.isValid());
  QVERIFY(paragraph.isValid());
  QVERIFY(quote.isValid());
  QVERIFY(code.isValid());
  QCOMPARE(title.blockFormat().headingLevel(), 1);
  QVERIFY(html.contains(QStringLiteral("font-size:xx-large")));
  QVERIFY(paragraph.blockFormat().bottomMargin() > 0);
  QVERIFY(quote.blockFormat().leftMargin() >
          paragraph.blockFormat().leftMargin());
  QVERIFY(blockUsesFamily(code, QStringLiteral("JetBrains Mono")));
  QVERIFY(code.blockFormat().background().style() != Qt::NoBrush);
  QVERIFY(rendered.rootFrame()->childFrames().size() > 0);
}

void MarkdownRenderTest::keepsPreviewResourcesLocal() {
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  const QString localPath = temp.filePath(QStringLiteral("diagram.png"));
  QFile local(localPath);
  QVERIFY(local.open(QIODevice::WriteOnly));
  local.write("not decoded during rendering");
  local.close();

  const QString markdown =
      QStringLiteral("![local diagram](diagram.png)\n\n"
                     "![tracking pixel](https://example.com/pixel.png)\n\n"
                     "<script>alert('no')</script>\n");
  const QString html = MarkdownRender::toHtml(
      markdown, temp.filePath(QStringLiteral("README.md")), testStyle());
  QVERIFY(html.contains(QUrl::fromLocalFile(localPath).toString()));
  QVERIFY(!html.contains(QStringLiteral("https://example.com/pixel.png")));
  QVERIFY(html.contains(QStringLiteral("tracking pixel")));
  QVERIFY(!html.contains(QStringLiteral("<script"), Qt::CaseInsensitive));
}

void MarkdownRenderTest::boundedDocumentBenchmark() {
  QString markdown;
  const QString section = QStringLiteral(
      "## Worker state\n\n"
      "This paragraph describes a deterministic background operation with "
      "enough prose to exercise wrapping and emphasis.\n\n"
      "```sql\nselect path, size from tree order by size desc;\n```\n\n");
  while (markdown.toUtf8().size() < 65536)
    markdown += section;
  QString html;
  QBENCHMARK {
    html = MarkdownRender::toHtml(markdown, QStringLiteral("/tmp/README.md"),
                                  testStyle());
  }
  QVERIFY(html.size() > markdown.size());
}

QTEST_MAIN(MarkdownRenderTest)

#include "markdown_render_test.moc"
