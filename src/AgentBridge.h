#pragma once

#include <QJsonObject>

// Omarchy-facing machine interface over Synchro's durable file catalog.
// The protocol surface stays independent of QML so the same executable can
// serve JSON on the command line and MCP over stdio.
class AgentBridge {
public:
  static QJsonObject handleRequest(const QJsonObject &request);
  static int runStdio();
};
