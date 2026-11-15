#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

class Monitor;

// Commands that need no display, for scripts and agents: montop snapshot,
// apps, skill, help. The window is `montop open`.
bool isCliCommand(const QString &word);
QString cliSummary();
int runCli(const QStringList &arguments);

// The packaged skill, which `montop skill install` links rather than copies,
// so it follows package upgrades. Omarchy ships its own skills the same way.
inline constexpr char PackagedSkill[] = "/usr/share/montop/skills/montop";

// Links the skill into ~/.agents/skills and the skill folder of each coding
// agent that is set up in home, from `packaged` when it holds a SKILL.md and
// otherwise from a copy of `text` in ~/.agents/skills/montop. Removing takes
// out only what Montop put there. Each returns a line per change, and an
// error per place it left alone.
struct SkillChanges {
    QStringList done;
    QStringList errors;
};
SkillChanges installSkill(const QString &home, const QString &packaged, const QByteArray &text);
SkillChanges removeSkill(const QString &home, const QString &packaged);

// What `snapshot --json` prints, from a monitor that has taken two samples.
// `top` limits the apps and processes listed; 0 lists them all.
QJsonObject snapshotJson(const Monitor &monitor, int top, const QString &sort);
