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
      {QStringLiteral("home"), QStringLiteral("Go home"), true},
      {QStringLiteral("hidden"), QStringLiteral("Toggle hidden files"), true},
      {QStringLiteral("grid"), QStringLiteral("Grid view"), true},
      {QStringLiteral("list"), QStringLiteral("List view"), true},
      {QStringLiteral("empty"), QStringLiteral("Empty trash"), true},
      {QStringLiteral("help"), QStringLiteral("Key reference"), true},
      {QStringLiteral("?"), QStringLiteral("Key reference"), true},
  };
}

QString CommandPalette::helpText() {
  return QStringLiteral(
      "j/k  move     h  up        l  open/peek    Enter  activate\n"
      "/    filter   :  command   ?name  search   Ctrl+L  jump\n"
      ".    hidden   v  grid      t  terminal     Space  peek\n"
      "Esc  pop      F1  help     g  reveal\n"
      "\n"
      ":trash :recent :home :hidden :grid :list :empty :help :?");
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
