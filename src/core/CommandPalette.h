#pragma once

#include <QString>
#include <QVector>

// Colon-command table. Builtins win over action :id / :title (K7).
struct CommandSpec {
  QString id;
  QString title;
  bool builtin = false;
};

class CommandPalette {
public:
  static QVector<CommandSpec> builtins();
  static QString helpText();
  static QString stripSigil(const QString &text);
  static QString lastSegment(const QString &id);

  explicit CommandPalette(QVector<CommandSpec> actions = {});

  void setActions(QVector<CommandSpec> actions);
  QVector<CommandSpec> actions() const { return m_actions; }
  void registerAction(const QString &id, const QString &title);

  QVector<CommandSpec> match(const QString &query) const;
  bool resolve(const QString &query, CommandSpec *out,
               QString *error = nullptr) const;

private:
  QVector<CommandSpec> catalog() const;

  QVector<CommandSpec> m_actions;
};
