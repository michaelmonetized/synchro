#include "SyntaxHighlight.h"

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#ifdef SYNCHRO_HAVE_KSYNTAX
#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>
#endif

namespace {

QString escapeHtml(QStringView s) {
  QString out;
  out.reserve(s.size() + 8);
  for (const QChar c : s) {
    if (c == QLatin1Char('&'))
      out += QStringLiteral("&amp;");
    else if (c == QLatin1Char('<'))
      out += QStringLiteral("&lt;");
    else if (c == QLatin1Char('>'))
      out += QStringLiteral("&gt;");
    else
      out += c;
  }
  return out;
}

// Same file ThemeBridge reads. Syntax colors come from here, not a Kate theme.
QString colorsTomlPath() {
  const QByteArray env = qgetenv("SYNCHRO_COLORS_TOML");
  if (!env.isEmpty())
    return QString::fromLocal8Bit(env);
  return QDir::homePath() +
         QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

struct OmarchyPalette {
  QColor foreground = QColor(QStringLiteral("#d7e9cb"));
  QColor background = QColor(QStringLiteral("#020000"));
  QColor accent = QColor(QStringLiteral("#55937c"));
  QColor muted = QColor(QStringLiteral("#707880"));
  QColor urgent = QColor(QStringLiteral("#a55555"));
  QColor red = QColor(QStringLiteral("#a55555"));
  QColor yellow = QColor(QStringLiteral("#c4b454"));
  QColor orange = QColor(QStringLiteral("#c49254"));
  QColor green = QColor(QStringLiteral("#7aa86a"));
  QColor cyan = QColor(QStringLiteral("#5aa8a0"));
  QColor blue = QColor(QStringLiteral("#6a8ab4"));
  QColor magenta = QColor(QStringLiteral("#a07aa8"));
  QColor brown = QColor(QStringLiteral("#8a7058"));
};

void take(QColor *slot, const QString &val) {
  const QColor c(val);
  if (c.isValid())
    *slot = c;
}

OmarchyPalette loadPalette() {
  static OmarchyPalette cached;
  static QString cachedPath;
  static qint64 cachedMtime = -1;

  const QString path = colorsTomlPath();
  const qint64 mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
  if (cachedPath == path && cachedMtime == mtime && mtime > 0)
    return cached;

  OmarchyPalette pal;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    cached = pal;
    cachedPath = path;
    cachedMtime = mtime;
    return pal;
  }

  static const QRegularExpression re(
      QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*[\"']?(#[0-9A-Fa-f]{6})"));
  const QString raw = QString::fromUtf8(f.readAll());
  for (const QString &line : raw.split(QLatin1Char('\n'))) {
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch())
      continue;
    const QString key = m.captured(1);
    const QString val = m.captured(2);
    if (key == QLatin1String("foreground"))
      take(&pal.foreground, val);
    else if (key == QLatin1String("background"))
      take(&pal.background, val);
    else if (key == QLatin1String("accent"))
      take(&pal.accent, val);
    else if (key == QLatin1String("muted"))
      take(&pal.muted, val);
    else if (key == QLatin1String("red") || key == QLatin1String("color1"))
      take(&pal.red, val);
    else if (key == QLatin1String("yellow") || key == QLatin1String("color3"))
      take(&pal.yellow, val);
    else if (key == QLatin1String("orange"))
      take(&pal.orange, val);
    else if (key == QLatin1String("green") || key == QLatin1String("color2"))
      take(&pal.green, val);
    else if (key == QLatin1String("cyan") || key == QLatin1String("color6"))
      take(&pal.cyan, val);
    else if (key == QLatin1String("blue") || key == QLatin1String("color4"))
      take(&pal.blue, val);
    else if (key == QLatin1String("magenta") || key == QLatin1String("color5"))
      take(&pal.magenta, val);
    else if (key == QLatin1String("brown"))
      take(&pal.brown, val);
    else if (key == QLatin1String("color8"))
      take(&pal.muted, val);
  }
  pal.urgent = pal.red;
  cached = pal;
  cachedPath = path;
  cachedMtime = mtime;
  return pal;
}

#ifdef SYNCHRO_HAVE_KSYNTAX

QColor colorForStyle(KSyntaxHighlighting::Theme::TextStyle style,
                     const OmarchyPalette &pal) {
  using S = KSyntaxHighlighting::Theme::TextStyle;
  switch (style) {
  case S::Keyword:
  case S::ControlFlow:
    return pal.magenta;
  case S::Function:
    return pal.blue;
  case S::BuiltIn:
  case S::Extension:
  case S::Import:
  case S::DataType:
    return pal.cyan;
  case S::Preprocessor:
    return pal.orange;
  case S::Attribute:
  case S::Char:
  case S::SpecialChar:
    return pal.yellow;
  case S::String:
  case S::VerbatimString:
  case S::SpecialString:
    return pal.green;
  case S::DecVal:
  case S::BaseN:
  case S::Float:
  case S::Constant:
    return pal.orange;
  case S::Comment:
  case S::Documentation:
  case S::Annotation:
  case S::CommentVar:
  case S::RegionMarker:
  case S::Information:
    return pal.muted;
  case S::Warning:
    return pal.yellow;
  case S::Alert:
  case S::Error:
    return pal.red;
  case S::Operator:
    return pal.muted;
  case S::Variable:
  case S::Normal:
  case S::Others:
  default:
    return pal.foreground;
  }
}

KSyntaxHighlighting::Repository &repository() {
  static KSyntaxHighlighting::Repository repo;
  return repo;
}

class HtmlSink : public KSyntaxHighlighting::AbstractHighlighter {
public:
  QString html;
  OmarchyPalette pal;

  void start() {
    html = QStringLiteral(
               "<pre style=\"margin:0;color:%1;font-family:monospace;\">")
               .arg(pal.foreground.name());
  }

  void finish() { html += QStringLiteral("</pre>"); }

  void addLine(QStringView line) {
    m_line = line.toString();
    m_at = 0;
    m_state = highlightLine(m_line, m_state);
    if (m_at < m_line.size())
      html += escapeHtml(QStringView(m_line).mid(m_at));
  }

protected:
  void applyFormat(int offset, int length,
                   const KSyntaxHighlighting::Format &format) override {
    if (offset > m_at)
      html += escapeHtml(QStringView(m_line).mid(m_at, offset - m_at));
    const QStringView slice = QStringView(m_line).mid(offset, length);
    if (!format.isValid() ||
        format.textStyle() == KSyntaxHighlighting::Theme::Normal) {
      html += escapeHtml(slice);
    } else {
      const QColor c = colorForStyle(format.textStyle(), pal);
      html += QStringLiteral("<span style=\"color:");
      html += c.name();
      html += QLatin1Char(';');
      if (format.hasBoldOverride() && format.isBold(theme()))
        html += QStringLiteral("font-weight:700;");
      if (format.hasItalicOverride() && format.isItalic(theme()))
        html += QStringLiteral("font-style:italic;");
      html += QStringLiteral("\">");
      html += escapeHtml(slice);
      html += QStringLiteral("</span>");
    }
    m_at = offset + length;
  }

private:
  QString m_line;
  int m_at = 0;
  KSyntaxHighlighting::State m_state;
};

#endif

} // namespace

QString SyntaxHighlight::markFinds(const QString &html, const QString &plain,
                                   const QString &needle, int currentLocal,
                                   const QColor &matchFill,
                                   const QColor &currentFill) {
  QString src = html;
  if (src.isEmpty()) {
    src = QStringLiteral("<pre style=\"margin:0;\">");
    src += escapeHtml(plain);
    src += QStringLiteral("</pre>");
  }
  if (plain.isEmpty() || needle.isEmpty())
    return src;

  struct Range {
    int start = 0;
    int end = 0;
    bool current = false;
  };
  QVector<Range> ranges;
  bool sensitive = false;
  for (const QChar c : needle) {
    if (c.isUpper()) {
      sensitive = true;
      break;
    }
  }
  const Qt::CaseSensitivity cs =
      sensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
  int from = 0;
  int n = 0;
  while (true) {
    const int at = plain.indexOf(needle, from, cs);
    if (at < 0)
      break;
    Range r;
    r.start = at;
    r.end = at + needle.size();
    r.current = (n == currentLocal);
    ranges.append(r);
    from = at + qMax(1, needle.size());
    ++n;
  }
  if (ranges.isEmpty())
    return src;

  const QString matchCss = matchFill.isValid()
                               ? matchFill.name(QColor::HexArgb)
                               : QStringLiteral("#66cccc00");
  const QString currentCss = currentFill.isValid()
                                 ? currentFill.name(QColor::HexArgb)
                                 : QStringLiteral("#99ffcc00");

  QString out;
  out.reserve(src.size() + ranges.size() * 72);
  int textPos = 0;
  int ri = 0;
  bool hlOpen = false;
  QString hlCss;

  auto closeHl = [&] {
    if (!hlOpen)
      return;
    out += QStringLiteral("</span>");
    hlOpen = false;
    hlCss.clear();
  };
  auto openHl = [&](const QString &css) {
    if (hlOpen)
      return;
    out += QStringLiteral("<span style=\"background-color:");
    out += css;
    out += QStringLiteral(";\">");
    hlOpen = true;
    hlCss = css;
  };
  auto syncHl = [&] {
    while (ri < ranges.size() && textPos >= ranges.at(ri).end)
      ++ri;
    if (ri < ranges.size() && textPos >= ranges.at(ri).start &&
        textPos < ranges.at(ri).end) {
      const QString css =
          ranges.at(ri).current ? currentCss : matchCss;
      if (hlOpen && hlCss != css)
        closeHl();
      openHl(css);
    } else {
      closeHl();
    }
  };

  int i = 0;
  const int len = src.size();
  while (i < len) {
    if (src.at(i) == QLatin1Char('<')) {
      closeHl();
      const int gt = src.indexOf(QLatin1Char('>'), i);
      if (gt < 0) {
        out += QStringView(src).mid(i);
        break;
      }
      out += QStringView(src).mid(i, gt - i + 1);
      i = gt + 1;
      syncHl();
      continue;
    }
    if (src.at(i) == QLatin1Char('&')) {
      syncHl();
      const int sc = src.indexOf(QLatin1Char(';'), i);
      if (sc > i) {
        out += QStringView(src).mid(i, sc - i + 1);
        i = sc + 1;
      } else {
        out += src.at(i);
        ++i;
      }
      ++textPos;
      continue;
    }
    syncHl();
    out += src.at(i);
    ++i;
    ++textPos;
  }
  closeHl();
  return out;
}

HighlightedText SyntaxHighlight::highlight(const QString &path,
                                           const QString &text) {
  HighlightedText out;
  if (text.isEmpty())
    return out;
#ifdef SYNCHRO_HAVE_KSYNTAX
  auto &repo = repository();
  KSyntaxHighlighting::Definition def = repo.definitionForFileName(path);
  if (!def.isValid())
    return out;

  HtmlSink sink;
  sink.pal = loadPalette();
  sink.setDefinition(def);
  // Theme is only consulted for bold/italic flags from the definition.
  sink.setTheme(repo.defaultTheme(KSyntaxHighlighting::Repository::DarkTheme));
  sink.start();
  const QStringList lines = text.split(QLatin1Char('\n'));
  for (int i = 0; i < lines.size(); ++i) {
    if (i)
      sink.html += QLatin1Char('\n');
    sink.addLine(lines.at(i));
  }
  sink.finish();
  out.ok = true;
  out.language = def.name();
  out.html = sink.html;
#else
  Q_UNUSED(path);
#endif
  return out;
}
