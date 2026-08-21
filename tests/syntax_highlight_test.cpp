#include "SyntaxHighlight.h"

#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>

class SyntaxHighlightTest : public QObject {
  Q_OBJECT

private slots:
  void pythonKeywordsBecomeSpans();
  void rustAndCppResolve();
  void markupIsEscaped();
  void unknownExtensionStaysPlain();
  void colorsComeFromOmarchyToml();
  void markFindsKeepsSyntaxColor();
  void markFindsWrapsPlain();
};

void SyntaxHighlightTest::pythonKeywordsBecomeSpans() {
  const HighlightedText hl = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/sample.py"),
      QStringLiteral("def foo():\n    return True\n"));
#ifndef SYNCHRO_HAVE_KSYNTAX
  QVERIFY(!hl.ok);
  QSKIP("built without KF6 SyntaxHighlighting");
#endif
  QVERIFY(hl.ok);
  QVERIFY(hl.language.contains(QStringLiteral("Python"), Qt::CaseInsensitive));
  QVERIFY(hl.html.startsWith(QStringLiteral("<pre")));
  QVERIFY(hl.html.contains(QStringLiteral("<span")));
  QVERIFY(hl.html.contains(QStringLiteral("def")));
  QVERIFY(hl.html.contains(QStringLiteral("return")));
}

void SyntaxHighlightTest::rustAndCppResolve() {
#ifndef SYNCHRO_HAVE_KSYNTAX
  QSKIP("built without KF6 SyntaxHighlighting");
#endif
  const HighlightedText rs = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/main.rs"), QStringLiteral("fn main() {}\n"));
  QVERIFY(rs.ok);
  QVERIFY(rs.language.contains(QStringLiteral("Rust"), Qt::CaseInsensitive));

  const HighlightedText cc = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/main.cpp"),
      QStringLiteral("int main() { return 0; }\n"));
  QVERIFY(cc.ok);
  QVERIFY(cc.language.contains(QStringLiteral("C++"), Qt::CaseInsensitive) ||
          cc.language.contains(QStringLiteral("CPP"), Qt::CaseInsensitive));
}

void SyntaxHighlightTest::markupIsEscaped() {
#ifndef SYNCHRO_HAVE_KSYNTAX
  QSKIP("built without KF6 SyntaxHighlighting");
#endif
  const HighlightedText hl = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/page.html"),
      QStringLiteral("<script>alert(1)</script>\n"));
  QVERIFY(hl.ok);
  QVERIFY(!hl.html.contains(QStringLiteral("<script>")));
  QVERIFY(hl.html.contains(QStringLiteral("&lt;")) ||
          hl.html.contains(QStringLiteral("&#60;")));
}

void SyntaxHighlightTest::unknownExtensionStaysPlain() {
  const HighlightedText hl = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/blob.notalanguage"), QStringLiteral("plain words\n"));
  QVERIFY(!hl.ok);
  QVERIFY(hl.html.isEmpty());
}

void SyntaxHighlightTest::colorsComeFromOmarchyToml() {
#ifndef SYNCHRO_HAVE_KSYNTAX
  QSKIP("built without KF6 SyntaxHighlighting");
#endif
  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());
  const QString toml = tmp.filePath(QStringLiteral("colors.toml"));
  {
    QFile f(toml);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("foreground = \"#112233\"\n"
            "magenta = \"#ff00aa\"\n"
            "green = \"#00ff66\"\n"
            "muted = \"#445566\"\n");
  }
  qputenv("SYNCHRO_COLORS_TOML", QFile::encodeName(toml));
  const HighlightedText hl = SyntaxHighlight::highlight(
      QStringLiteral("/tmp/sample.py"),
      QStringLiteral("def foo():\n    return \"hi\"\n"));
  qunsetenv("SYNCHRO_COLORS_TOML");
  QVERIFY(hl.ok);
  QVERIFY2(hl.html.contains(QStringLiteral("#ff00aa")),
           "keywords should use colors.toml magenta, not a Kate theme");
  QVERIFY2(hl.html.contains(QStringLiteral("#00ff66")),
           "strings should use colors.toml green");
  QVERIFY(hl.html.contains(QStringLiteral("font-family:monospace")));
}

void SyntaxHighlightTest::markFindsKeepsSyntaxColor() {
  const QString html = QStringLiteral(
      "<pre><span style=\"color:#ff00aa;\">def</span> foo_token</pre>");
  const QString marked = SyntaxHighlight::markFinds(
      html, QStringLiteral("def foo_token"), QStringLiteral("foo_token"), 0,
      QColor(0x44, 0x88, 0x66, 0x48), QColor(0x44, 0x88, 0x66, 0x8c));
  QVERIFY2(marked.contains(QStringLiteral("color:#ff00aa;")),
           qPrintable(marked));
  QVERIFY2(marked.contains(QStringLiteral("background-color:")),
           qPrintable(marked));
  QVERIFY(marked.contains(QStringLiteral("foo_token")));
  QVERIFY(!marked.contains(QStringLiteral("color:#020000")));
}

void SyntaxHighlightTest::markFindsWrapsPlain() {
  const QString marked = SyntaxHighlight::markFinds(
      QString(), QStringLiteral("one\ntwo HIT\nthree\n"), QStringLiteral("HIT"),
      0, QColor(Qt::yellow), QColor(Qt::red));
  QVERIFY(marked.startsWith(QStringLiteral("<pre")));
  QVERIFY(marked.contains(QStringLiteral("\n")));
  QVERIFY(marked.contains(QStringLiteral("background-color:")));
  QVERIFY(marked.contains(QStringLiteral("HIT")));
}

int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  SyntaxHighlightTest tc;
  return QTest::qExec(&tc, argc, argv);
}

#include "syntax_highlight_test.moc"
