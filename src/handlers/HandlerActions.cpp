#include "HandlerActions.h"

#include "CoreVerbs.h"
#include "HandlerExec.h"

#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

HandlerActions::HandlerActions(HandlerRegistry *registry, HandlerExec *exec)
    : m_reg(registry), m_exec(exec) {}

bool HandlerActions::isVirtualLocation(const QString &path) {
  return path.startsWith(QLatin1String("trash:")) ||
         path.startsWith(QLatin1String("recent:")) ||
         path.startsWith(QLatin1String("search:"));
}

HandlerActions::Kind HandlerActions::classify(const Manifest &m,
                                              const QString &kind) {
  const QString runtime = m.runtime(kind);
  if (runtime == QLatin1String("core"))
    return Kind::Core;
  if (!m.execLine(kind).isEmpty() || runtime == QLatin1String("exec"))
    return Kind::Exec;
  if (m.entryPoints.contains(kind) && !m.entryPoints.value(kind).isEmpty())
    return Kind::Qml;
  return Kind::None;
}

bool HandlerActions::runExec(const Manifest &m, const QString &kind,
                             const QVector<Manifest::Item> &items,
                             const QString &cwd) {
  m_error.clear();
  if (!m_exec) {
    m_error = QStringLiteral("no exec");
    return false;
  }
  const QString line = m.execLine(kind);
  if (line.isEmpty()) {
    m_error = QStringLiteral("handler '%1' has no %2.exec").arg(m.id, kind);
    return false;
  }
  HandlerExec::Request req;
  req.exec = line;
  req.handlerId = m.id;
  req.handlerDir = m.sourceDir;
  req.cwd = cwd;
  for (const auto &item : items) {
    HandlerExec::Item hi;
    hi.path = item.path;
    hi.uri = item.uri.isEmpty() ? QUrl::fromLocalFile(item.path) : item.uri;
    hi.mime = item.mime;
    hi.isDir = item.isDir;
    req.items.append(hi);
  }
  if (!m_exec->run(req)) {
    m_error = m_exec->lastError();
    return false;
  }
  return true;
}

bool HandlerActions::runCore(const Manifest &m,
                             const QVector<Manifest::Item> &items) {
  m_error.clear();
  const QString verb = m.coreVerb(QStringLiteral("action"));
  if (verb == QLatin1String("trash"))
    return runTrash(items);
  m_error = QStringLiteral("unknown core verb '%1'").arg(verb);
  return false;
}

bool HandlerActions::openBest(const QVector<Manifest::Item> &items,
                              const QString &cwd) {
  m_error.clear();
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  const auto matches = m_reg->resolve(QStringLiteral("open"), items);
  if (matches.isEmpty()) {
    m_error = QStringLiteral("no open handler matched");
    return false;
  }
  return openWith(matches.constFirst().id, items, cwd);
}

bool HandlerActions::openWith(const QString &id,
                              const QVector<Manifest::Item> &items,
                              const QString &cwd) {
  m_error.clear();
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  const HandlerRegistry::Record rec = m_reg->handler(id);
  if (rec.manifest.id.isEmpty()) {
    m_error = QStringLiteral("unknown handler '%1'").arg(id);
    return false;
  }
  if (!rec.enabled) {
    m_error = QStringLiteral("handler '%1' is disabled").arg(id);
    return false;
  }
  if (classify(rec.manifest, QStringLiteral("open")) != Kind::Exec) {
    m_error = QStringLiteral("handler '%1' is not an exec open").arg(id);
    return false;
  }
  return runExec(rec.manifest, QStringLiteral("open"), items, cwd);
}

bool HandlerActions::runTerminal(const QVector<Manifest::Item> &items,
                                 const QString &cwd) {
  m_error.clear();
  if (isVirtualLocation(cwd)) {
    m_error = QStringLiteral("terminal is disabled on virtual locations");
    return false;
  }
  if (!m_reg) {
    m_error = QStringLiteral("no registry");
    return false;
  }
  const HandlerRegistry::Record rec =
      m_reg->handler(QStringLiteral("synchro.action.terminal"));
  if (rec.manifest.id.isEmpty() || !rec.enabled) {
    m_error = QStringLiteral("synchro.action.terminal is not available");
    return false;
  }
  return runExec(rec.manifest, QStringLiteral("action"), items, cwd);
}

bool HandlerActions::runTrash(const QVector<Manifest::Item> &items) {
  m_error.clear();
  QStringList paths;
  paths.reserve(items.size());
  for (const auto &item : items) {
    if (!item.path.isEmpty())
      paths.append(item.path);
  }
  if (!CoreVerbs::trash(paths, &m_error))
    return false;
  return true;
}

QVector<HandlerRegistry::Match>
HandlerActions::openMatches(const QVector<Manifest::Item> &items) const {
  if (!m_reg)
    return {};
  return m_reg->resolve(QStringLiteral("open"), items);
}

QVariantList
HandlerActions::openCandidates(const QVector<Manifest::Item> &items) const {
  QVariantList out;
  for (const auto &m : openMatches(items)) {
    QVariantMap row;
    row.insert(QStringLiteral("id"), m.id);
    row.insert(QStringLiteral("name"), m.manifest.name);
    row.insert(QStringLiteral("exec"), m.manifest.execLine(QStringLiteral("open")));
    row.insert(QStringLiteral("priority"), m.priority);
    out.append(row);
  }
  return out;
}
