#include "run.h"

#include "monitor.h"
#include "procfs.h"

#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <sys/resource.h>

using procfs::readFile;

namespace {

// "usage_usec 123" style files.
quint64 keyed(const QByteArray &text, const QByteArray &key) {
    for (const QByteArray &line : text.split('\n'))
        if (line.startsWith(key + ' '))
            return line.mid(key.size() + 1).trimmed().toULongLong();
    return 0;
}

// The "total=" of a pressure file's "some" line: microseconds in which at
// least one task waited.
quint64 stalled(const QByteArray &text) {
    for (const QByteArray &line : text.split('\n'))
        if (line.startsWith("some "))
            for (const QByteArray &word : line.split(' '))
                if (word.startsWith("total="))
                    return word.mid(6).toULongLong();
    return 0;
}

QString seconds(double s) {
    return s < 10 ? QString::number(s, 'f', 2) + " s" : s < 600 ? QString::number(s, 'f', 1) + " s"
                                                                : QString("%1 min %2 s").arg(int(s) / 60).arg(int(s) % 60);
}
QString size(double value) { return Monitor::bytes(value).replace(QChar(0x202F), ' '); }

// A command name as systemd wants it in a unit name: anything but
// letters, digits, and _ . escaped as \xNN, so `vitals open` shows the run as
// that command.
QString unitId(const QString &command) {
    QString out;
    for (const char byte : QFileInfo(command).fileName().toUtf8()) {
        const uchar c = uchar(byte);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.')
            out += QChar(c);
        else
            out += QString("\\x%1").arg(c, 2, 16, QLatin1Char('0'));
    }
    return out.isEmpty() ? QString("command") : out;
}

// Ctrl+C belongs to the command. Vitals waits it out to report on it, with a
// handler rather than SIG_IGN: an ignored signal would be inherited by the
// command, a handler is reset when it starts.
void carryOn(int) {}

} // namespace

CgroupUsage readCgroupUsage(const QString &path, const QSet<QString> &physicalDevices) {
    CgroupUsage u;
    const QByteArray cpu = readFile(path + "/cpu.stat", 4096);
    if (cpu.isNull())
        return u;
    u.ok = true;
    u.cpuUsec = keyed(cpu, "usage_usec");
    u.userUsec = keyed(cpu, "user_usec");
    u.systemUsec = keyed(cpu, "system_usec");
    u.memory = readFile(path + "/memory.current", 64).trimmed().toULongLong();
    u.memoryPeak = readFile(path + "/memory.peak", 64).trimmed().toULongLong();
    const QByteArray memory = readFile(path + "/memory.stat", 16384);
    u.ownMemory = keyed(memory, "anon") + keyed(memory, "shmem") + keyed(memory, "kernel");
    const QByteArray io = readFile(path + "/io.stat", 16384);
    u.hasIo = !io.isNull();
    for (const QByteArray &line : io.split('\n')) {
        const QList<QByteArray> fields = line.split(' ');
        if (fields.isEmpty() || !physicalDevices.contains(QString::fromLatin1(fields[0])))
            continue;
        for (const QByteArray &f : fields) {
            if (f.startsWith("rbytes="))
                u.readBytes += f.mid(7).toULongLong();
            else if (f.startsWith("wbytes="))
                u.writeBytes += f.mid(7).toULongLong();
        }
    }
    u.cpuWaitUsec = stalled(readFile(path + "/cpu.pressure", 1024));
    u.memoryWaitUsec = stalled(readFile(path + "/memory.pressure", 1024));
    u.ioWaitUsec = stalled(readFile(path + "/io.pressure", 1024));
    u.processesPeak = readFile(path + "/pids.peak", 64).trimmed().toInt();
    return u;
}

QJsonObject runJson(const RunReport &r) {
    const CgroupUsage &u = r.usage;
    const double cpuSeconds = u.cpuUsec / 1e6;
    return {
        {"command", QJsonArray::fromStringList(r.command)},
        {"exit_code", r.exitCode},
        {"seconds", std::round(r.seconds * 1000) / 1000},
        {"cpu", QJsonObject{
                    {"seconds", std::round(cpuSeconds * 1000) / 1000},
                    {"user_seconds", std::round(u.userUsec / 1e3) / 1000},
                    {"system_seconds", std::round(u.systemUsec / 1e3) / 1000},
                    {"average_percent", r.seconds > 0 ? std::round(1000 * cpuSeconds / r.seconds) / 10 : 0},
                    {"peak_percent", std::round(r.cpuPeak * 10) / 10},
                }},
        {"memory", QJsonObject{
                       {"peak_bytes", double(r.ownMemoryPeak)},
                       {"peak_with_cache_bytes", u.memoryPeak ? QJsonValue(double(u.memoryPeak)) : QJsonValue()},
                   }},
        {"disk", QJsonObject{{"read_bytes", double(u.readBytes)}, {"written_bytes", double(u.writeBytes)}}},
        {"waited_seconds", QJsonObject{
                               {"cpu", std::round(u.cpuWaitUsec / 1e3) / 1000},
                               {"memory", std::round(u.memoryWaitUsec / 1e3) / 1000},
                               {"disk", std::round(u.ioWaitUsec / 1e3) / 1000},
                           }},
        {"processes_peak", u.processesPeak},
    };
}

QString runText(const RunReport &r) {
    const CgroupUsage &u = r.usage;
    const double cpuSeconds = u.cpuUsec / 1e6;
    QStringList out;
    out << "vitals run: " + r.command.join(' ');
    out << "  Time       " + seconds(r.seconds);
    out << QString("  CPU        %1 of CPU time (user %2, system %3), %4 cores on average, %5 at the peak")
               .arg(seconds(cpuSeconds), seconds(u.userUsec / 1e6), seconds(u.systemUsec / 1e6))
               .arg(r.seconds > 0 ? cpuSeconds / r.seconds : 0, 0, 'f', 1)
               .arg(r.cpuPeak / 100, 0, 'f', 1);
    QString memory = "  Memory     peak " + size(r.ownMemoryPeak);
    if (u.memoryPeak)
        memory += ", " + size(u.memoryPeak) + " with file cache";
    out << memory;
    out << "  Disk       read " + size(u.readBytes) + ", wrote " + size(u.writeBytes);
    out << "  Waited     " + QString("on CPU %1, on memory %2, on disk %3")
                                 .arg(seconds(u.cpuWaitUsec / 1e6), seconds(u.memoryWaitUsec / 1e6), seconds(u.ioWaitUsec / 1e6));
    if (u.processesPeak > 0)
        out << QString("  Processes  %1 at most at once").arg(u.processesPeak);
    out << QString("  Exit       %1").arg(r.exitCode);
    return out.join('\n');
}

int runCommand(const QStringList &arguments) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Run a command and report what it used: time, CPU, peak memory, disk IO, how long it waited on CPU, "
        "memory, and disk, and how many processes it ran at most. It counts every process the command starts, "
        "including ones that have already exited. The report goes to stderr, so the command's own output stays "
        "as it was, and vitals exits with the command's exit code.");
    parser.addHelpOption();
    parser.addPositionalArgument("run", "", "run");
    parser.addPositionalArgument("command", "The command and its arguments", "<command> [args...]");
    parser.addOption({"json", "Report as JSON (still on stderr)"});
    parser.addOption({"interval", "Milliseconds between samples of memory and CPU peaks (default 250)", "ms", "250"});
    // Only Vitals' own options come before the command; from its name on,
    // everything is the command's, so `vitals run make -j8` works without --.
    QStringList own = arguments.mid(0, 2), command;
    for (int i = 2; i < arguments.size(); ++i) {
        const QString &a = arguments[i];
        if (a == "--") {
            command = arguments.mid(i + 1);
            break;
        }
        if (!a.startsWith('-')) {
            command = arguments.mid(i);
            break;
        }
        own << a;
        if (a == "--interval" && i + 1 < arguments.size())
            own << arguments[++i];
    }
    parser.process(own);

    if (command.isEmpty()) {
        fprintf(stderr, "Name a command: vitals run make -j8\n");
        return 1;
    }
    bool ok = false;
    const int interval = parser.value("interval").toInt(&ok);
    if (!ok || interval < 50 || interval > 10000) {
        fprintf(stderr, "--interval takes milliseconds between 50 and 10000.\n");
        return 1;
    }

    Monitor monitor; // For the disks and the cgroup root.
    QTemporaryDir scratch;
    const QString statusFile = scratch.filePath("status");
    // The command runs in a scope of its own, so its cgroup counts it and
    // everything it starts. A scope is gone the moment its last process
    // exits, taking the final counts with it, so a shell waits there
    // instead, until Vitals has read them, or a minute if Vitals is gone.
    // `trap :` keeps that shell from dying with the command on Ctrl+C, and
    // isn't inherited by it.
    const QString unit = QString("app-vitals-%1-%2.scope")
                             .arg(unitId(command.first()))
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    const QString script = "trap : INT QUIT TERM; \"$@\"; echo $? > \"$VITALS_RUN_STATUS\"; exec sleep 60";

    struct sigaction handler = {};
    handler.sa_handler = carryOn;
    sigaction(SIGINT, &handler, nullptr);
    sigaction(SIGQUIT, &handler, nullptr);

    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setInputChannelMode(QProcess::ForwardedInputChannel);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("VITALS_RUN_STATUS", statusFile);
    process.setProcessEnvironment(environment);
    QElapsedTimer clock;
    clock.start();
    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    rusage before = {};
    getrusage(RUSAGE_CHILDREN, &before);
    process.start("systemd-run", QStringList{"--user", "--scope", "--quiet", "--collect", "--unit", unit, "--",
                                             "sh", "-c", script, "sh"} + command);
    if (!process.waitForStarted(5000)) {
        fprintf(stderr, "Couldn't start systemd-run: %s\n", qPrintable(process.errorString()));
        return 1;
    }

    // systemd-run moves itself into the scope, then becomes the shell.
    const QString root = "/sys/fs/cgroup";
    QString cgroup;
    RunReport report;
    report.command = command;
    CgroupUsage last, previous;
    qint64 previousAt = 0;
    while (true) {
        if (cgroup.isEmpty()) {
            for (const QByteArray &line : readFile(QString("/proc/%1/cgroup").arg(process.processId()), 4096).split('\n'))
                if (line.startsWith("0::") && line.contains(unit.toUtf8()))
                    cgroup = root + QString::fromUtf8(line.mid(3)).trimmed();
        }
        const bool done = QFile::exists(statusFile) && QFile(statusFile).size() > 0;
        if (!cgroup.isEmpty()) {
            const CgroupUsage now = readCgroupUsage(cgroup, monitor.physicalDevices());
            if (now.ok) {
                const qint64 at = clock.elapsed();
                report.ownMemoryPeak = std::max(report.ownMemoryPeak, now.ownMemory);
                if (previous.ok && at > previousAt)
                    report.cpuPeak = std::max(report.cpuPeak, 100.0 * (now.cpuUsec - std::min(now.cpuUsec, previous.cpuUsec))
                                                                  / ((at - previousAt) * 1000.0));
                previous = now;
                previousAt = at;
                last = now;
            }
        }
        if (done)
            break;
        if (process.state() == QProcess::NotRunning)
            break; // systemd-run itself failed; it said why on stderr.
        process.waitForFinished(interval);
    }
    // When the command ended, not when this loop noticed.
    const QFileInfo statusInfo(statusFile);
    report.seconds = statusInfo.exists() ? std::max<qint64>(0, statusInfo.lastModified().toMSecsSinceEpoch() - startedAt) / 1000.0
                                         : clock.elapsed() / 1000.0;

    QFile status(statusFile);
    if (!status.open(QIODevice::ReadOnly)) {
        // No status: the scope never ran the command.
        process.waitForFinished(1000);
        return process.exitCode() ? process.exitCode() : 1;
    }
    report.exitCode = status.readAll().trimmed().toInt();
    report.usage = last;
    process.kill(); // The waiting shell, now sleep; the scope ends with it.
    process.waitForFinished(2000);

    // Without the IO controller (systemd doesn't hand it to user units by
    // default) the cgroup has no io.stat. The kernel's per-process
    // accounting stands in: the shell has waited for the command, which
    // waited for its own children, so it adds up the whole tree.
    if (!report.usage.hasIo) {
        rusage after = {};
        getrusage(RUSAGE_CHILDREN, &after);
        report.usage.readBytes = quint64(after.ru_inblock - before.ru_inblock) * 512;
        report.usage.writeBytes = quint64(after.ru_oublock - before.ru_oublock) * 512;
    }
    // A run shorter than a few samples can't show its peak; it was at least
    // the average.
    if (report.seconds > 0)
        report.cpuPeak = std::max(report.cpuPeak, 100.0 * report.usage.cpuUsec / 1e6 / report.seconds);

    // The shell that waited ran too, but for a few milliseconds.
    if (parser.isSet("json"))
        fprintf(stderr, "%s", QJsonDocument(runJson(report)).toJson().constData());
    else
        fprintf(stderr, "\n%s\n", qPrintable(runText(report)));
    return report.exitCode;
}
