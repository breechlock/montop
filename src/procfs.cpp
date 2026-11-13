#include "procfs.h"

#include <QFile>
#include <QSet>

#include <fcntl.h>
#include <unistd.h>

namespace procfs {

QByteArray readFile(const QString &path, qint64 limit) {
    // Plain POSIX reads: the process list opens a few thousand of these a
    // second, and QFile's buffering and bookkeeping show up in a profile.
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {};
    QByteArray data;
    char buffer[8192];
    while (data.size() < limit) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n <= 0)
            break;
        data.append(buffer, n);
    }
    ::close(fd);
    return data;
}

QList<CpuTimes> parseStat(const QByteArray &text) {
    QList<CpuTimes> result;
    for (const QByteArray &line : text.split('\n')) {
        if (!line.startsWith("cpu"))
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 5)
            continue;
        // user nice system idle iowait irq softirq steal; guest time is
        // already counted in user, so adding it again would double it.
        quint64 values[8] = {};
        for (int i = 0; i < 8 && i + 1 < fields.size(); ++i)
            values[i] = fields[i + 1].toULongLong();
        CpuTimes times;
        for (quint64 v : values)
            times.total += v;
        times.busy = times.total - values[3] - values[4];
        result.append(times);
    }
    return result;
}

QHash<QString, quint64> parseMeminfo(const QByteArray &text) {
    QHash<QString, quint64> result;
    for (const QByteArray &line : text.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QList<QByteArray> rest = line.mid(colon + 1).simplified().split(' ');
        quint64 value = rest.value(0).toULongLong();
        if (rest.value(1) == "kB")
            value *= 1024;
        result.insert(QString::fromLatin1(line.left(colon)), value);
    }
    return result;
}

QList<DiskCounters> parseDiskstats(const QByteArray &text) {
    QList<DiskCounters> result;
    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> f = line.simplified().split(' ');
        if (f.size() < 14)
            continue;
        DiskCounters d;
        d.name = QString::fromLatin1(f[2]);
        d.readBytes = f[5].toULongLong() * 512;
        d.writeBytes = f[9].toULongLong() * 512;
        d.busyMs = f[12].toULongLong();
        result.append(d);
    }
    return result;
}

QList<NetCounters> parseNetDev(const QByteArray &text) {
    QList<NetCounters> result;
    for (const QByteArray &line : text.split('\n')) {
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QList<QByteArray> f = line.mid(colon + 1).simplified().split(' ');
        if (f.size() < 9)
            continue;
        NetCounters n;
        n.name = QString::fromLatin1(line.left(colon).trimmed());
        n.rxBytes = f[0].toULongLong();
        n.txBytes = f[8].toULongLong();
        result.append(n);
    }
    return result;
}

bool parsePidStat(const QByteArray &text, PidStat &out) {
    const int open = text.indexOf('(');
    const int close = text.lastIndexOf(')');
    if (open <= 0 || close < open)
        return false;
    out.pid = text.left(open).trimmed().toInt();
    out.comm = QString::fromUtf8(text.mid(open + 1, close - open - 1));
    // Field 3 (state) is index 0 here; utime is field 14, stime 15,
    // num_threads 20, and rss 24.
    const QList<QByteArray> f = text.mid(close + 2).trimmed().split(' ');
    if (f.size() < 22)
        return false;
    out.state = f[0].isEmpty() ? '?' : f[0].at(0);
    out.ppid = f[1].toInt();
    out.cpuTicks = f[11].toULongLong() + f[12].toULongLong();
    out.threads = f[17].toInt();
    out.rssPages = f[21].toULongLong();
    return out.pid > 0;
}

bool parsePidIo(const QByteArray &text, PidIo &out) {
    bool read = false, write = false;
    for (const QByteArray &line : text.split('\n')) {
        if (line.startsWith("read_bytes:")) {
            out.readBytes = line.mid(11).trimmed().toULongLong();
            read = true;
        } else if (line.startsWith("write_bytes:")) {
            out.writeBytes = line.mid(12).trimmed().toULongLong();
            write = true;
        }
    }
    return read && write;
}

QString parseCmdline(const QByteArray &text) {
    QByteArray joined = text;
    while (joined.endsWith('\0'))
        joined.chop(1);
    // Arguments can hold newlines and tabs (think `python -c`), which would
    // break a one-line table, so every control character becomes a space.
    for (char &c : joined)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f)
            c = ' ';
    return QString::fromUtf8(joined).simplified();
}

// The kernel escapes spaces and a few other characters in mount paths as
// three-digit octal sequences.
static QString unescapeMount(const QByteArray &field) {
    QByteArray out;
    for (int i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size()) {
            bool ok = false;
            const int code = field.mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                out.append(char(code));
                i += 3;
                continue;
            }
        }
        out.append(field[i]);
    }
    return QString::fromUtf8(out);
}

QList<Mount> parseMounts(const QByteArray &text) {
    static const QSet<QByteArray> types{"ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "vfat", "exfat",
                                        "ntfs", "ntfs3", "fuseblk", "zfs", "bcachefs", "jfs", "reiserfs"};
    QList<Mount> result;
    QHash<QString, int> byDevice;
    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> f = line.split(' ');
        if (f.size() < 3 || !types.contains(f[2]))
            continue;
        const Mount mount{unescapeMount(f[0]), unescapeMount(f[1]), QString::fromLatin1(f[2])};
        const auto existing = byDevice.constFind(mount.device);
        if (existing == byDevice.constEnd()) {
            byDevice.insert(mount.device, result.size());
            result.append(mount);
        } else if (mount.path.size() < result[*existing].path.size()) {
            result[*existing] = mount;
        }
    }
    return result;
}

} // namespace procfs
