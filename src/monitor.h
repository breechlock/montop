#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <optional>
#include <QTimer>
#include <QVariantList>

#include "processmodel.h"
#include "apps.h"
#include "procfs.h"

// Samples the kernel's counters on a timer and turns them into rates,
// percentages, and short histories for the window to draw. Everything comes
// from /proc and /sys; both roots are arguments so tests can hand it a
// directory of fixed files.
class Monitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hostname READ hostname CONSTANT)
    Q_PROPERTY(QString kernel READ kernel CONSTANT)
    Q_PROPERTY(QString cpuModel READ cpuModel CONSTANT)
    Q_PROPERTY(int coreCount READ coreCount NOTIFY sampled)
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY sampled)
    Q_PROPERTY(QVariantList cores READ cores NOTIFY sampled)
    Q_PROPERTY(double cpuTemperature READ cpuTemperature NOTIFY sampled)
    Q_PROPERTY(double cpuFrequency READ cpuFrequency NOTIFY sampled)
    Q_PROPERTY(QList<qreal> loadAverage READ loadAverage NOTIFY sampled)
    Q_PROPERTY(qint64 uptime READ uptime NOTIFY sampled)
    Q_PROPERTY(double memoryTotal READ memoryTotal NOTIFY sampled)
    Q_PROPERTY(double memoryUsed READ memoryUsed NOTIFY sampled)
    Q_PROPERTY(double memoryAvailable READ memoryAvailable NOTIFY sampled)
    Q_PROPERTY(double memoryCached READ memoryCached NOTIFY sampled)
    Q_PROPERTY(double memoryFree READ memoryFree NOTIFY sampled)
    Q_PROPERTY(double swapTotal READ swapTotal NOTIFY sampled)
    Q_PROPERTY(double swapUsed READ swapUsed NOTIFY sampled)
    Q_PROPERTY(QVariantList disks READ disks NOTIFY sampled)
    Q_PROPERTY(QVariantList filesystems READ filesystems NOTIFY sampled)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY sampled)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY sampled)
    Q_PROPERTY(QString error READ error NOTIFY sampled)
    Q_PROPERTY(ProcessModel *processes READ processes CONSTANT)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)

public:
    // How many samples each graph remembers.
    // Graphs scroll at a fixed speed and a wide one shows more of the past,
    // so keep enough for a full-screen graph: about four minutes at the
    // default four samples a second.
    static constexpr int HistoryLength = 1024;

    explicit Monitor(const QString &procRoot = "/proc", const QString &sysRoot = "/sys", QObject *parent = nullptr);

    QString hostname() const { return m_hostname; }
    QString kernel() const { return m_kernel; }
    QString cpuModel() const { return m_cpuModel; }
    int coreCount() const { return m_cores.size(); }
    double cpuUsage() const { return m_cpuUsage; }
    QVariantList cores() const { return m_cores; }
    double cpuTemperature() const { return m_cpuTemperature; }
    double cpuFrequency() const { return m_cpuFrequency; }
    QList<qreal> loadAverage() const { return m_loadAverage; }
    qint64 uptime() const { return m_uptime; }
    double memoryTotal() const { return m_memTotal; }
    double memoryUsed() const { return m_memUsed; }
    double memoryAvailable() const { return m_memAvailable; }
    double memoryCached() const { return m_memCached; }
    double memoryFree() const { return m_memFree; }
    double swapTotal() const { return m_swapTotal; }
    double swapUsed() const { return m_swapUsed; }
    QVariantList disks() const { return m_disks; }
    QVariantList filesystems() const { return m_filesystems; }
    QVariantList networks() const { return m_networks; }
    int taskCount() const { return m_taskCount; }
    QString error() const { return m_error; }
    ProcessModel *processes() { return &m_processes; }
    int interval() const { return m_timer.interval(); }
    bool paused() const { return m_paused; }

    // A series by name, for graphs to read straight from C++ rather than
    // through a copy into QML on every sample: "cpu", "memory",
    // "disk/<name>/read", "disk/<name>/write", "net/<name>/rx", "net/<name>/tx".
    QList<qreal> history(const QString &series) const { return m_history.value(series); }

    void setInterval(int ms);
    void setPaused(bool paused);

    Q_INVOKABLE void faster();
    Q_INVOKABLE void slower();

    // Sends SIGTERM, or SIGKILL when forced. Returns 0 or the errno, which
    // the window turns into a sentence.
    Q_INVOKABLE int signalProcess(int pid, bool force);
    // The same for every process of an app. Returns how many were sent and
    // how many failed, with the first failure's errno.
    Q_INVOKABLE QVariantMap signalProcesses(const QVariantList &pids, bool force);

    // Pausing (freezing) an app and capping its CPU, through systemd's user
    // manager so systemd stays in charge of its own units. Only the user's
    // apps and background services qualify: not the desktop's own services,
    // not the system's, and not the app Vitals itself runs in. Returns an
    // empty string, or why it didn't happen in a sentence for the window.
    Q_INVOKABLE QString setAppPaused(const QString &groupKey, bool paused);
    // `percent` is of one core, as in the CPU column; 0 lifts the limit.
    Q_INVOKABLE QString setAppCpuLimit(const QString &groupKey, double percent);
    // Thaws and unlimits what this monitor paused or limited, so nothing
    // stays frozen after the window closes. The command line doesn't call
    // it: there, a pause is meant to last.
    void undoControls();
    // The program run for the above; tests hand in a fake.
    void setSystemctl(const QString &program) { m_systemctl = program; }
    const GroupInfo *findGroup(const QString &key) const;
    // An app's name and state for the window: name, frozen, cpuLimit, control.
    Q_INVOKABLE QVariantMap app(const QString &groupKey) const;

    // The latest process scan, for the command line.
    const QList<ProcessInfo> &processList() const { return m_processList; }
    const QList<GroupInfo> &groupList() const { return m_groupList; }

    // Sizes in IEC units, with the decimal separator of the current language.
    Q_INVOKABLE static QString bytes(double value);
    Q_INVOKABLE static QString rate(double bytesPerSecond);

    // Starts sampling on the timer.
    void start();

    // Takes one full sample now, processes included. Tests call this.
    void sample();
    int processInterval() const;

signals:
    void sampled();
    void intervalChanged();
    void pausedChanged();

private:
    void sampleCpu();
    void sampleMemory();
    void sampleDisks(double seconds);
    void sampleFilesystems();
    void sampleNetworks(double seconds);
    void tick();
    void sampleSystem();
    void sampleProcessTable();
    void sampleProcesses(double seconds, quint64 cpuTotalDelta);
    QList<GroupInfo> groupProcesses(const QList<ProcessInfo> &processes, double seconds);
    QString groupName(const apps::Identity &identity, const QStringList &memberNames);
    QString findTemperatureInput() const;
    QString controlOf(const QString &cgroup) const;
    QString refusal(const GroupInfo &group) const;
    QString systemctl(const QStringList &arguments);
    QString userName(uint uid);
    void push(const QString &series, qreal value);

    QString m_proc, m_sys;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastSample = -1;
    qint64 m_lastProcessSample = -1;
    quint64 m_processCpuTotal = 0;
    bool m_paused = false;
    QString m_error;

    QString m_hostname, m_kernel, m_cpuModel, m_temperatureInput;
    QList<procfs::CpuTimes> m_prevCpu;
    double m_cpuUsage = 0, m_cpuTemperature = -1, m_cpuFrequency = 0;
    QHash<QString, QList<qreal>> m_history;
    QList<qreal> m_loadAverage;
    QVariantList m_cores;
    qint64 m_uptime = 0;

    double m_memTotal = 0, m_memUsed = 0, m_memAvailable = 0, m_memCached = 0, m_memFree = 0;
    double m_swapTotal = 0, m_swapUsed = 0;

    QHash<QString, procfs::DiskCounters> m_prevDisks;
    QVariantList m_disks, m_filesystems;

    QHash<QString, procfs::NetCounters> m_prevNet;
    QVariantList m_networks;

    struct PidHistory {
        quint64 ticks = 0;
        procfs::PidIo io;
        bool hasIo = false;
        QString comm, command, name, cgroup;
    };
    QHash<int, PidHistory> m_prevPids;
    QHash<uint, QString> m_users;
    ProcessModel m_processes;
    int m_taskCount = 0;
    QList<ProcessInfo> m_processList;
    QList<GroupInfo> m_groupList;
    QHash<int, QString> m_cgroupOf;
    // Cumulative counters per cgroup path, for rates between scans.
    struct CgroupCounters {
        quint64 cpuUsec = 0;
        quint64 readBytes = 0, writeBytes = 0;
        bool hasIo = false;
    };
    QHash<QString, CgroupCounters> m_prevCgroups;
    QSet<QString> m_physicalDevices; // "major:minor" of the disks listed.
    std::optional<apps::DesktopNames> m_desktopNames;
    QHash<QString, QString> m_groupNames;
    long m_pageSize = 4096;

    QString m_systemctl = "systemctl";
    QString m_selfCgroup;
    uint m_uid = 0;
    QSet<QString> m_pausedUnits, m_limitedUnits;
};
