#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

// Parsers for the kernel's text interfaces under /proc. They take the file
// contents rather than paths, so tests feed them fixed text and the Monitor
// decides where the files come from.
namespace procfs {

// Reads a whole proc or sysfs file. Those report a size of 0, so this reads
// until EOF instead of trusting stat(). Returns an empty array on any error.
QByteArray readFile(const QString &path, qint64 limit = 1 << 20);

struct CpuTimes {
    quint64 busy = 0;
    quint64 total = 0;
};

// /proc/stat: the aggregate "cpu" line first, then cpu0, cpu1, ...
QList<CpuTimes> parseStat(const QByteArray &text);

// /proc/meminfo, every value converted from kB to bytes.
QHash<QString, quint64> parseMeminfo(const QByteArray &text);

struct DiskCounters {
    QString name;
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
    quint64 busyMs = 0;
};

// /proc/diskstats. Sectors there are always 512 bytes, whatever the device.
QList<DiskCounters> parseDiskstats(const QByteArray &text);

struct NetCounters {
    QString name;
    quint64 rxBytes = 0;
    quint64 txBytes = 0;
};

// /proc/net/dev.
QList<NetCounters> parseNetDev(const QByteArray &text);

struct PidStat {
    int pid = 0;
    int ppid = 0;
    QString comm;
    char state = '?';
    quint64 cpuTicks = 0;
    int threads = 0;
    quint64 rssPages = 0;
};

// /proc/<pid>/stat. The command name sits in parentheses and may itself
// contain spaces and parentheses, so fields are counted from the last ')'.
bool parsePidStat(const QByteArray &text, PidStat &out);

struct PidIo {
    quint64 readBytes = 0;
    quint64 writeBytes = 0;
};

// /proc/<pid>/io, which only the process owner (or root) may read.
bool parsePidIo(const QByteArray &text, PidIo &out);

// /proc/<pid>/cmdline, NUL separated, joined with spaces.
QString parseCmdline(const QByteArray &text);

struct Mount {
    QString device;
    QString path;
    QString type;
};

// /proc/self/mounts, limited to filesystems that hold data on a block device,
// one entry per device (Btrfs mounts the same device once per subvolume, and
// the shortest mount path is the one people recognise).
QList<Mount> parseMounts(const QByteArray &text);

} // namespace procfs
