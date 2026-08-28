#include "PortalService.h"

#include "FileOpEngine.h"
#include "HandlerLoader.h"
#include "HandlerRegistry.h"
#include "HostApi.h"
#include "IconImageProvider.h"
#include "MimeMap.h"
#include "XdgOpen.h"

#include "DirectoryModel.h"
#include "ThumbImageProvider.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>

#include <cstdio>

struct FilterRuleDBus {
  uint type = 0;
  QString pattern;
};

struct FilterDBus {
  QString name;
  QList<FilterRuleDBus> rules;
};

struct ChoicePairDBus {
  QString id;
  QString value;
};

Q_DECLARE_METATYPE(FilterRuleDBus)
Q_DECLARE_METATYPE(FilterDBus)
Q_DECLARE_METATYPE(QList<FilterRuleDBus>)
Q_DECLARE_METATYPE(QList<FilterDBus>)
Q_DECLARE_METATYPE(ChoicePairDBus)
Q_DECLARE_METATYPE(QList<ChoicePairDBus>)

QDBusArgument &operator<<(QDBusArgument &arg, const FilterRuleDBus &r) {
  arg.beginStructure();
  arg << r.type << r.pattern;
  arg.endStructure();
  return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FilterRuleDBus &r) {
  arg.beginStructure();
  arg >> r.type >> r.pattern;
  arg.endStructure();
  return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const FilterDBus &f) {
  arg.beginStructure();
  arg << f.name << f.rules;
  arg.endStructure();
  return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FilterDBus &f) {
  arg.beginStructure();
  arg >> f.name >> f.rules;
  arg.endStructure();
  return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ChoicePairDBus &c) {
  arg.beginStructure();
  arg << c.id << c.value;
  arg.endStructure();
  return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, ChoicePairDBus &c) {
  arg.beginStructure();
  arg >> c.id >> c.value;
  arg.endStructure();
  return arg;
}

namespace {

void registerPortalMetaTypes() {
  static const bool once = [] {
    qDBusRegisterMetaType<FilterRuleDBus>();
    qDBusRegisterMetaType<QList<FilterRuleDBus>>();
    qDBusRegisterMetaType<FilterDBus>();
    qDBusRegisterMetaType<QList<FilterDBus>>();
    qDBusRegisterMetaType<ChoicePairDBus>();
    qDBusRegisterMetaType<QList<ChoicePairDBus>>();
    return true;
  }();
  Q_UNUSED(once);
}

QVariant unwrap(const QVariant &value) {
  if (value.userType() == qMetaTypeId<QDBusVariant>())
    return unwrap(value.value<QDBusVariant>().variant());
  return value;
}

bool asArgument(const QVariant &value, QDBusArgument *out) {
  const QVariant v = unwrap(value);
  if (!v.canConvert<QDBusArgument>())
    return false;
  *out = v.value<QDBusArgument>();
  return true;
}

bool optionBool(const QVariantMap &options, const char *key, bool fallback) {
  const auto it = options.constFind(QLatin1String(key));
  if (it == options.cend())
    return fallback;
  return unwrap(*it).toBool();
}

QString optionString(const QVariantMap &options, const char *key) {
  const auto it = options.constFind(QLatin1String(key));
  if (it == options.cend())
    return {};
  return unwrap(*it).toString();
}

QByteArray optionBytes(const QVariantMap &options, const char *key) {
  const auto it = options.constFind(QLatin1String(key));
  if (it == options.cend())
    return {};
  const QVariant v = unwrap(*it);
  if (v.canConvert<QByteArray>())
    return v.toByteArray();
  QDBusArgument arg;
  if (asArgument(v, &arg)) {
    QByteArray bytes;
    arg >> bytes;
    return bytes;
  }
  return {};
}

QString pathFromBytes(QByteArray bytes) {
  if (bytes.endsWith('\0'))
    bytes.chop(1);
  return QString::fromUtf8(bytes);
}

QString optionPath(const QVariantMap &options, const char *key) {
  return pathFromBytes(optionBytes(options, key));
}

QStringList optionFiles(const QVariantMap &options) {
  const auto it = options.constFind(QStringLiteral("files"));
  if (it == options.cend())
    return {};
  const QVariant v = unwrap(*it);
  QStringList names;
  if (v.canConvert<QVariantList>()) {
    const QVariantList list = v.toList();
    for (const QVariant &item : list) {
      const QByteArray raw = unwrap(item).toByteArray();
      const QString name = pathFromBytes(raw);
      if (!name.isEmpty())
        names.append(QFileInfo(name).fileName());
    }
    return names;
  }
  QDBusArgument arg;
  if (asArgument(v, &arg)) {
    arg.beginArray();
    while (!arg.atEnd()) {
      QByteArray raw;
      arg >> raw;
      const QString name = pathFromBytes(raw);
      if (!name.isEmpty())
        names.append(QFileInfo(name).fileName());
    }
    arg.endArray();
  }
  return names;
}

PortalFilter fromDBus(const FilterDBus &in) {
  PortalFilter out;
  out.name = in.name;
  out.rules.reserve(in.rules.size());
  for (const FilterRuleDBus &r : in.rules)
    out.rules.append(PortalFilterRule{r.type, r.pattern});
  return out;
}

QVector<PortalFilter> optionFilters(const QVariantMap &options) {
  const auto it = options.constFind(QStringLiteral("filters"));
  if (it == options.cend())
    return {};
  QVector<PortalFilter> out;
  QDBusArgument arg;
  if (asArgument(*it, &arg)) {
    QList<FilterDBus> list;
    arg >> list;
    for (const FilterDBus &f : list)
      out.append(fromDBus(f));
    return out;
  }
  const QVariant v = unwrap(*it);
  if (!v.canConvert<QVariantList>())
    return out;
  for (const QVariant &item : v.toList()) {
    const QVariantList row = unwrap(item).toList();
    if (row.size() < 2)
      continue;
    PortalFilter f;
    f.name = row.at(0).toString();
    for (const QVariant &ruleV : row.at(1).toList()) {
      const QVariantList rule = unwrap(ruleV).toList();
      if (rule.size() < 2)
        continue;
      f.rules.append(
          PortalFilterRule{rule.at(0).toUInt(), rule.at(1).toString()});
    }
    out.append(f);
  }
  return out;
}

PortalFilter optionCurrentFilter(const QVariantMap &options) {
  const auto it = options.constFind(QStringLiteral("current_filter"));
  if (it == options.cend())
    return {};
  QDBusArgument arg;
  if (asArgument(*it, &arg)) {
    FilterDBus f;
    arg >> f;
    return fromDBus(f);
  }
  const QVariantList row = unwrap(*it).toList();
  if (row.size() < 2)
    return {};
  PortalFilter f;
  f.name = row.at(0).toString();
  for (const QVariant &ruleV : row.at(1).toList()) {
    const QVariantList rule = unwrap(ruleV).toList();
    if (rule.size() < 2)
      continue;
    f.rules.append(PortalFilterRule{rule.at(0).toUInt(), rule.at(1).toString()});
  }
  return f;
}

QVector<PortalChoice> optionChoices(const QVariantMap &options) {
  const auto it = options.constFind(QStringLiteral("choices"));
  if (it == options.cend())
    return {};
  QVector<PortalChoice> out;
  QDBusArgument arg;
  if (!asArgument(*it, &arg))
    return out;
  arg.beginArray();
  while (!arg.atEnd()) {
    PortalChoice c;
    QString selected;
    arg.beginStructure();
    arg >> c.id >> c.label;
    arg.beginArray();
    while (!arg.atEnd()) {
      QString oid;
      QString olabel;
      arg.beginStructure();
      arg >> oid >> olabel;
      arg.endStructure();
      c.optionIds.append(oid);
      c.optionLabels.append(olabel);
    }
    arg.endArray();
    arg >> selected;
    arg.endStructure();
    c.value = selected;
    if (c.optionIds.isEmpty() && c.value.isEmpty())
      c.value = QStringLiteral("false");
    out.append(c);
  }
  arg.endArray();
  return out;
}

QString stripMnemonic(QString label) {
  label.remove(QLatin1Char('_'));
  label.remove(QLatin1Char('&'));
  return label.trimmed();
}

QString defaultTitle(ChooserSession::Kind kind, bool directory) {
  switch (kind) {
  case ChooserSession::Kind::SaveFile:
    return QStringLiteral("Save File");
  case ChooserSession::Kind::SaveFiles:
    return QStringLiteral("Save Files");
  case ChooserSession::Kind::OpenFile:
    break;
  }
  return directory ? QStringLiteral("Select Folder") : QStringLiteral("Open File");
}

const char *kindName(ChooserSession::Kind kind) {
  switch (kind) {
  case ChooserSession::Kind::SaveFile:
    return "SaveFile";
  case ChooserSession::Kind::SaveFiles:
    return "SaveFiles";
  case ChooserSession::Kind::OpenFile:
    break;
  }
  return "OpenFile";
}

bool titleLooksLikeChooser(const QString &title) {
  return title.startsWith(QLatin1String("Open"), Qt::CaseInsensitive) ||
         title.startsWith(QLatin1String("Save"), Qt::CaseInsensitive) ||
         title.startsWith(QLatin1String("Select"), Qt::CaseInsensitive) ||
         title.startsWith(QLatin1String("Choose"), Qt::CaseInsensitive);
}

QString titledForApp(ChooserSession::Kind kind, bool directory,
                     const QString &title, const QString &appId) {
  const QString fallback = defaultTitle(kind, directory);
  QString t = title.trimmed();
  if (t.isEmpty())
    t = fallback;
  else if (!titleLooksLikeChooser(t))
    t = fallback + QStringLiteral(" — ") + t;
  if (!appId.isEmpty() && !t.contains(appId, Qt::CaseInsensitive))
    t += QStringLiteral(" — ") + appId;
  return t;
}

QString defaultAccept(ChooserSession::Kind kind, bool directory) {
  switch (kind) {
  case ChooserSession::Kind::SaveFile:
  case ChooserSession::Kind::SaveFiles:
    return QStringLiteral("Save");
  case ChooserSession::Kind::OpenFile:
    break;
  }
  return directory ? QStringLiteral("Select") : QStringLiteral("Open");
}

QString existingDir(const QString &path) {
  if (path.isEmpty())
    return {};
  QFileInfo info(path);
  if (info.isDir()) {
    const QString canon = info.canonicalFilePath();
    return canon.isEmpty() ? info.absoluteFilePath() : canon;
  }
  if (info.exists()) {
    const QFileInfo parent(info.absolutePath());
    const QString canon = parent.canonicalFilePath();
    return canon.isEmpty() ? parent.absoluteFilePath() : canon;
  }
  return {};
}

bool isValidSaveBaseName(const QString &name) {
  if (name.isEmpty() || name == QLatin1String(".") ||
      name == QLatin1String(".."))
    return false;
  if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
    return false;
  return true;
}

bool isSaveFilePath(const QString &path) {
  const QFileInfo fi(path);
  if (fi.isDir())
    return false;
  if (!fi.exists())
    return true;
  return fi.isFile();
}

QStringList fileUris(const QStringList &paths) {
  QStringList uris;
  uris.reserve(paths.size());
  for (const QString &path : paths) {
    if (path.isEmpty())
      continue;
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    if (!url.isValid() || !url.isLocalFile() ||
        url.scheme() != QLatin1String("file"))
      continue;
    uris.append(url.toString(QUrl::FullyEncoded));
  }
  return uris;
}

FilterDBus toDBus(const PortalFilter &f) {
  FilterDBus out;
  out.name = f.name;
  for (const PortalFilterRule &r : f.rules)
    out.rules.append(FilterRuleDBus{r.type, r.pattern});
  return out;
}

} // namespace

PortalRequestAdaptor::PortalRequestAdaptor(ChooserSession *session)
    : QDBusAbstractAdaptor(session), m_session(session) {}

void PortalRequestAdaptor::Close() {
  // Reply to Close first; tearing down the object here drops that reply.
  if (m_session)
    QMetaObject::invokeMethod(m_session, &ChooserSession::cancel,
                              Qt::QueuedConnection);
}

FileChooserAdaptor::FileChooserAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent) {}

uint FileChooserAdaptor::OpenFile(const QDBusObjectPath &handle,
                                  const QString &app_id,
                                  const QString &parent_window,
                                  const QString &title,
                                  const QVariantMap &options,
                                  QVariantMap &results) {
  results.clear();
  auto *svc = qobject_cast<PortalService *>(parent());
  if (!svc)
    return 2;
  // Context lives on the registered object, not this adaptor.
  svc->setDelayedReply(true);
  svc->beginRequest(ChooserSession::Kind::OpenFile, handle, app_id,
                    parent_window, title, options, svc->connection(),
                    svc->message());
  return 0;
}

uint FileChooserAdaptor::SaveFile(const QDBusObjectPath &handle,
                                  const QString &app_id,
                                  const QString &parent_window,
                                  const QString &title,
                                  const QVariantMap &options,
                                  QVariantMap &results) {
  results.clear();
  auto *svc = qobject_cast<PortalService *>(parent());
  if (!svc)
    return 2;
  svc->setDelayedReply(true);
  svc->beginRequest(ChooserSession::Kind::SaveFile, handle, app_id,
                    parent_window, title, options, svc->connection(),
                    svc->message());
  return 0;
}

uint FileChooserAdaptor::SaveFiles(const QDBusObjectPath &handle,
                                   const QString &app_id,
                                   const QString &parent_window,
                                   const QString &title,
                                   const QVariantMap &options,
                                   QVariantMap &results) {
  results.clear();
  auto *svc = qobject_cast<PortalService *>(parent());
  if (!svc)
    return 2;
  svc->setDelayedReply(true);
  svc->beginRequest(ChooserSession::Kind::SaveFiles, handle, app_id,
                    parent_window, title, options, svc->connection(),
                    svc->message());
  return 0;
}

QString ChooserSession::windowTitle(Kind kind, bool directory,
                                    const QString &title, const QString &appId) {
  return titledForApp(kind, directory, title, appId);
}

ChooserSession::ChooserSession(Kind kind, const QDBusObjectPath &handle,
                               const QString &title,
                               const QVariantMap &options,
                               HandlerRegistry *registry,
                               HandlerLoader *loader, XdgOpen *xdg,
                               MimeMap *mime, QQmlEngine *engine,
                               QObject *parent)
    : QObject(parent), m_kind(kind), m_handle(handle),
      m_title(title.trimmed()), m_nav(&m_model),
      m_selection(&m_filter, &m_model), m_keys(&m_model, &m_filter, &m_nav) {
  registerPortalMetaTypes();
  m_filter.setDirectoryModel(&m_model);
  m_model.setSearchModel(&m_search);
  m_model.setRecentStore(&m_recents);
  m_keys.setSelection(&m_selection);
  m_keys.setSearchModel(&m_search);
  m_keys.setRecentStore(&m_recents);
  if (m_config.view() == QLatin1String("grid"))
    m_keys.setGridMode(true);
  m_model.setShowHidden(m_config.showHidden());
  m_filter.setSortRoleName(m_config.sortRole());
  m_filter.setSortOrder(m_config.sortOrder());
  applyOptions(options);
  m_title = ChooserSession::windowTitle(kind, m_directory, m_title);
  if (m_acceptLabel.isEmpty())
    m_acceptLabel = defaultAccept(kind, m_directory);

  m_directory = m_directory || kind == Kind::SaveFiles;
  m_keys.setChooserMode(true, m_multiple && kind == Kind::OpenFile,
                        kind == Kind::SaveFile);
  if (registry) {
    m_chips.setRegistry(registry);
    m_chips.setConfig(&m_config);
    m_chips.setNav(&m_nav);
    m_chips.setDirectoryModel(&m_model);
    m_chips.setChooserMode(true);
    m_keys.setLocationChips(&m_chips);
  }

  if (registry && loader && engine) {
    m_host = new HostApi(&m_model, &m_filter, &m_nav, registry, loader, xdg,
                         mime, engine, this);
    m_host->setChooserMode(true);
    m_keys.setPeekHost(m_host);
    connect(m_host, &HostApi::peekCommitRequested, this,
            &ChooserSession::onAcceptRequested);
    m_host->setGridMode(m_keys.gridMode());
    connect(&m_keys, &KeyMachine::gridModeChanged, m_host, [this] {
      if (m_host)
        m_host->setGridMode(m_keys.gridMode());
    });
  }

  connect(&m_keys, &KeyMachine::chooserAcceptRequested, this,
          &ChooserSession::onAcceptRequested);
  connect(&m_keys, &KeyMachine::dismissRequested, this,
          &ChooserSession::cancel);
  connect(&m_keys, &KeyMachine::chooserPromptDismissRequested, this,
          &ChooserSession::dismissOverwrite);
  connect(&m_keys, &KeyMachine::filterCycleRequested, this,
          &ChooserSession::cycleFilter);
  connect(&m_selection, &SelectionModel::cursorChanged, this,
          &ChooserSession::destPreviewChanged);
  connect(&m_model, &DirectoryModel::pathChanged, this,
          &ChooserSession::destPreviewChanged);
  if (m_host) {
    connect(m_host, &HostApi::folderPeekChanged, this,
            &ChooserSession::destPreviewChanged);
  }
  connect(&m_model, &DirectoryModel::fileActivated, this,
          [this](const QString &path, const QString &) {
            if (m_done)
              return;
            if (m_kind == Kind::OpenFile && !m_directory) {
              if (m_multiple && m_selection.selectedCount() > 1) {
                accept();
                return;
              }
              finishWithPaths({path});
              return;
            }
            if (m_kind == Kind::SaveFiles) {
              accept();
              return;
            }
            if (m_directory) {
              const QString parent = QFileInfo(path).absolutePath();
              if (!parent.isEmpty() && QFileInfo(parent).isDir() &&
                  !DirectoryModel::isVirtualPath(parent))
                finishWithPaths({parent});
              return;
            }
            if (m_kind == Kind::SaveFile) {
              setSaveName(QFileInfo(path).fileName());
              accept();
            }
          });

  QString start = optionPath(options, "current_folder");
  if (m_kind == Kind::SaveFile) {
    const QString currentFile = optionPath(options, "current_file");
    if (!currentFile.isEmpty()) {
      const QFileInfo fi(currentFile);
      if (m_saveName.isEmpty())
        m_saveName = fi.fileName();
      if (start.isEmpty())
        start = fi.absolutePath();
    }
  }
  const QString dir = existingDir(start);
  m_model.setPath(dir.isEmpty() ? QDir::homePath() : dir);
}

ChooserSession::~ChooserSession() {
  if (m_window) {
    m_window->disconnect(this);
    m_window->close();
    m_window->deleteLater();
    m_window = nullptr;
  }
}

QObject *ChooserSession::hostApi() const { return m_host; }

void ChooserSession::applyOptions(const QVariantMap &options) {
  m_multiple = optionBool(options, "multiple", false);
  m_directory = optionBool(options, "directory", false);
  m_acceptLabel = stripMnemonic(optionString(options, "accept_label"));
  m_saveName = optionString(options, "current_name");
  m_saveFiles = optionFiles(options);
  m_filters = optionFilters(options);
  const PortalFilter current = optionCurrentFilter(options);
  if (!current.name.isEmpty() || !current.rules.isEmpty()) {
    int found = -1;
    for (int i = 0; i < m_filters.size(); ++i) {
      if (m_filters.at(i).name == current.name) {
        found = i;
        break;
      }
    }
    if (found < 0) {
      m_filters.prepend(current);
      found = 0;
    }
    m_filterIndex = found;
  }
  m_choices = optionChoices(options);
  applyCurrentFilter();
}

void ChooserSession::applyCurrentFilter() {
  if (m_filterIndex >= 0 && m_filterIndex < m_filters.size())
    m_filter.setPortalRules(m_filters.at(m_filterIndex).rules);
  else
    m_filter.setPortalRules({});
}

QStringList ChooserSession::filterNames() const {
  QStringList names;
  names.reserve(m_filters.size());
  for (const PortalFilter &f : m_filters)
    names.append(f.name);
  return names;
}

QVariantList ChooserSession::choiceModels() const {
  QVariantList out;
  out.reserve(m_choices.size());
  for (const PortalChoice &c : m_choices) {
    QVariantMap row;
    row.insert(QStringLiteral("id"), c.id);
    row.insert(QStringLiteral("label"), c.label);
    row.insert(QStringLiteral("optionIds"), c.optionIds);
    row.insert(QStringLiteral("optionLabels"), c.optionLabels);
    row.insert(QStringLiteral("value"), c.value);
    out.append(row);
  }
  return out;
}

void ChooserSession::setSaveName(const QString &name) {
  if (m_saveName == name)
    return;
  m_saveName = name;
  emit saveNameChanged();
  emit destPreviewChanged();
}

void ChooserSession::setCurrentFilterIndex(int index) {
  if (m_filters.isEmpty())
    return;
  const int next = qBound(0, index, m_filters.size() - 1);
  if (next == m_filterIndex)
    return;
  m_filterIndex = next;
  applyCurrentFilter();
  emit currentFilterIndexChanged();
}

void ChooserSession::cycleFilter(int delta) {
  if (m_filters.isEmpty())
    return;
  const int n = m_filters.size();
  int next = m_filterIndex + delta;
  next %= n;
  if (next < 0)
    next += n;
  setCurrentFilterIndex(next);
  m_keys.setStatusMessage(m_filters.at(m_filterIndex).name);
}

void ChooserSession::setChoiceValue(int index, const QString &value) {
  if (index < 0 || index >= m_choices.size())
    return;
  if (m_choices[index].value == value)
    return;
  m_choices[index].value = value;
  emit choicesChanged();
}

void ChooserSession::setDelayedReply(const QDBusConnection &connection,
                                     const QDBusMessage &message) {
  m_replyConnection = connection;
  m_replyMessage = message;
  m_hasReply = true;
}

bool ChooserSession::createWindow(QQmlEngine *engine) {
  if (m_done)
    return false;
  if (!engine || m_window)
    return m_window;
  auto *ctx = new QQmlContext(engine->rootContext(), this);
  QQmlComponent component(engine);
#ifdef SYNCHRO_CHOOSER_QML
  component.loadUrl(QUrl::fromLocalFile(QStringLiteral(SYNCHRO_CHOOSER_QML)));
#else
  component.loadFromModule("Synchro", "ChooserWindow");
#endif
  if (component.isError()) {
    std::fprintf(stderr, "synchro: chooser qml: %s\n",
                 qPrintable(component.errorString()));
    return false;
  }
  QObject *obj = component.createWithInitialProperties(
      {{QStringLiteral("chooser"), QVariant::fromValue(this)}}, ctx);
  if (!obj) {
    std::fprintf(stderr, "synchro: chooser qml create failed\n");
    return false;
  }
  obj->setParent(this);
  m_window = qobject_cast<QQuickWindow *>(obj);
  if (!m_window) {
    delete obj;
    return false;
  }
  m_window->setTitle(m_title);
  m_window->setFlags(m_window->flags() | Qt::Dialog);
  QObject::connect(m_window, &QQuickWindow::closing, this,
                   [this] { cancel(); });
  // Map only after title + Dialog are set so Hyprland's float rule matches
  // at first commit (title changes after map are not re-evaluated).
  m_window->setVisible(true);
  m_window->requestActivate();
  return true;
}

void ChooserSession::onAcceptRequested() {
  if (m_done)
    return;
  if (m_overwriteOpen) {
    confirmOverwrite();
    return;
  }
  if (m_kind == Kind::SaveFile) {
    accept();
    return;
  }
  if (m_kind == Kind::SaveFiles) {
    accept();
    return;
  }
  if (m_directory) {
    if (m_host && m_host->isOpen() && !m_host->folderPath().isEmpty() &&
        !DirectoryModel::isVirtualPath(m_host->folderPath())) {
      finishWithPaths({m_host->folderPath()});
      return;
    }
    if (cursorIsDir()) {
      finishWithPaths({m_selection.cursorPath()});
      return;
    }
    if (cursorIsFile()) {
      const QString parent =
          QFileInfo(m_selection.cursorPath()).absolutePath();
      if (!parent.isEmpty() && QFileInfo(parent).isDir() &&
          !DirectoryModel::isVirtualPath(parent)) {
        finishWithPaths({parent});
        return;
      }
    }
    accept();
    return;
  }
  if (m_host && m_host->isOpen()) {
    if (m_host->folderListing()) {
      if (m_host->peekCursorIsDir()) {
        m_host->peekActivate();
        return;
      }
      const QString path = m_host->peekCursorPath();
      if (!path.isEmpty() && QFileInfo(path).isFile())
        finishWithPaths({path});
      return;
    }
    const QString path = m_host->file().isLocalFile()
                             ? m_host->file().toLocalFile()
                             : m_host->peekCursorPath();
    if (!path.isEmpty() && QFileInfo(path).isFile())
      finishWithPaths({path});
    return;
  }
  if (m_multiple && m_selection.selectedCount() > 1) {
    accept();
    return;
  }
  if (cursorIsDir()) {
    m_nav.navigate(m_selection.cursorPath());
    return;
  }
  accept();
}

bool ChooserSession::cursorIsDir() const {
  const QString path = m_selection.cursorPath();
  return !path.isEmpty() && QFileInfo(path).isDir();
}

bool ChooserSession::cursorIsFile() const {
  const QString path = m_selection.cursorPath();
  if (path.isEmpty())
    return false;
  const QFileInfo fi(path);
  return fi.exists() && !fi.isDir();
}

QStringList ChooserSession::collectOpenPaths() const {
  if (m_directory) {
    if (m_host && m_host->isOpen() && !m_host->folderPath().isEmpty() &&
        !DirectoryModel::isVirtualPath(m_host->folderPath()))
      return {m_host->folderPath()};
    if (!DirectoryModel::isVirtualPath(m_model.path()))
      return {m_model.path()};
    return {};
  }
  QStringList sel = m_selection.selectedPaths();
  if (sel.isEmpty()) {
    const QString cursor = m_selection.cursorPath();
    if (!cursor.isEmpty())
      sel.append(cursor);
  }
  QStringList files;
  for (const QString &path : sel) {
    const QFileInfo fi(path);
    if (!fi.exists() || fi.isDir())
      continue;
    files.append(fi.absoluteFilePath());
  }
  if (!m_multiple && files.size() > 1)
    files = QStringList{files.first()};
  return files;
}

QString ChooserSession::destFolder() const {
  // Save As targets the directory being browsed. The cursor is always parked
  // on some row (often the first sorted item), so treating a selected folder
  // or a Miller preview as the destination makes saving depend on incidental
  // focus rather than the location shown in the dialog.
  if (m_kind == Kind::SaveFile) {
    const QString cwd = m_model.path();
    if (!cwd.isEmpty() && !DirectoryModel::isVirtualPath(cwd) &&
        QFileInfo(cwd).isDir())
      return cwd;
    return {};
  }
  if (m_host && m_host->isOpen() && m_host->folderListing() &&
      !m_host->folderPath().isEmpty() &&
      !DirectoryModel::isVirtualPath(m_host->folderPath()))
    return m_host->folderPath();
  if (m_kind == Kind::SaveFiles && cursorIsDir()) {
    const QString path = m_selection.cursorPath();
    if (!path.isEmpty() && !DirectoryModel::isVirtualPath(path) &&
        QFileInfo(path).isDir())
      return path;
  }
  const QString cwd = m_model.path();
  if (!cwd.isEmpty() && !DirectoryModel::isVirtualPath(cwd) &&
      QFileInfo(cwd).isDir())
    return cwd;
  return {};
}

QString ChooserSession::destPreview() const {
  if (m_kind == Kind::SaveFile) {
    const QString dest = saveFileDest();
    if (!dest.isEmpty())
      return dest;
    const QString folder = destFolder();
    if (!folder.isEmpty() && !m_saveName.trimmed().isEmpty())
      return QDir(folder).filePath(m_saveName.trimmed());
    return m_saveName;
  }
  if (m_kind == Kind::SaveFiles)
    return destFolder();
  return {};
}

QStringList ChooserSession::collectSaveFilesPaths() const {
  const QString folder = destFolder();
  if (folder.isEmpty())
    return {};
  if (m_saveFiles.isEmpty())
    return {folder};
  QStringList paths;
  paths.reserve(m_saveFiles.size());
  for (const QString &name : m_saveFiles) {
    const QString destName = FileOpEngine::collisionName(folder, name);
    if (destName.isEmpty())
      continue;
    paths.append(QDir(folder).filePath(destName));
  }
  return paths;
}

QString ChooserSession::saveFileDest() const {
  const QString name = m_saveName.trimmed();
  if (!isValidSaveBaseName(name))
    return {};
  const QString folder = destFolder();
  if (folder.isEmpty())
    return {};
  const QString dest = QDir::cleanPath(QDir(folder).filePath(name));
  if (dest.isEmpty() || !isSaveFilePath(dest))
    return {};
  return dest;
}

void ChooserSession::activateOrAccept() { onAcceptRequested(); }

void ChooserSession::accept() {
  if (m_done)
    return;
  if (m_overwriteOpen) {
    confirmOverwrite();
    return;
  }
  if (m_kind == Kind::SaveFile) {
    const QString dest = saveFileDest();
    if (dest.isEmpty()) {
      m_keys.setStatusMessage(
          isValidSaveBaseName(m_saveName.trimmed())
              ? QStringLiteral("cannot save here")
              : QStringLiteral("need a file name"));
      return;
    }
    if (QFileInfo::exists(dest)) {
      promptOverwrite(QFileInfo(dest).fileName());
      return;
    }
    finishWithPaths({dest});
    return;
  }
  if (m_kind == Kind::SaveFiles) {
    const QStringList paths = collectSaveFilesPaths();
    if (paths.isEmpty())
      return;
    finishWithPaths(paths);
    return;
  }
  const QStringList paths = collectOpenPaths();
  if (paths.isEmpty())
    return;
  finishWithPaths(paths);
}

void ChooserSession::cancel() {
  QVariantMap results;
  results.insert(QStringLiteral("uris"), QStringList());
  finish(1, results);
}

void ChooserSession::fail() {
  QVariantMap results;
  results.insert(QStringLiteral("uris"), QStringList());
  finish(2, results);
}

void ChooserSession::confirmOverwrite() {
  if (!m_overwriteOpen)
    return;
  const QString dest = saveFileDest();
  if (dest.isEmpty()) {
    dismissOverwrite();
    return;
  }
  m_keys.setChooserPromptOpen(false);
  m_overwriteOpen = false;
  emit overwriteOpenChanged();
  finishWithPaths({dest});
}

void ChooserSession::dismissOverwrite() {
  if (!m_overwriteOpen)
    return;
  m_keys.setChooserPromptOpen(false);
  m_overwriteOpen = false;
  m_overwriteName.clear();
  emit overwriteOpenChanged();
}

void ChooserSession::promptOverwrite(const QString &name) {
  m_overwriteName = name;
  m_overwriteOpen = true;
  m_keys.setChooserPromptOpen(true);
  emit overwriteOpenChanged();
}

void ChooserSession::finishWithPaths(const QStringList &paths) {
  QStringList allowed = paths;
  if (m_kind == Kind::SaveFile) {
    if (allowed.size() != 1 || !isSaveFilePath(allowed.first()))
      return;
  }
  const QStringList uris = fileUris(allowed);
  if (uris.isEmpty())
    return;
  const QString tmpRoot = QDir::tempPath();
  for (const QString &path : allowed) {
    const QFileInfo fi(path);
    if (!fi.exists())
      continue;
    const QString abs = fi.absoluteFilePath();
    if (abs.startsWith(tmpRoot))
      continue;
    m_recents.record(abs, QString());
  }
  finish(0, successResults(allowed));
}

QVariant ChooserSession::currentFilterVariant() const {
  if (m_filterIndex < 0 || m_filterIndex >= m_filters.size())
    return {};
  return QVariant::fromValue(toDBus(m_filters.at(m_filterIndex)));
}

QVariant ChooserSession::choicesVariant() const {
  if (m_choices.isEmpty())
    return {};
  QList<ChoicePairDBus> pairs;
  pairs.reserve(m_choices.size());
  for (const PortalChoice &c : m_choices)
    pairs.append(ChoicePairDBus{c.id, c.value});
  return QVariant::fromValue(pairs);
}

QVariantMap ChooserSession::successResults(const QStringList &paths) const {
  QVariantMap results;
  results.insert(QStringLiteral("uris"), fileUris(paths));
  if (m_kind == Kind::OpenFile)
    results.insert(QStringLiteral("writable"), false);
  const QVariant filter = currentFilterVariant();
  if (filter.isValid())
    results.insert(QStringLiteral("current_filter"), filter);
  const QVariant choices = choicesVariant();
  if (choices.isValid())
    results.insert(QStringLiteral("choices"), choices);
  return results;
}

void ChooserSession::finish(uint response, const QVariantMap &results) {
  if (m_done)
    return;
  m_done = true;
  if (m_window) {
    m_window->disconnect(this);
    m_window->hide();
  }
  emit finished(response, results);
}

QString PortalService::defaultServiceName() {
  return QStringLiteral("org.freedesktop.impl.portal.desktop.synchro");
}

QString PortalService::objectPath() {
  return QStringLiteral("/org/freedesktop/portal/desktop");
}

PortalService::PortalService(QObject *parent)
    : QObject(parent), m_connection(QDBusConnection::sessionBus()) {
  registerPortalMetaTypes();
  m_engine = new QQmlEngine(this);
  m_engine->addImportPath(QCoreApplication::applicationDirPath() +
                          QStringLiteral("/qml"));
  ThumbImageProvider::install(m_engine);
  IconImageProvider::install(m_engine);
  m_registry = new HandlerRegistry;
  m_registry->scan();
  m_loader = new HandlerLoader();
  m_xdg = new XdgOpen();
  m_xdg->load();
  m_mime = new MimeMap();
}

PortalService::~PortalService() {
  const auto sessions = m_sessions.values();
  m_sessions.clear();
  for (ChooserSession *s : sessions) {
    if (!s->done())
      s->cancel();
    s->deleteLater();
  }
  if (m_connection.isConnected() && !m_serviceName.isEmpty()) {
    m_connection.unregisterObject(objectPath());
    m_connection.unregisterService(m_serviceName);
  }
  delete m_loader;
  delete m_registry;
  delete m_xdg;
  delete m_mime;
}

bool PortalService::start(const QDBusConnection &connection,
                          const QString &serviceName) {
  m_connection = connection;
  m_serviceName = serviceName;
  if (!m_connection.isConnected()) {
    m_error = QStringLiteral("no session bus");
    return false;
  }
  if (!m_adaptor)
    m_adaptor = new FileChooserAdaptor(this);
  if (!m_connection.registerService(serviceName)) {
    m_error = QStringLiteral("could not own %1").arg(serviceName);
    return false;
  }
  if (!m_connection.registerObject(objectPath(), this,
                                   QDBusConnection::ExportAdaptors)) {
    m_connection.unregisterService(serviceName);
    m_error = QStringLiteral("could not export %1").arg(objectPath());
    return false;
  }
  return true;
}

ChooserSession *PortalService::beginRequest(
    ChooserSession::Kind kind, const QDBusObjectPath &handle,
    const QString &appId, const QString &parentWindow, const QString &title,
    const QVariantMap &options, const QDBusConnection &connection,
    const QDBusMessage &message) {
  Q_UNUSED(parentWindow);
  const bool directory = optionBool(options, "directory", false) ||
                         kind == ChooserSession::Kind::SaveFiles;
  const QString winTitle = ChooserSession::windowTitle(kind, directory, title,
                                                       appId);
  std::fprintf(stderr, "synchro: portal %s app_id=%s title=%s\n",
               kindName(kind),
               qPrintable(appId.isEmpty() ? QStringLiteral("-") : appId),
               qPrintable(winTitle.isEmpty() ? title : winTitle));
  if (ChooserSession *existing = m_sessions.value(handle.path(), nullptr)) {
    existing->cancel();
  }
  auto *session =
      new ChooserSession(kind, handle, winTitle, options, m_registry, m_loader,
                         m_xdg, m_mime, m_engine, this);
  session->setDelayedReply(connection, message);
  new PortalRequestAdaptor(session);
  QDBusConnection conn = connection;
  if (!conn.registerObject(handle.path(), session,
                           QDBusConnection::ExportAdaptors)) {
    std::fprintf(stderr, "synchro: portal: could not export Request %s\n",
                 qPrintable(handle.path()));
  }
  m_sessions.insert(handle.path(), session);
  connect(session, &ChooserSession::finished, this,
          [this, session](uint response, const QVariantMap &results) {
            onSessionFinished(session, response, results);
          });
  QMetaObject::invokeMethod(
      session,
      [session, engine = m_engine] {
        if (session->done())
          return;
        if (!session->createWindow(engine))
          session->fail();
      },
      Qt::QueuedConnection);
  return session;
}

ChooserSession *PortalService::openStandalone(ChooserSession::Kind kind,
                                              const QString &title,
                                              const QVariantMap &options) {
  static int standalone = 0;
  const QString path =
      QStringLiteral("/org/freedesktop/portal/desktop/request/standalone/%1")
          .arg(++standalone);
  auto *session =
      new ChooserSession(kind, QDBusObjectPath(path), title, options,
                         m_registry, m_loader, m_xdg, m_mime, m_engine, this);
  m_sessions.insert(path, session);
  connect(session, &ChooserSession::finished, this,
          [this, session](uint response, const QVariantMap &results) {
            onSessionFinished(session, response, results);
          });
  if (!session->createWindow(m_engine)) {
    m_sessions.remove(path);
    session->disconnect(this);
    delete session;
    return nullptr;
  }
  return session;
}

ChooserSession *PortalService::session(const QString &handlePath) const {
  return m_sessions.value(handlePath, nullptr);
}

QStringList PortalService::sessionHandles() const { return m_sessions.keys(); }

void PortalService::onSessionFinished(ChooserSession *session, uint response,
                                      const QVariantMap &results) {
  if (!session)
    return;
  const QString path = session->handle().path();
  if (session->hasDelayedReply()) {
    QDBusMessage reply = session->replyMessage().createReply();
    reply << response << results;
    session->replyConnection().send(reply);
    session->replyConnection().unregisterObject(path);
  }
  m_sessions.remove(path);
  emit lastSessionFinished(response, results);
  session->deleteLater();
}
