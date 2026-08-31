#include "SemanticSearchBridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

SemanticSearchBridge::SemanticSearchBridge(QObject *parent) : QObject(parent) {
  m_timeout = new QTimer(this);
  m_timeout->setSingleShot(true);
  m_timeout->setInterval(5 * 60 * 1000);
  connect(m_timeout, &QTimer::timeout, this, [this] {
    if (!m_process)
      return;
    m_error = QStringLiteral("Semantic search timed out.");
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->kill();
    process->deleteLater();
    emit changed();
  });
}

SemanticSearchBridge::~SemanticSearchBridge() { cancel(); }

bool SemanticSearchBridge::start(const QString &queryText,
                                 const QString &cwdText, int limit) {
  const QString cleanQuery = queryText.trimmed();
  if (cleanQuery.isEmpty()) {
    m_error = QStringLiteral("Describe the images you want to find.");
    emit changed();
    return false;
  }
  if (cleanQuery.size() > 500) {
    m_error = QStringLiteral("Semantic image queries are limited to 500 characters.");
    emit changed();
    return false;
  }
  cancel();
  m_query = cleanQuery;
  m_cwd = QDir::cleanPath(
      QFileInfo(cwdText.isEmpty() ? QDir::homePath() : cwdText)
          .absoluteFilePath());
  m_error.clear();
  m_output.clear();
  m_errorOutput.clear();

  auto *process = new QProcess(this);
  m_process = process;
  process->setProgram(QCoreApplication::applicationFilePath());
  process->setArguments(
      {QStringLiteral("semantic"), QStringLiteral("search"), cleanQuery,
       QStringLiteral("--cwd"), m_cwd, QStringLiteral("--limit"),
       QString::number(qBound(1, limit, 200)), QStringLiteral("--compact")});
  connect(process, &QProcess::readyReadStandardOutput, this,
          [this, process] { m_output += process->readAllStandardOutput(); });
  connect(process, &QProcess::readyReadStandardError, this,
          [this, process] {
            m_errorOutput += process->readAllStandardError();
          });
  connect(process, &QProcess::errorOccurred, this,
          [this, process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart && m_process == process) {
              m_error = QStringLiteral("Could not start the semantic search worker.");
              finish(false);
            }
          });
  connect(process,
          qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
          [this, process](int code, QProcess::ExitStatus status) {
            if (m_process != process)
              return;
            finish(status == QProcess::NormalExit && code == 0);
          });
  process->start();
  m_timeout->start();
  emit changed();
  return true;
}

void SemanticSearchBridge::finish(bool processOk) {
  QProcess *process = m_process;
  m_process = nullptr;
  m_timeout->stop();
  if (process) {
    m_output += process->readAllStandardOutput();
    m_errorOutput += process->readAllStandardError();
    process->deleteLater();
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(m_output, &parseError);
  const QVariantMap result = document.isObject()
                                 ? document.object().toVariantMap()
                                 : QVariantMap();
  if (processOk && result.value(QStringLiteral("ok")).toBool()) {
    m_error.clear();
    emit changed();
    emit resultReady(
        result.value(QStringLiteral("label"),
                     QStringLiteral("Semantic images"))
            .toString(),
        result);
    return;
  }

  if (m_error.isEmpty()) {
    m_error = result.value(QStringLiteral("error")).toString();
    if (m_error.isEmpty() && !m_errorOutput.trimmed().isEmpty())
      m_error = QString::fromUtf8(m_errorOutput.trimmed());
    if (m_error.isEmpty())
      m_error = parseError.error == QJsonParseError::NoError
                    ? QStringLiteral("Semantic search failed.")
                    : QStringLiteral("Semantic worker returned invalid output.");
  }
  emit changed();
}

void SemanticSearchBridge::cancel() {
  m_timeout->stop();
  if (m_process) {
    QProcess *process = m_process;
    m_process = nullptr;
    disconnect(process, nullptr, this, nullptr);
    process->kill();
    process->deleteLater();
  }
  m_output.clear();
  m_errorOutput.clear();
}
