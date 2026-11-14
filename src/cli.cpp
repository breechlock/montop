// The command line follows Hype's (MIT, David Heinemeier Hansson).
#include "cli.h"

#include "monitor.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <cstdio>

namespace {

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

// Sizes for a terminal or an agent: plain spaces, no locale.
QString bytes(double value) { return Monitor::bytes(value).replace(QChar(0x202F), ' '); }
QString rate(double value) { return value < 0 ? QString("-") : bytes(value) + "/s"; }
QJsonValue rateJson(double value) { return value < 0 ? QJsonValue() : QJsonValue(std::round(value)); }
double round1(double value) { return std::round(value * 10) / 10; }

QString duration(qint64 seconds) {
    const qint64 d = seconds / 86400, h = seconds % 86400 / 3600, m = seconds % 3600 / 60;
    return d > 0 ? QString("%1d %2h").arg(d).arg(h) : h > 0 ? QString("%1h %2m").arg(h).arg(m) : QString("%1m").arg(m);
}

const QStringList sorts{"cpu", "memory", "read", "write"};

ProcessModel::Column columnFor(const QString &sort) {
    return sort == "memory" ? ProcessModel::Memory
           : sort == "read" ? ProcessModel::Read
           : sort == "write" ? ProcessModel::Write
                             : ProcessModel::Cpu;
}

QList<ProcessInfo> sortedProcesses(const Monitor &monitor, const QString &sort) {
    QList<ProcessInfo> list = monitor.processList();
    const auto column = columnFor(sort);
    std::sort(list.begin(), list.end(), [&](const ProcessInfo &a, const ProcessInfo &b) {
        return ProcessModel::lessThan(a, b, column, true);
    });
    return list;
}

QList<GroupInfo> sortedGroups(const Monitor &monitor, const QString &sort) {
    QList<GroupInfo> list = monitor.groupList();
    const auto column = columnFor(sort);
    std::sort(list.begin(), list.end(), [&](const GroupInfo &a, const GroupInfo &b) {
        return ProcessModel::lessThan(a, b, column, true);
    });
    return list;
}

QJsonObject processJson(const ProcessInfo &p, const QHash<QString, QString> &appNames) {
    return {
        {"pid", p.pid}, {"ppid", p.ppid}, {"name", p.name}, {"app", appNames.value(p.group)},
        {"user", p.user}, {"state", QString(QChar(p.state))}, {"threads", p.threads},
        {"cpu_percent", round1(p.cpu)}, {"memory_bytes", double(p.memory)},
        {"read_bytes_per_second", rateJson(p.readRate)}, {"write_bytes_per_second", rateJson(p.writeRate)},
        {"command", p.command},
    };
}

QJsonObject appJson(const GroupInfo &g, const QHash<int, ProcessInfo> &byPid, const QHash<QString, QString> &appNames) {
    // The few processes that matter most inside the app, so an agent can
    // name the tab or build step without a second call.
    QList<ProcessInfo> members;
    for (int pid : g.pids)
        if (byPid.contains(pid))
            members.append(byPid.value(pid));
    std::sort(members.begin(), members.end(), [](const ProcessInfo &a, const ProcessInfo &b) {
        return ProcessModel::lessThan(a, b, ProcessModel::Cpu, true);
    });
    QJsonArray top, pids;
    for (int i = 0; i < members.size() && i < 3; ++i)
        top.append(processJson(members[i], appNames));
    for (int pid : g.pids)
        pids.append(pid);
    return {
        {"name", g.name}, {"kind", g.kind}, {"key", g.key}, {"units", QJsonArray::fromStringList(g.units)},
        {"user", g.user}, {"processes", int(g.pids.size())}, {"threads", g.threads},
        {"cpu_percent", round1(g.cpu)}, {"memory_bytes", double(g.memory)},
        {"read_bytes_per_second", rateJson(g.readRate)}, {"write_bytes_per_second", rateJson(g.writeRate)},
        {"pids", pids}, {"top_processes", top},
        {"paused", g.frozen}, {"cpu_limit_percent", g.cpuLimit > 0 ? QJsonValue(round1(g.cpuLimit)) : QJsonValue()},
        {"controllable", g.control.isEmpty()},
    };
}

struct Command {
    QCommandLineParser parser;
    Command(const QString &name, const QString &description) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
    }
    void json() { parser.addOption({"json", "Print the result as JSON"}); }
    void sampling() {
        parser.addOption({"interval", "Milliseconds between the two samples rates are measured over (default 1000)",
                          "ms", "1000"});
        parser.addOption({"sort", "Order by cpu, memory, read, or write (default cpu)", "column", "cpu"});
    }
    QString argument(int index) const { return parser.positionalArguments().value(index); }

    // Rates need two readings; take them `interval` apart.
    bool sample(Monitor &monitor) const {
        bool ok = false;
        const int interval = parser.value("interval").toInt(&ok);
        if (!ok || interval < 100 || interval > 60000) {
            print(stderr, "--interval takes milliseconds between 100 and 60000.");
            return false;
        }
        if (!sorts.contains(parser.value("sort"))) {
            print(stderr, "--sort takes one of: " + sorts.join(", ") + ".");
            return false;
        }
        if (!monitor.error().isEmpty()) {
            print(stderr, monitor.error());
            return false;
        }
        QThread::msleep(ulong(interval));
        monitor.sample();
        return true;
    }
};

QHash<QString, QString> appNames(const Monitor &monitor) {
    QHash<QString, QString> names;
    for (const GroupInfo &g : monitor.groupList())
        names.insert(g.key, g.name);
    return names;
}

// One column of a table, cut with an ellipsis so the next column never
// touches it.
QString pad(const QString &text, int width, bool right = false) {
    const int room = width - 1;
    const QString cut = text.size() > room ? text.left(room - 1) + "…" : text;
    return right ? cut.rightJustified(width) : cut.leftJustified(width);
}

int snapshot(const QStringList &arguments) {
    Command command("snapshot", "What the system is doing now: CPU, memory, disks, network, and the busiest apps and processes.");
    command.json();
    command.sampling();
    command.parser.addOption({"top", "How many apps and processes to list; 0 lists all (default 10)", "n", "10"});
    command.parser.process(arguments);
    bool ok = false;
    const int top = command.parser.value("top").toInt(&ok);
    if (!ok || top < 0)
        return fail("--top takes a number, 0 for all.");

    Monitor monitor;
    if (!command.sample(monitor))
        return 1;
    const QString sort = command.parser.value("sort");
    if (command.parser.isSet("json")) {
        print(snapshotJson(monitor, top, sort));
        return 0;
    }

    QStringList out;
    const auto load = monitor.loadAverage();
    out << QString("%1 · %2 · up %3 · load %4 %5 %6")
               .arg(monitor.hostname(), monitor.kernel(), duration(monitor.uptime()))
               .arg(load.value(0), 0, 'f', 2).arg(load.value(1), 0, 'f', 2).arg(load.value(2), 0, 'f', 2);
    QString cpu = QString("CPU      %1% of %2 cores").arg(qRound(monitor.cpuUsage() * 100)).arg(monitor.coreCount());
    if (monitor.cpuFrequency() > 0)
        cpu += QString(" · %1 GHz").arg(monitor.cpuFrequency() / 1000, 0, 'f', 2);
    if (monitor.cpuTemperature() >= 0)
        cpu += QString(" · %1 °C").arg(qRound(monitor.cpuTemperature()));
    out << cpu;
    out << QString("Memory   %1 used of %2, %3 available · swap %4 of %5")
               .arg(bytes(monitor.memoryUsed()), bytes(monitor.memoryTotal()), bytes(monitor.memoryAvailable()),
                    bytes(monitor.swapUsed()), bytes(monitor.swapTotal()));
    for (const QVariant &d : monitor.disks()) {
        const QVariantMap disk = d.toMap();
        out << QString("Disk     %1  read %2  write %3  busy %4%")
                   .arg(disk["name"].toString(), rate(disk["readRate"].toDouble()), rate(disk["writeRate"].toDouble()))
                   .arg(qRound(disk["busy"].toDouble() * 100));
    }
    for (const QVariant &f : monitor.filesystems()) {
        const QVariantMap fs = f.toMap();
        out << QString("Storage  %1  %2 used of %3 (%4%)")
                   .arg(fs["path"].toString(), bytes(fs["used"].toDouble()), bytes(fs["total"].toDouble()))
                   .arg(qRound(fs["share"].toDouble() * 100));
    }
    for (const QVariant &n : monitor.networks()) {
        const QVariantMap net = n.toMap();
        if (net["up"].toBool())
            out << QString("Network  %1  down %2  up %3")
                       .arg(net["name"].toString(), rate(net["rxRate"].toDouble()), rate(net["txRate"].toDouble()));
    }

    const auto groups = sortedGroups(monitor, sort);
    out << "" << QString("Apps by %1").arg(sort);
    out << pad("APP", 28) + pad("CPU%", 7, true) + pad("MEMORY", 11, true) + pad("READ/S", 12, true)
               + pad("WRITE/S", 12, true) + pad("PROCS", 7, true);
    for (int i = 0; i < groups.size() && (top == 0 || i < top); ++i) {
        const GroupInfo &g = groups[i];
        out << pad(g.name, 28) + pad(QString::number(g.cpu, 'f', 1), 7, true) + pad(bytes(g.memory), 11, true)
                   + pad(rate(g.readRate), 12, true) + pad(rate(g.writeRate), 12, true)
                   + pad(QString::number(g.pids.size()), 7, true);
    }

    const auto names = appNames(monitor);
    const auto processes = sortedProcesses(monitor, sort);
    out << "" << QString("Processes by %1").arg(sort);
    out << pad("PID", 8, true) + "  " + pad("NAME", 20) + pad("APP", 18) + pad("CPU%", 7, true) + pad("MEMORY", 11, true)
               + "  COMMAND";
    for (int i = 0; i < processes.size() && (top == 0 || i < top); ++i) {
        const ProcessInfo &p = processes[i];
        out << pad(QString::number(p.pid), 8, true) + "  " + pad(p.name, 20) + pad(names.value(p.group), 18)
                   + pad(QString::number(p.cpu, 'f', 1), 7, true) + pad(bytes(p.memory), 11, true) + "  "
                   + p.command.left(80);
    }
    out << "" << "CPU% is of one core; a process busy on two cores shows 200.";
    print(stdout, out.join('\n'));
    return 0;
}

// The app a person or agent means: its key, its name, a unit, or the PID of
// any of its processes. A name that fits several is an error, not a guess.
const GroupInfo *findApp(const Monitor &monitor, const QString &query, QString &error) {
    const QList<GroupInfo> &groups = monitor.groupList();
    bool isPid = false;
    const int pid = query.toInt(&isPid);
    QList<const GroupInfo *> found;
    for (const GroupInfo &g : groups) {
        if (g.key == query)
            return &g;
        if (isPid ? g.pids.contains(pid)
                  : g.name.compare(query, Qt::CaseInsensitive) == 0 || g.units.contains(query)
                        || g.controlUnits.contains(query))
            found.append(&g);
    }
    if (found.size() == 1)
        return found.first();
    if (found.isEmpty()) {
        error = "No app called " + query + ". vitals apps lists them.";
    } else {
        QStringList keys;
        for (const GroupInfo *g : found)
            keys << g->key;
        error = query + " could be " + keys.join(", ") + ". Use one of those keys.";
    }
    return nullptr;
}

QJsonObject controlJson(const GroupInfo &g) {
    return {{"name", g.name}, {"key", g.key}, {"units", QJsonArray::fromStringList(g.controlUnits)},
            {"paused", g.frozen}, {"cpu_limit_percent", g.cpuLimit > 0 ? QJsonValue(round1(g.cpuLimit)) : QJsonValue()}};
}

// pause, resume, and limit: they change the system, so they say exactly
// what they did, and a pause lasts until resume or logout.
int control(const QStringList &arguments) {
    const QString name = arguments.value(1);
    Command command(name, name == "pause" ? "Pause an app: freeze every process in it until vitals resume."
                          : name == "resume" ? "Resume an app that was paused."
                                             : "Cap an app's CPU, in percent of one core, or lift the cap with off.");
    command.json();
    command.parser.addPositionalArgument("app", "The app's name or key (from vitals apps), a unit, or a PID in it", "<app>");
    if (name == "limit")
        command.parser.addPositionalArgument("percent", "Percent of one core, as in the CPU column (200 is two cores), or off",
                                             "<percent|off>");
    command.parser.process(arguments);
    const QString query = command.argument(1);
    if (query.isEmpty())
        return fail("Name the app: vitals " + name + " <app>.");
    double percent = 0;
    if (name == "limit") {
        const QString value = command.argument(2);
        bool ok = value == "off";
        if (!ok)
            percent = QString(value).remove('%').toDouble(&ok);
        if (!ok || percent < 0)
            return fail("Give the limit in percent of one core, like 50 or 200, or off.");
    }
    Monitor monitor;
    if (!monitor.error().isEmpty())
        return fail(monitor.error());
    QString error;
    const GroupInfo *g = findApp(monitor, query, error);
    if (!g)
        return fail(error);
    const QString key = g->key;
    error = name == "limit" ? monitor.setAppCpuLimit(key, percent) : monitor.setAppPaused(key, name == "pause");
    if (!error.isEmpty())
        return fail(error);
    g = monitor.findGroup(key);
    if (command.parser.isSet("json")) {
        print(controlJson(*g));
        return 0;
    }
    print(stdout, name == "pause"    ? "Paused " + g->name + ". Resume it with: vitals resume " + key
                  : name == "resume" ? "Resumed " + g->name + "."
                  : percent > 0      ? "Limited " + g->name + " to " + QString::number(g->cpuLimit, 'f', 0)
                                      + "% of one core until logout. Lift it with: vitals limit " + key + " off"
                                     : "Lifted the CPU limit on " + g->name + ".");
    return 0;
}

int appsCommand(const QStringList &arguments) {
    Command command("apps", "Every app and service, with what its processes use together.");
    command.json();
    command.sampling();
    command.parser.process(arguments);
    Monitor monitor;
    if (!command.sample(monitor))
        return 1;
    const QString sort = command.parser.value("sort");
    const auto groups = sortedGroups(monitor, sort);

    if (command.parser.isSet("json")) {
        QHash<int, ProcessInfo> byPid;
        for (const ProcessInfo &p : monitor.processList())
            byPid.insert(p.pid, p);
        const auto names = appNames(monitor);
        QJsonArray list;
        for (const GroupInfo &g : groups)
            list.append(appJson(g, byPid, names));
        print(QJsonObject{{"apps", list}, {"cores", monitor.coreCount()}});
        return 0;
    }
    QStringList out;
    out << pad("APP", 28) + pad("KIND", 9) + pad("CPU%", 7, true) + pad("MEMORY", 11, true) + pad("READ/S", 12, true)
               + pad("WRITE/S", 12, true) + pad("PROCS", 7, true) + "  STATE";
    for (const GroupInfo &g : groups) {
        QStringList state;
        if (g.frozen)
            state << "paused";
        if (g.cpuLimit > 0)
            state << "limit " + QString::number(g.cpuLimit, 'f', 0) + "%";
        out << pad(g.name, 28) + pad(g.kind, 9) + pad(QString::number(g.cpu, 'f', 1), 7, true)
                   + pad(bytes(g.memory), 11, true) + pad(rate(g.readRate), 12, true) + pad(rate(g.writeRate), 12, true)
                   + pad(QString::number(g.pids.size()), 7, true) + "  " + state.join(", ");
    }
    print(stdout, out.join('\n'));
    return 0;
}

// As Hype and the hey and basecamp tools do: one shared copy, linked into
// Claude Code, found directly by Codex.
int skill(const QStringList &arguments) {
    Command command("skill", "Print the agent skill, or install it for your coding agents with: skill install.");
    command.parser.addPositionalArgument("install", "Copy to ~/.agents/skills/vitals and link into ~/.claude/skills", "[install]");
    command.parser.process(arguments);
    QFile bundled(":/skill.md");
    if (!bundled.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray text = bundled.readAll();
    if (command.argument(1).isEmpty()) {
        fputs(text.constData(), stdout);
        return 0;
    }
    if (command.argument(1) != "install")
        return fail("Use vitals skill to print the skill, or vitals skill install.");
    const QDir home = QDir::home();
    const QString directory = home.filePath(".agents/skills/vitals");
    QSaveFile file(directory + "/SKILL.md");
    if (!QDir().mkpath(directory) || !file.open(QIODevice::WriteOnly) || file.write(text) < 0 || !file.commit())
        return fail("Could not write " + file.fileName());
    print(stdout, "Installed " + file.fileName());
    if (!home.exists(".claude"))
        return 0;
    const QString link = home.filePath(".claude/skills/vitals");
    const QFileInfo existing(link);
    if (existing.isSymLink())
        QFile::remove(link);
    else if (existing.exists())
        return fail(link + " exists and is not a link; leaving it alone.");
    if (!home.mkpath(".claude/skills") || !QFile::link("../../.agents/skills/vitals", link))
        return fail("Could not link " + link);
    print(stdout, "Linked " + link);
    return 0;
}

int help(const QStringList &arguments) {
    const QString topic = arguments.value(2);
    if (isCliCommand(topic) && topic != "help")
        return runCli({arguments[0], topic, "--help"});
    print(stdout, "Usage: vitals <command> [options]\n\n" + cliSummary());
    return 0;
}

} // namespace

QJsonObject snapshotJson(const Monitor &monitor, int top, const QString &sort) {
    QJsonArray cores;
    for (const QVariant &c : monitor.cores()) {
        const QVariantMap core = c.toMap();
        cores.append(QJsonObject{{"usage_percent", round1(core["usage"].toDouble() * 100)},
                                 {"frequency_mhz", qRound(core["mhz"].toDouble())}});
    }
    QJsonArray load;
    for (qreal v : monitor.loadAverage())
        load.append(v);

    QJsonArray disks;
    for (const QVariant &d : monitor.disks()) {
        const QVariantMap disk = d.toMap();
        disks.append(QJsonObject{
            {"name", disk["name"].toString()}, {"model", disk["model"].toString()},
            {"read_bytes_per_second", std::round(disk["readRate"].toDouble())},
            {"write_bytes_per_second", std::round(disk["writeRate"].toDouble())},
            {"busy_percent", round1(disk["busy"].toDouble() * 100)},
        });
    }
    QJsonArray filesystems;
    for (const QVariant &f : monitor.filesystems()) {
        const QVariantMap fs = f.toMap();
        filesystems.append(QJsonObject{
            {"path", fs["path"].toString()}, {"device", fs["device"].toString()}, {"type", fs["type"].toString()},
            {"total_bytes", fs["total"].toDouble()}, {"used_bytes", fs["used"].toDouble()},
            {"available_bytes", fs["available"].toDouble()}, {"used_percent", round1(fs["share"].toDouble() * 100)},
        });
    }
    QJsonArray networks;
    for (const QVariant &n : monitor.networks()) {
        const QVariantMap net = n.toMap();
        networks.append(QJsonObject{
            {"name", net["name"].toString()}, {"up", net["up"].toBool()},
            {"rx_bytes_per_second", std::round(net["rxRate"].toDouble())},
            {"tx_bytes_per_second", std::round(net["txRate"].toDouble())},
            {"rx_total_bytes", net["rxTotal"].toDouble()}, {"tx_total_bytes", net["txTotal"].toDouble()},
        });
    }

    QHash<int, ProcessInfo> byPid;
    for (const ProcessInfo &p : monitor.processList())
        byPid.insert(p.pid, p);
    const auto names = appNames(monitor);
    QJsonArray apps;
    const auto groups = sortedGroups(monitor, sort);
    for (int i = 0; i < groups.size() && (top == 0 || i < top); ++i)
        apps.append(appJson(groups[i], byPid, names));
    QJsonArray processes;
    const auto sortedList = sortedProcesses(monitor, sort);
    for (int i = 0; i < sortedList.size() && (top == 0 || i < top); ++i)
        processes.append(processJson(sortedList[i], names));

    return {
        {"time", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"host", QJsonObject{{"hostname", monitor.hostname()}, {"kernel", monitor.kernel()},
                             {"uptime_seconds", monitor.uptime()}}},
        {"cpu", QJsonObject{
                    {"model", monitor.cpuModel()}, {"cores", monitor.coreCount()},
                    {"usage_percent", round1(monitor.cpuUsage() * 100)},
                    {"frequency_mhz", qRound(monitor.cpuFrequency())},
                    {"temperature_celsius", monitor.cpuTemperature() >= 0 ? QJsonValue(round1(monitor.cpuTemperature())) : QJsonValue()},
                    {"load_average", load}, {"per_core", cores},
                }},
        {"memory", QJsonObject{
                       {"total_bytes", monitor.memoryTotal()}, {"used_bytes", monitor.memoryUsed()},
                       {"available_bytes", monitor.memoryAvailable()}, {"cached_bytes", monitor.memoryCached()},
                       {"swap_total_bytes", monitor.swapTotal()}, {"swap_used_bytes", monitor.swapUsed()},
                   }},
        {"disks", disks},
        {"filesystems", filesystems},
        {"network", networks},
        {"sorted_by", sort},
        {"apps", apps},
        {"processes", processes},
        {"notes", "cpu_percent is relative to one core (a process busy on two cores shows 200). "
                  "App totals come from the app's cgroups and include child processes that already exited. "
                  "A null rate means the kernel did not let this user read it."},
    };
}

QString cliSummary() {
    return "Window:\n"
           "  open                 Open the monitor\n\n"
           "Commands that need no display (vitals help <command> for options):\n"
           "  snapshot             CPU, memory, disks, network, and the busiest apps and processes\n"
           "  apps                 Every app and service, with what its processes use together\n"
           "  pause <app>          Freeze every process in an app until you resume it\n"
           "  resume <app>         Resume a paused app\n"
           "  limit <app> <%|off>  Cap an app's CPU in percent of one core, until logout\n"
           "  skill [install]      Print the skill for coding agents, or install it";
}

bool isCliCommand(const QString &word) {
    return QStringList{"snapshot", "apps", "pause", "resume", "limit", "skill", "help", "--help", "-h"}.contains(word);
}

int runCli(const QStringList &arguments) {
    const QString command = arguments.value(1);
    return command == "snapshot" ? snapshot(arguments)
           : command == "apps"   ? appsCommand(arguments)
           : command == "pause" || command == "resume" || command == "limit" ? control(arguments)
           : command == "skill"  ? skill(arguments)
                                 : help(arguments);
}
