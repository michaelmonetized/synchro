#include "HandlerActions.h"

#include "CoreVerbs.h"
#include "HandlerExec.h"
#include "VolumeStore.h"

#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

HandlerActions::HandlerActions(HandlerRegistry *registry, HandlerExec *exec)
    : m_reg(registry), m_exec(exec) {}

bool HandlerActions::isVirtualLocation(const QString &path) {
  return path.startsWith(QLatin1String("trash:")) ||
         path.startsWith(QLatin1String("recent:")) ||
         path.startsWith(QLatin1String("search:")) ||
         path.startsWith(QLatin1String("volumes:"));
}

bool HandlerActions::canEject(const QVector<Manifest::Item> &items) {
  if (items.size() != 1)
    return false;
  const QString path = items.constFirst().path;
  if (path.isEmpty() || isVirtualLocation(path))
    return false;
  const VolumeStore::Volume v = VolumeStore::instance().findMount(path);
  if (v.mountPoint.isEmpty() || v.mountPoint == QLatin1String("/"))
    return false;
  return v.extra || v.removable;
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
  // A selected folder is a better project root for an agent than its parent
  // listing. Files keep their containing directory as the working root. The
  // full selection remains available through SYNCHRO_SELECTION either way.
  if (m.id == QLatin1String("synchro.action.agent") && items.size() == 1) {
    const Manifest::Item &item = items.constFirst();
    if (!item.path.isEmpty()) {
      const QFileInfo info(item.path);
      req.cwd = item.isDir ? info.absoluteFilePath() : info.absolutePath();
    }
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
  if (verb == QLatin1String("eject"))
    return runEject(items, QString());
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

bool HandlerActions::runAction(const QString &id,
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
  if (!rec.manifest.hasKind(QStringLiteral("action"))) {
    m_error = QStringLiteral("handler '%1' is not an action").arg(id);
    return false;
  }
  const Kind kind = classify(rec.manifest, QStringLiteral("action"));
  if (kind == Kind::Core) {
    if (rec.manifest.coreVerb(QStringLiteral("action")) ==
        QLatin1String("eject"))
      return runEject(items, cwd);
    return runCore(rec.manifest, items);
  }
  if (kind == Kind::Exec) {
    const QString exec = rec.manifest.execLine(QStringLiteral("action"));
    const bool terminal = rec.manifest.id ==
                              QLatin1String("synchro.action.terminal") ||
                          exec.contains(QLatin1String("xdg-terminal-exec"));
    const bool agent =
        rec.manifest.id == QLatin1String("synchro.action.agent");
    if ((terminal || agent) && isVirtualLocation(cwd)) {
      m_error = terminal
                    ? QStringLiteral("terminal is disabled on virtual locations")
                    : QStringLiteral("agent is disabled on virtual locations");
      return false;
    }
    return runExec(rec.manifest, QStringLiteral("action"), items, cwd);
  }
  if (kind == Kind::Qml) {
    m_error = QStringLiteral("qml action requires host");
    return false;
  }
  m_error = QStringLiteral("handler '%1' has no action runtime").arg(id);
  return false;
}

bool HandlerActions::runEject(const QVector<Manifest::Item> &items,
                              const QString &cwd) {
  m_error.clear();
  QString target;
  if (!items.isEmpty())
    target = items.constFirst().path;
  if (target.isEmpty() || HandlerActions::isVirtualLocation(target))
    target = cwd;
  if (!VolumeStore::instance().eject(target, &m_error)) {
    if (m_error.isEmpty())
      m_error = QStringLiteral("eject failed");
    return false;
  }
  return true;
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

QVector<HandlerRegistry::Match>
HandlerActions::actionMatches(const QVector<Manifest::Item> &items) const {
  if (!m_reg)
    return {};
  auto matches = m_reg->resolve(QStringLiteral("action"), items);
  QVector<HandlerRegistry::Match> out;
  out.reserve(matches.size());
  for (const auto &m : matches) {
    if (m.id == QLatin1String("synchro.action.eject") && !canEject(items))
      continue;
    out.append(m);
  }
  return out;
}
