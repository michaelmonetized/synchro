#pragma once

#include "HandlerRegistry.h"
#include "Manifest.h"

#include <QString>
#include <QVariantList>
#include <QVector>

class HandlerExec;

// Run open/action handlers: exec peers, core verbs, or flag QML actions
// for the host to load.
class HandlerActions {
public:
  enum class Kind { None, Exec, Core, Qml };

  HandlerActions(HandlerRegistry *registry, HandlerExec *exec);

  static bool isVirtualLocation(const QString &path);
  static bool canEject(const QVector<Manifest::Item> &items);
  static Kind classify(const Manifest &m, const QString &kind);

  bool openBest(const QVector<Manifest::Item> &items, const QString &cwd);
  bool openWith(const QString &id, const QVector<Manifest::Item> &items,
                const QString &cwd);
  bool runExec(const Manifest &m, const QString &kind,
               const QVector<Manifest::Item> &items, const QString &cwd);
  bool runCore(const Manifest &m, const QVector<Manifest::Item> &items);
  bool runTerminal(const QVector<Manifest::Item> &items, const QString &cwd);
  bool runTrash(const QVector<Manifest::Item> &items);
  bool runEject(const QVector<Manifest::Item> &items, const QString &cwd);
  bool runAction(const QString &id, const QVector<Manifest::Item> &items,
                 const QString &cwd);

  QVector<HandlerRegistry::Match>
  openMatches(const QVector<Manifest::Item> &items) const;
  QVariantList openCandidates(const QVector<Manifest::Item> &items) const;
  QVector<HandlerRegistry::Match>
  actionMatches(const QVector<Manifest::Item> &items) const;

  QString lastError() const { return m_error; }
  HandlerExec *exec() const { return m_exec; }

private:
  HandlerRegistry *m_reg = nullptr;
  HandlerExec *m_exec = nullptr;
  QString m_error;
};
