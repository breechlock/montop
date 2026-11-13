#include "monitor.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSysInfo>
#include <algorithm>

#include <cerrno>
#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

using namespace procfs;

static QString readLine(const QString &path) {
    return QString::fromUtf8(readFile(path, 4096)).trimmed();
}

Monitor::Monitor(const QString &procRoot, const QString &sysRoot, QObject *parent)
    : QObject(parent), m_proc(procRoot), m_sys(sysRoot) {
    m_pageSize = sysconf(_SC_PAGESIZE);
    m_hostname = QSysInfo::machineHostName();
    m_kernel = QSysInfo::kernelVersion();
    for (const QByteArray &line : readFile(m_proc + "/cpuinfo").split('\n')) {
        if (line.startsWith("model name")) {
            m_cpuModel = QString::fromUtf8(line.mid(line.indexOf(':') + 1)).simplified();
            // "Intel(R) Core(TM) Ultra 7 155H" reads better without the marks.
            m_cpuModel.remove("(R)").remove("(TM)").replace(" CPU", "");
            m_cpuModel = m_cpuModel.simplified();
            break;
        }
    }
    m_temperatureInput = findTemperatureInput();

    m_clock.start();
    // Graphs scroll between samples, so a late tick shows as a stutter; a
    // precise timer keeps the beat even.
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &Monitor::tick);

    sample();
}

void Monitor::start() {
    // Rates need two samples; the constructor took the first, and this takes
    // the second soon rather than showing zeros for a whole interval.
    QTimer::singleShot(250, this, [this] {
        if (!m_paused)
            tick();
    });
    m_timer.start();
}

void Monitor::setInterval(int ms) {
    ms = std::clamp(ms, 125, 4000);
    if (ms == m_timer.interval())
        return;
    m_timer.setInterval(ms);
    emit intervalChanged();
}

void Monitor::faster() {
    setInterval(interval() / 2);
}

void Monitor::slower() {
    setInterval(interval() * 2);
}

void Monitor::setPaused(bool paused) {
    if (paused == m_paused)
        return;
    m_paused = paused;
    if (m_paused) {
        m_timer.stop();
    } else {
        // The first sample after a pause would spread the paused time over
        // one interval; resetting the baseline keeps the rates honest.
        m_lastSample = m_lastProcessSample = -1;
        sample();
        m_timer.start();
    }
    emit pausedChanged();
}

int Monitor::signalProcess(int pid, bool force) {
    if (pid <= 1)
        return EPERM;
    return ::kill(pid, force ? SIGKILL : SIGTERM) == 0 ? 0 : errno;
}

QString Monitor::bytes(double value) {
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int unit = 0;
    while (value >= 1024 && unit < 5) {
        value /= 1024;
        ++unit;
    }
    const int precision = unit == 0 || value >= 100 ? 0 : value >= 10 ? 1 : 2;
    return QLocale().toString(value, 'f', precision) + QChar(0x202F) + QLatin1String(units[unit]);
}

QString Monitor::rate(double bytesPerSecond) {
    return bytes(bytesPerSecond) + "/s";
}

void Monitor::push(const QString &series, qreal value) {
    QList<qreal> &history = m_history[series];
    history.append(value);
    if (history.size() > HistoryLength)
        history.remove(0, history.size() - HistoryLength);
}

int Monitor::processInterval() const {
    // Scanning every process costs far more than the system counters and a
    // table rewritten four times a second can't be read anyway, so the
    // process list keeps a calmer pace of its own.
    return std::max(1000, interval() * 2);
}

void Monitor::sample() {
    sampleSystem();
    sampleProcessTable();
    emit sampled();
}

void Monitor::tick() {
    sampleSystem();
    // Half an interval of slack, so timer jitter doesn't skip a beat.
    if (m_lastProcessSample < 0 || m_clock.elapsed() - m_lastProcessSample >= processInterval() - interval() / 2)
        sampleProcessTable();
    emit sampled();
}

void Monitor::sampleSystem() {
    const qint64 now = m_clock.elapsed();
    const double seconds = m_lastSample < 0 ? 0 : (now - m_lastSample) / 1000.0;
    m_lastSample = now;

    sampleCpu();
    sampleMemory();
    sampleDisks(seconds);
    sampleNetworks(seconds);
}

void Monitor::sampleProcessTable() {
    const qint64 now = m_clock.elapsed();
    const double seconds = m_lastProcessSample < 0 ? 0 : (now - m_lastProcessSample) / 1000.0;
    m_lastProcessSample = now;

    // The process scan runs at its own pace, so it measures CPU time against
    // its own previous reading, not the last system sample's.
    const quint64 total = m_prevCpu.isEmpty() ? 0 : m_prevCpu[0].total;
    const quint64 delta = seconds > 0 && total > m_processCpuTotal ? total - m_processCpuTotal : 0;
    m_processCpuTotal = total;

    sampleFilesystems();
    sampleProcesses(seconds, delta);
}

void Monitor::sampleCpu() {
    const QList<CpuTimes> times = parseStat(readFile(m_proc + "/stat"));
    if (times.isEmpty()) {
        m_error = tr("Can't read %1. Vitals needs the proc filesystem to see the system.").arg(m_proc + "/stat");
        return;
    }
    m_error.clear();

    auto usage = [](const CpuTimes &now, const CpuTimes &before) {
        const quint64 total = now.total - before.total;
        return total == 0 ? 0.0 : double(now.busy - before.busy) / double(total);
    };

    const bool hasPrevious = m_prevCpu.size() == times.size();
    m_cpuUsage = hasPrevious ? usage(times[0], m_prevCpu[0]) : 0;
    push("cpu", m_cpuUsage);

    QVariantList cores;
    double frequencySum = 0;
    int frequencyCount = 0;
    for (int i = 1; i < times.size(); ++i) {
        const double mhz = readLine(QString("%1/devices/system/cpu/cpu%2/cpufreq/scaling_cur_freq").arg(m_sys).arg(i - 1))
                               .toDouble() / 1000.0;
        if (mhz > 0) {
            frequencySum += mhz;
            ++frequencyCount;
        }
        cores.append(QVariantMap{{"usage", hasPrevious ? usage(times[i], m_prevCpu[i]) : 0.0}, {"mhz", mhz}});
    }
    m_cores = cores;
    m_cpuFrequency = frequencyCount ? frequencySum / frequencyCount : 0;
    m_prevCpu = times;

    m_cpuTemperature = m_temperatureInput.isEmpty() ? -1 : readLine(m_temperatureInput).toDouble() / 1000.0;

    const QStringList load = readLine(m_proc + "/loadavg").split(' ');
    m_loadAverage = {load.value(0).toDouble(), load.value(1).toDouble(), load.value(2).toDouble()};
    m_uptime = qint64(readLine(m_proc + "/uptime").section(' ', 0, 0).toDouble());
}

// The package temperature where the driver reports one, so the number
// matches what the fans react to; otherwise the first CPU sensor found.
QString Monitor::findTemperatureInput() const {
    static const QStringList drivers{"coretemp", "k10temp", "zenpower", "cpu_thermal", "acpitz"};
    static const QStringList labels{"Package id 0", "Tctl", "Tdie"};
    const QDir hwmon(m_sys + "/class/hwmon");
    for (const QString &driver : drivers) {
        for (const QString &entry : hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
            const QString base = hwmon.filePath(entry);
            if (readLine(base + "/name") != driver)
                continue;
            for (int i = 1; i < 64; ++i) {
                const QString input = QString("%1/temp%2_input").arg(base).arg(i);
                if (QFileInfo::exists(input) && labels.contains(readLine(QString("%1/temp%2_label").arg(base).arg(i))))
                    return input;
            }
            if (QFileInfo::exists(base + "/temp1_input"))
                return base + "/temp1_input";
        }
    }
    return {};
}

void Monitor::sampleMemory() {
    const auto info = parseMeminfo(readFile(m_proc + "/meminfo"));
    m_memTotal = info.value("MemTotal");
    m_memFree = info.value("MemFree");
    // MemAvailable is the kernel's own estimate of what can be handed out
    // without swapping; "used" is everything else, as free(1) reports it.
    m_memAvailable = info.value("MemAvailable", info.value("MemFree"));
    m_memUsed = std::max(0.0, m_memTotal - m_memAvailable);
    m_memCached = info.value("Cached") + info.value("Buffers") + info.value("SReclaimable");
    m_swapTotal = info.value("SwapTotal");
    m_swapUsed = std::max(0.0, m_swapTotal - double(info.value("SwapFree")));
    push("memory", m_memTotal > 0 ? m_memUsed / m_memTotal : 0);
}

void Monitor::sampleDisks(double seconds) {
    // Whole physical devices only: /sys/block lists disks, not partitions.
    // Loop and RAM devices and empty slots are noise, and device-mapper
    // volumes (LUKS, LVM) repeat the traffic of the disk underneath them.
    const QDir block(m_sys + "/block");
    const QStringList devices = block.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    QHash<QString, DiskCounters> counters;
    for (const DiskCounters &d : parseDiskstats(readFile(m_proc + "/diskstats")))
        counters.insert(d.name, d);

    QVariantList disks;
    QHash<QString, DiskCounters> previous;
    for (const QString &name : devices) {
        if (name.startsWith("loop") || name.startsWith("ram") || name.startsWith("zram") || name.startsWith("dm-")
            || !counters.contains(name))
            continue;
        if (readLine(block.filePath(name + "/size")).toULongLong() == 0)
            continue;
        const DiskCounters now = counters.value(name);
        previous.insert(name, now);
        double readRate = 0, writeRate = 0, busy = 0;
        if (seconds > 0 && m_prevDisks.contains(name)) {
            const DiskCounters before = m_prevDisks.value(name);
            readRate = (now.readBytes - before.readBytes) / seconds;
            writeRate = (now.writeBytes - before.writeBytes) / seconds;
            busy = std::min(1.0, (now.busyMs - before.busyMs) / (seconds * 1000));
        }
        push("disk/" + name + "/read", readRate);
        push("disk/" + name + "/write", writeRate);
        // The model says more than "nvme0n1" to most people.
        const QString model = readLine(block.filePath(name + "/device/model"));
        disks.append(QVariantMap{
            {"name", name},
            {"model", model},
            {"readRate", readRate},
            {"writeRate", writeRate},
            {"busy", busy},
        });
    }
    m_prevDisks = previous;
    m_disks = disks;
}

void Monitor::sampleFilesystems() {
    QVariantList filesystems;
    for (const Mount &mount : parseMounts(readFile(m_proc + "/self/mounts"))) {
        struct statvfs fs;
        if (statvfs(QFile::encodeName(mount.path).constData(), &fs) != 0 || fs.f_blocks == 0)
            continue;
        const double total = double(fs.f_blocks) * fs.f_frsize;
        const double available = double(fs.f_bavail) * fs.f_frsize;
        const double used = total - double(fs.f_bfree) * fs.f_frsize;
        filesystems.append(QVariantMap{
            {"path", mount.path}, {"device", mount.device}, {"type", mount.type},
            {"total", total}, {"used", used}, {"available", available},
            {"share", total > 0 ? used / (used + available) : 0.0},
        });
    }
    m_filesystems = filesystems;
}

void Monitor::sampleNetworks(double seconds) {
    struct Row { QVariantMap map; bool up; double total; };
    QList<Row> rows;
    QHash<QString, NetCounters> previous;
    for (const NetCounters &now : parseNetDev(readFile(m_proc + "/net/dev"))) {
        if (now.name == "lo" || now.name.startsWith("veth"))
            continue;
        previous.insert(now.name, now);
        double rx = 0, tx = 0;
        if (seconds > 0 && m_prevNet.contains(now.name)) {
            const NetCounters before = m_prevNet.value(now.name);
            rx = (now.rxBytes - before.rxBytes) / seconds;
            tx = (now.txBytes - before.txBytes) / seconds;
        }
        push("net/" + now.name + "/rx", rx);
        push("net/" + now.name + "/tx", tx);
        const bool up = readLine(QString("%1/class/net/%2/operstate").arg(m_sys, now.name)) == "up";
        if (!up && now.rxBytes + now.txBytes == 0)
            continue;
        rows.append({QVariantMap{
                         {"name", now.name}, {"up", up},
                         {"rxRate", rx}, {"txRate", tx},
                         {"rxTotal", double(now.rxBytes)}, {"txTotal", double(now.txBytes)},
                     },
                     up, double(now.rxBytes + now.txBytes)});
    }
    // Connected interfaces first, busiest on top.
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.up != b.up ? a.up : a.total > b.total;
    });
    QVariantList networks;
    for (const Row &row : rows)
        networks.append(row.map);
    m_prevNet = previous;
    m_networks = networks;
}

QString Monitor::userName(uint uid) {
    const auto cached = m_users.constFind(uid);
    if (cached != m_users.constEnd())
        return *cached;
    struct passwd pwd, *result = nullptr;
    char buffer[4096];
    QString name = getpwuid_r(uid, &pwd, buffer, sizeof buffer, &result) == 0 && result
                       ? QString::fromLocal8Bit(pwd.pw_name) : QString::number(uid);
    m_users.insert(uid, name);
    return name;
}

void Monitor::sampleProcesses(double seconds, quint64 cpuTotalDelta) {
    DIR *dir = opendir(QFile::encodeName(m_proc).constData());
    if (!dir)
        return;

    // A process's share is measured against one core's worth of ticks, so a
    // process saturating two cores shows 200 %, as in top and htop.
    const int cores = std::max(1, int(m_cores.size()));
    const double ticksPerCore = cpuTotalDelta > 0 ? double(cpuTotalDelta) / cores : 0;

    QList<ProcessInfo> processes;
    QHash<int, PidHistory> seen;
    int tasks = 0;
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] < '1' || entry->d_name[0] > '9')
            continue;
        const QString base = m_proc + '/' + QLatin1String(entry->d_name);
        PidStat stat;
        if (!parsePidStat(readFile(base + "/stat", 4096), stat))
            continue; // Exited between readdir() and here.

        ProcessInfo p;
        p.pid = stat.pid;
        p.ppid = stat.ppid;
        p.state = stat.state;
        p.threads = stat.threads;
        p.kernel = stat.pid == 2 || stat.ppid == 2;
        p.memory = stat.rssPages * quint64(m_pageSize);
        p.memoryShare = m_memTotal > 0 ? p.memory / m_memTotal : 0;
        tasks += std::max(1, stat.threads);

        struct stat info;
        if (::stat(QFile::encodeName(base).constData(), &info) == 0)
            p.user = userName(info.st_uid);

        PidHistory history;
        const auto before = m_prevPids.constFind(p.pid);
        const bool known = before != m_prevPids.constEnd() && before->comm == stat.comm;
        history.ticks = stat.cpuTicks;
        history.comm = stat.comm;

        // The command line rarely changes once a process has exec'd, and
        // exec changes the comm too, so it's read again only then.
        if (known) {
            history.command = before->command;
            history.name = before->name;
        } else {
            history.command = parseCmdline(readFile(base + "/cmdline", 8192));
            // comm is cut at 15 characters; argv[0] usually has the whole name.
            const QString argv0 = QFileInfo(history.command.section(' ', 0, 0)).fileName();
            history.name = !argv0.isEmpty() && argv0.startsWith(stat.comm) ? argv0 : stat.comm;
        }
        p.name = history.name;
        p.command = history.command.isEmpty() ? '[' + stat.comm + ']' : history.command;

        if (known && ticksPerCore > 0)
            p.cpu = 100.0 * double(stat.cpuTicks - std::min(stat.cpuTicks, before->ticks)) / ticksPerCore;

        history.hasIo = parsePidIo(readFile(base + "/io", 4096), history.io);
        if (history.hasIo && known && before->hasIo && seconds > 0) {
            p.readRate = (history.io.readBytes - std::min(history.io.readBytes, before->io.readBytes)) / seconds;
            p.writeRate = (history.io.writeBytes - std::min(history.io.writeBytes, before->io.writeBytes)) / seconds;
        } else if (history.hasIo) {
            p.readRate = p.writeRate = 0;
        }

        seen.insert(p.pid, history);
        processes.append(p);
    }
    closedir(dir);

    m_prevPids = seen;
    m_taskCount = tasks;
    m_processes.update(processes);
}
