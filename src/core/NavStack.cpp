#include "NavStack.h"

#include "DirectoryModel.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

NavStack::NavStack(DirectoryModel *model, QObject *parent)
    : QObject(parent), m_model(model) {
  if (!m_model)
    return;
  connect(m_model, &DirectoryModel::aboutToNavigate, this,
          &NavStack::onAboutToNavigate);
  connect(m_model, &DirectoryModel::pathChanged, this,
          &NavStack::onPathChanged);
  if (!m_model->path().isEmpty())
    m_current.path = m_model->path();
}

void NavStack::setLiveFilter(const QString &filter) { m_liveFilter = filter; }

QString NavStack::homePath() const {
  const QString home = QDir::homePath();
  const QString canon = QFileInfo(home).canonicalFilePath();
  return canon.isEmpty() ? home : canon;
}

NavStack::Frame NavStack::snapshot() const {
  Frame f;
  if (!m_model)
    return f;
  f.path = m_model->path();
  f.selectedName = m_model->currentName();
  f.viewMode = QStringLiteral("list");
  f.filter = m_liveFilter;
  return f;
}

void NavStack::restore(const Frame &frame) {
  if (!m_model)
    return;
  m_restoring = true;
  m_model->setPath(frame.path, frame.selectedName);
  m_liveFilter = frame.filter;
  emit filterRestored(frame.filter);
  m_current = frame;
  m_restoring = false;
  emit historyChanged();
}

void NavStack::onAboutToNavigate() {
  if (m_restoring || !m_model)
    return;
  if (m_model->path().isEmpty())
    return;
  m_back.append(snapshot());
  m_forward.clear();
}

void NavStack::onPathChanged() {
  if (m_restoring || !m_model)
    return;
  m_current.path = m_model->path();
  m_current.selectedName.clear();
  m_current.viewMode = QStringLiteral("list");
  m_current.filter.clear();
  emit historyChanged();
}

void NavStack::navigate(const QString &path) {
  if (m_model)
    m_model->setPath(path);
}

void NavStack::goBack() {
  if (!m_model || m_back.isEmpty())
    return;
  const Frame dest = m_back.takeLast();
  m_forward.append(snapshot());
  restore(dest);
}

void NavStack::goForward() {
  if (!m_model || m_forward.isEmpty())
    return;
  const Frame dest = m_forward.takeLast();
  m_back.append(snapshot());
  restore(dest);
}

void NavStack::goUp() {
  if (!m_model)
    return;
  const QString path = m_model->path();
  if (path.isEmpty())
    return;
  if (DirectoryModel::isVirtualPath(path)) {
    const QString dest = m_model->returnPath();
    if (!dest.isEmpty())
      m_model->setPath(dest);
    return;
  }
  const QString root = m_model->volumeRoot();
  if (!root.isEmpty() && (path == root || QDir::cleanPath(path) == root)) {
    m_model->setPath(QStringLiteral("volumes://"));
    return;
  }
  QDir dir(path);
  const QString name = QFileInfo(QDir::cleanPath(path)).fileName();
  if (!dir.cdUp())
    return;
  m_model->setPath(dir.absolutePath(), name);
}

void NavStack::goHome() { navigate(homePath()); }

void NavStack::goTrash() { navigate(QStringLiteral("trash://")); }

void NavStack::goRecent() { navigate(QStringLiteral("recent://")); }

void NavStack::goVolumes() { navigate(QStringLiteral("volumes://")); }

QVariantList NavStack::pathSegments() const {
  return segmentsFor(m_model ? m_model->path() : QString());
}

QVariantList NavStack::segmentsFor(const QString &path) const {
  QVariantList segs;
  if (path.isEmpty())
    return segs;

  const auto add = [&](const QString &label, const QString &target) {
    QVariantMap m;
    m.insert(QStringLiteral("label"), label);
    m.insert(QStringLiteral("path"), target);
    segs.append(m);
  };

  if (DirectoryModel::isSearchPath(path)) {
    add(QStringLiteral("search"), path);
    return segs;
  }
  if (path.startsWith(QLatin1String("trash:"))) {
    add(QStringLiteral("trash"), path);
    return segs;
  }
  if (path.startsWith(QLatin1String("recent:"))) {
    add(QStringLiteral("recent"), path);
    return segs;
  }
  if (path.startsWith(QLatin1String("volumes:"))) {
    add(QStringLiteral("volumes"), QStringLiteral("volumes://"));
    return segs;
  }

  if (m_model) {
    const QString root = m_model->volumeRoot();
    if (!root.isEmpty() &&
        (path == root || path.startsWith(root + QLatin1Char('/')))) {
      add(QStringLiteral("volumes"), QStringLiteral("volumes://"));
      const QString label = QFileInfo(root).fileName().isEmpty()
                                ? root
                                : QFileInfo(root).fileName();
      add(label, root);
      if (path == root)
        return segs;
      const QStringList parts =
          path.mid(root.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
      QString acc = root;
      for (const QString &part : parts) {
        acc += QLatin1Char('/') + part;
        add(part, acc);
      }
      return segs;
    }
  }

  const QString home = homePath();
  QString prefix;
  QString rest;
  if (path == home || path.startsWith(home + QLatin1Char('/'))) {
    add(QStringLiteral("~"), home);
    if (path == home)
      return segs;
    prefix = home;
    rest = path.mid(home.size());
  } else {
    add(QStringLiteral("/"), QStringLiteral("/"));
    if (path == QLatin1String("/"))
      return segs;
    rest = path;
  }

  const QStringList parts = rest.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  QString acc = prefix;
  for (const QString &part : parts) {
    acc += QLatin1Char('/') + part;
    add(part, acc);
  }
  return segs;
}
