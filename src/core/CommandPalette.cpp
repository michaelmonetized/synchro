#include "CommandPalette.h"

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

QString CommandPalette::helpText() {
  return QStringLiteral(
      "j/k  move     WASD  move    Shift+WASD  leap 5\n"
      "h / Left / Q  up     l / Right / E  open/peek     Enter  activate\n"
      "Space  peek this     Enter  open/send     Esc / Q  leave peek\n"
      "A/D    peek index/file     W/S  move or scroll file\n"
      "Ctrl+Enter / right-click  do-layer (actions + params)\n"
      "do: W/S verbs   A/D params   Enter run   Esc/Q leave\n"
      "/    filter   Tab  search/listing   :  command   Ctrl+L  jump\n"
      "? name (fd)   ?? content (rg)\n"
      ".    hidden   v / middle-click  grid      V  visual\n"
      "Shift+P  pin folder     [ ]  cycle filter\n"
      "y/x/p copy/cut/paste   drag drop   r rename   n mkdir   u undo\n"
      "Delete trash   Shift+Delete unlink   t terminal   g reveal\n"
      "Esc  pop      F1  help\n"
      "\n"
      ":trash :recent :volumes :home :hidden :pin :unpin :sort :grid :list :fsn :empty :agent :help :?\n"
      ":all :files :folders  show everything / files only / folders only\n"
      ":term [bottom|left|right|off]  terminal panel (follows cwd)\n"
      "Ctrl+`  open terminal / flip focus between panel and browser\n"
      ":sort name|size|mtime|type [asc|desc]   :sort  flip order   list headers click-sort\n"
      ":fsv tree|map / Ctrl+M  3D browser\n"
      "in fsv: click fly-to   WASD drive   drag orbit   wheel zoom   M map/tree");
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
