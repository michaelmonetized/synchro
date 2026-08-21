#include "CommandPalette.h"

#include <QVariantMap>

#include <initializer_list>

namespace {

QString norm(const QString &s) { return s.trimmed().toLower(); }

bool exactAlias(const CommandSpec &c, const QString &q) {
  return CommandPalette::lastSegment(c.id).compare(q, Qt::CaseInsensitive) ==
             0 ||
         c.title.compare(q, Qt::CaseInsensitive) == 0;
}

bool prefixHit(const CommandSpec &c, const QString &q) {
  if (q.isEmpty())
    return true;
  const QString id = c.id.toLower();
  const QString tail = CommandPalette::lastSegment(c.id).toLower();
  const QString title = c.title.toLower();
  return id == q || tail == q || title == q || id.startsWith(q) ||
         tail.startsWith(q) || title.startsWith(q);
}

} // namespace

QVector<CommandSpec> CommandPalette::builtins() {
  return {
      {QStringLiteral("trash"), QStringLiteral("Open trash"), true},
      {QStringLiteral("recent"), QStringLiteral("Open recents"), true},
      {QStringLiteral("volumes"), QStringLiteral("Open volumes"), true},
      {QStringLiteral("home"), QStringLiteral("Go home"), true},
      {QStringLiteral("hidden"), QStringLiteral("Toggle hidden files"), true},
      {QStringLiteral("pin"), QStringLiteral("Pin this folder"), true},
      {QStringLiteral("unpin"), QStringLiteral("Unpin this folder"), true},
      {QStringLiteral("sort"), QStringLiteral("Sort listing"), true},
      {QStringLiteral("all"), QStringLiteral("Show files and folders"), true},
      {QStringLiteral("files"), QStringLiteral("Files only"), true},
      {QStringLiteral("folders"), QStringLiteral("Folders only"), true},
      {QStringLiteral("grid"), QStringLiteral("Grid view"), true},
      {QStringLiteral("list"), QStringLiteral("List view"), true},
      {QStringLiteral("fsn"), QStringLiteral("3D file browser (fsv)"), true},
      {QStringLiteral("fsv"), QStringLiteral("3D file browser (tree|map)"), true},
      {QStringLiteral("park"), QStringLiteral("3D file browser (fsv)"), true},
      {QStringLiteral("nedry"), QStringLiteral("3D file browser (fsv)"), true},
      {QStringLiteral("term"), QStringLiteral("Terminal panel"), true},
      {QStringLiteral("empty"), QStringLiteral("Empty trash"), true},
      {QStringLiteral("help"), QStringLiteral("Key reference"), true},
      {QStringLiteral("?"), QStringLiteral("Key reference"), true},
  };
}

namespace {

struct HelpRow {
  const char *keys;
  const char *what;
};

struct HelpSection {
  const char *title;
  std::initializer_list<HelpRow> rows;
};

// Single source of truth for the help overlay (rendered as a grid) and
// the plain helpText() dump.
const std::initializer_list<HelpSection> kHelp = {
    {"Navigate",
     {{"j k / W S", "move cursor (Shift leaps 5)"},
      {"h / Left / Q", "up a folder"},
      {"l / Right / E", "open / peek"},
      {"Enter", "activate"},
      {"Tab", "toggle search / listing"},
      {"[ ]", "cycle chooser filter"},
      {"Esc", "pop / back"}}},
    {"Peek",
     {{"Space", "peek selection"},
      {"Enter", "open / send"},
      {"A / D", "step index / file"},
      {"W / S", "move or scroll file"},
      {"Esc / Q", "leave peek"}}},
    {"Do layer",
     {{"Ctrl+Enter", "open (also right-click)"},
      {"W / S", "verbs"},
      {"A / D", "params"},
      {"Enter", "run"},
      {"Esc / Q", "leave"}}},
    {"Find & fields",
     {{"/", "filter listing"},
      {":", "command"},
      {"Ctrl+L", "jump to path"},
      {"Ctrl+K", "filter field"},
      {"? name", "name search (fd)"},
      {"?? text", "content search (rg)"}}},
    {"Files",
     {{"y / x / p", "copy / cut / paste"},
      {"r", "rename"},
      {"n", "new folder"},
      {"u", "undo"},
      {"Delete", "trash"},
      {"Shift+Delete", "unlink"},
      {"drag", "drop to move / copy"}}},
    {"View",
     {{".", "hidden files"},
      {"v", "grid / list (middle-click)"},
      {"V", "visual select"},
      {"Shift+P", "pin folder"},
      {"headers", "click to sort columns"},
      {"All Files Folders", "kind filter chips"}}},
    {"Terminal",
     {{"Ctrl+`", "terminal — open / flip focus"},
      {":term", "bottom | left | right | off"},
      {"hover", "focus follows the pointer"},
      {"t", "external terminal"},
      {"g", "reveal"}}},
    {"Commands",
     {{":sort <role>", "name / size / mtime / type"},
      {":sort", "again: flip order"},
      {":all :files :folders", "kind filter"},
      {":trash :recent", "trash / recents"},
      {":volumes :home", "volumes / home"},
      {":hidden :pin :unpin", "toggles"},
      {":grid :list :empty", "views / empty trash"},
      {":help ? F1", "this help"}}},
    {"fsv — it's a unix system",
     {{":fsv / :fsn / Ctrl+M", "3D browser, tree | map"},
      {"click", "fly to node along the roads"},
      {"W A S D", "drive"},
      {"drag / wheel", "orbit / zoom"},
      {"M", "map <-> tree"},
      {"Enter", "dive into folder"}}},
};

} // namespace

QVariantList CommandPalette::helpModel() {
  QVariantList sections;
  for (const HelpSection &sec : kHelp) {
    QVariantList rows;
    for (const HelpRow &row : sec.rows) {
      QVariantMap r;
      r.insert(QStringLiteral("keys"), QString::fromUtf8(row.keys));
      r.insert(QStringLiteral("what"), QString::fromUtf8(row.what));
      rows.append(r);
    }
    QVariantMap m;
    m.insert(QStringLiteral("title"), QString::fromUtf8(sec.title));
    m.insert(QStringLiteral("rows"), rows);
    sections.append(m);
  }
  return sections;
}

QString CommandPalette::helpText() {
  QString out;
  for (const HelpSection &sec : kHelp) {
    out += QString::fromUtf8(sec.title);
    out += QLatin1Char('\n');
    for (const HelpRow &row : sec.rows) {
      out += QStringLiteral("  %1  %2\n")
                 .arg(QString::fromUtf8(row.keys), -22)
                 .arg(QString::fromUtf8(row.what));
    }
    out += QLatin1Char('\n');
  }
  return out.trimmed();
}

QString CommandPalette::stripSigil(const QString &text) {
  QString t = text.trimmed();
  if (t.startsWith(QLatin1Char(':')))
    t = t.mid(1).trimmed();
  return t;
}

QString CommandPalette::lastSegment(const QString &id) {
  const int dot = id.lastIndexOf(QLatin1Char('.'));
  return dot < 0 ? id : id.mid(dot + 1);
}

CommandPalette::CommandPalette(QVector<CommandSpec> actions)
    : m_actions(std::move(actions)) {}

void CommandPalette::setActions(QVector<CommandSpec> actions) {
  m_actions = std::move(actions);
}

void CommandPalette::registerAction(const QString &id, const QString &title) {
  if (id.isEmpty())
    return;
  for (CommandSpec &c : m_actions) {
    if (c.id == id) {
      c.title = title;
      return;
    }
  }
  m_actions.append({id, title, false});
}

QVector<CommandSpec> CommandPalette::catalog() const {
  QVector<CommandSpec> out = builtins();
  out += m_actions;
  return out;
}

QVector<CommandSpec> CommandPalette::match(const QString &query) const {
  const QString q = norm(stripSigil(query));
  QVector<CommandSpec> out;
  for (const CommandSpec &c : catalog()) {
    if (prefixHit(c, q))
      out.append(c);
  }
  return out;
}

bool CommandPalette::resolve(const QString &query, CommandSpec *out,
                             QString *error) const {
  const QString q = stripSigil(query);
  if (q.isEmpty()) {
    if (error)
      *error = QStringLiteral("unknown command");
    return false;
  }

  for (const CommandSpec &c : builtins()) {
    if (c.id.compare(q, Qt::CaseInsensitive) == 0) {
      if (out)
        *out = c;
      return true;
    }
  }
  for (const CommandSpec &c : m_actions) {
    if (c.id.compare(q, Qt::CaseInsensitive) == 0) {
      if (out)
        *out = c;
      return true;
    }
  }

  QVector<CommandSpec> aliases;
  for (const CommandSpec &c : catalog()) {
    if (exactAlias(c, q))
      aliases.append(c);
  }
  if (aliases.size() == 1) {
    if (out)
      *out = aliases.constFirst();
    return true;
  }
  if (aliases.size() > 1) {
    for (const CommandSpec &c : aliases) {
      if (c.builtin) {
        if (out)
          *out = c;
        return true;
      }
    }
    if (error)
      *error = QStringLiteral("ambiguous command");
    return false;
  }

  const QVector<CommandSpec> hits = match(q);
  if (hits.isEmpty()) {
    if (error)
      *error = QStringLiteral("unknown command");
    return false;
  }
  if (hits.size() == 1) {
    if (out)
      *out = hits.constFirst();
    return true;
  }
  if (error)
    *error = QStringLiteral("ambiguous command");
  return false;
}
