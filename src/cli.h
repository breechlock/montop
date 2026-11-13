#pragma once

#include <QJsonObject>
#include <QStringList>

class Monitor;

// Commands that need no display, for scripts and agents: vitals snapshot,
// apps, skill, help. The window is `vitals open`.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);

// What `snapshot --json` prints, from a monitor that has taken two samples.
// `top` limits the apps and processes listed; 0 lists them all.
QJsonObject snapshotJson(const Monitor &monitor, int top, const QString &sort);
