#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

// What a cgroup has used so far, from its own counters. Every figure is
// cumulative except `memory` and `ownMemory`, which are now.
struct CgroupUsage {
    bool ok = false;
    quint64 cpuUsec = 0, userUsec = 0, systemUsec = 0;
    quint64 memory = 0;     // memory.current: the app's own plus file cache.
    quint64 ownMemory = 0;  // anon + shmem + kernel: what it holds itself.
    quint64 memoryPeak = 0; // memory.peak, with file cache; 0 if unknown.
    quint64 readBytes = 0, writeBytes = 0;
    bool hasIo = false; // Whether io.stat was there to read.
    // Time some of its tasks stalled waiting, from the pressure files.
    quint64 cpuWaitUsec = 0, memoryWaitUsec = 0, ioWaitUsec = 0;
    int processesPeak = 0;
};

// Reads the counters of the cgroup directory `path`, counting IO only on the
// physical disks listed ("major:minor"), so a disk and the encrypted volume
// on it aren't counted twice.
CgroupUsage readCgroupUsage(const QString &path, const QSet<QString> &physicalDevices);

// What `vitals run` reports, from the last reading and what was tracked
// while the command ran.
struct RunReport {
    QStringList command;
    int exitCode = 0;
    double seconds = 0;
    CgroupUsage usage;
    quint64 ownMemoryPeak = 0; // Sampled, so a spike shorter than a sample can slip by.
    double cpuPeak = 0;        // Percent of one core, over one sample.
};
QJsonObject runJson(const RunReport &report);
QString runText(const RunReport &report);

// vitals run [--json] [--interval ms] -- <command> [args...]
int runCommand(const QStringList &arguments);
