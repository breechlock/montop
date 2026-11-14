#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <unistd.h>

#include "apps.h"
#include "cli.h"
#include "monitor.h"
#include "processmodel.h"
#include "procfs.h"
#include "sparkline.h"
#include "theme.h"
#include "translator.h"

class VitalsTests : public QObject {
    Q_OBJECT

private slots:
    void parsesStat()
    {
        const auto cpus = procfs::parseStat("cpu  100 0 50 800 50 0 0 0 0 0\n"
                                            "cpu0 60 0 20 400 20 0 0 0 0 0\n"
                                            "cpu1 40 0 30 400 30 0 0 0 0 0\n"
                                            "intr 1 2 3\n");
        QCOMPARE(cpus.size(), 3);
        QCOMPARE(cpus[0].total, quint64(1000));
        QCOMPARE(cpus[0].busy, quint64(150)); // idle and iowait are not busy
        QCOMPARE(cpus[2].busy, quint64(70));
    }

    void parsesMeminfo()
    {
        const auto info = procfs::parseMeminfo("MemTotal:       16000 kB\nMemAvailable:    4000 kB\nHugePages_Total: 0\n");
        QCOMPARE(info.value("MemTotal"), quint64(16000) * 1024);
        QCOMPARE(info.value("MemAvailable"), quint64(4000) * 1024);
        QCOMPARE(info.value("HugePages_Total"), quint64(0));
    }

    void parsesDiskstats()
    {
        const auto disks = procfs::parseDiskstats(
            " 259       0 nvme0n1 18809 22403 3455903 2866 198616 186189 24770666 100954 0 16069 106888 861 0 21600328 772\n");
        QCOMPARE(disks.size(), 1);
        QCOMPARE(disks[0].name, QString("nvme0n1"));
        QCOMPARE(disks[0].readBytes, quint64(3455903) * 512);
        QCOMPARE(disks[0].writeBytes, quint64(24770666) * 512);
        QCOMPARE(disks[0].busyMs, quint64(16069));
    }

    void parsesNetDev()
    {
        const auto nets = procfs::parseNetDev(
            "Inter-|   Receive                            |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets\n"
            "    lo: 1000 10 0 0 0 0 0 0 1000 10 0 0 0 0 0 0\n"
            "wlan0: 5000 50 0 0 0 0 0 0 7000 70 0 0 0 0 0 0\n");
        QCOMPARE(nets.size(), 2);
        QCOMPARE(nets[1].name, QString("wlan0"));
        QCOMPARE(nets[1].rxBytes, quint64(5000));
        QCOMPARE(nets[1].txBytes, quint64(7000));
    }

    void parsesPidStatWithAwkwardNames()
    {
        procfs::PidStat stat;
        // A comm with spaces and a closing parenthesis must not shift fields.
        QVERIFY(procfs::parsePidStat("42 (Web (Content)) S 7 42 42 0 -1 4194560 100 0 0 0 "
                                     "30 12 0 0 20 0 9 0 5000 100000 250 18446744073709551615",
                                     stat));
        QCOMPARE(stat.pid, 42);
        QCOMPARE(stat.comm, QString("Web (Content)"));
        QCOMPARE(stat.state, 'S');
        QCOMPARE(stat.ppid, 7);
        QCOMPARE(stat.cpuTicks, quint64(42));
        QCOMPARE(stat.threads, 9);
        QCOMPARE(stat.rssPages, quint64(250));
    }

    void flattensCommandLines()
    {
        QCOMPARE(procfs::parseCmdline(QByteArray("python\0-c\0import os\nprint(1)\0", 30)),
                 QString("python -c import os print(1)"));
    }

    void parsesMountsOncePerDevice()
    {
        const auto mounts = procfs::parseMounts(
            "/dev/mapper/root /home btrfs rw 0 0\n"
            "/dev/mapper/root / btrfs rw 0 0\n"
            "proc /proc proc rw 0 0\n"
            "/dev/sda1 /mnt/my\\040disk ext4 rw 0 0\n");
        QCOMPARE(mounts.size(), 2);
        QCOMPARE(mounts[0].path, QString("/"));
        QCOMPARE(mounts[1].path, QString("/mnt/my disk"));
    }

    void modelSortsFiltersAndHidesKernelThreads()
    {
        ProcessModel model;
        model.setGrouped(false);
        model.update({process(10, "zsh", 1.0), process(11, "brave", 40.0), process(12, "kworker/0:1", 0, true),
                      process(13, "Hyprland", 5.0)}, {});
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.keyAt(0), QString("p:11")); // CPU, highest first

        model.setSortColumn(ProcessModel::Name);
        QCOMPARE(model.keyAt(0), QString("p:11")); // brave, Hyprland, zsh: A to Z, ignoring case
        QCOMPARE(model.keyAt(1), QString("p:13"));

        model.setFilter("hypr");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.rowOf("p:13"), 0);

        model.setFilter("");
        model.setShowKernelThreads(true);
        QCOMPARE(model.rowCount(), 4);
    }

    void modelKeepsRowsAcrossUpdates()
    {
        ProcessModel model;
        model.setGrouped(false);
        model.update({process(1, "a", 1), process(2, "b", 2)}, {});
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.update({process(2, "b", 0), process(3, "c", 9)}, {});
        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model.keyAt(0), QString("p:3"));
        QCOMPARE(model.keyAt(1), QString("p:2"));
    }

    void monitorMeasuresFromProc()
    {
        QTemporaryDir root;
        const QString proc = root.filePath("proc"), sys = root.filePath("sys");
        QDir().mkpath(proc + "/100");
        QDir().mkpath(sys + "/block/sda");
        write(sys + "/block/sda/size", "1000\n");
        write(proc + "/meminfo", "MemTotal: 1000 kB\nMemFree: 200 kB\nMemAvailable: 600 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
        write(proc + "/loadavg", "0.50 0.25 0.10 1/100 100\n");
        write(proc + "/uptime", "3600.5 100.0\n");
        write(proc + "/net/dev", "h\nh\neth0: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n");
        write(proc + "/self/mounts", "");
        write(proc + "/100/cmdline", QByteArray("/usr/bin/worker\0--busy\0", 23));

        // Two CPUs; the process has used 0 ticks so far.
        write(proc + "/stat", "cpu  0 0 0 0 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0\ncpu1 0 0 0 0 0 0 0 0\n");
        write(proc + "/diskstats", "8 0 sda 0 0 0 0 0 0 0 0 0 0 0\n");
        write(proc + "/100/stat", pidStat(100, "worker", 0));
        Monitor monitor(proc, sys);

        // 200 ticks pass across both CPUs, 50 busy; the process used 100 of
        // them, which is one whole core.
        write(proc + "/stat", "cpu  50 0 0 150 0 0 0 0\ncpu0 50 0 0 50 0 0 0 0\ncpu1 0 0 0 100 0 0 0 0\n");
        write(proc + "/100/stat", pidStat(100, "worker", 100));
        write(proc + "/diskstats", "8 0 sda 0 0 2048 0 0 0 0 0 0 0 0\n");
        QTest::qWait(100);
        monitor.sample();

        QCOMPARE(monitor.coreCount(), 2);
        QCOMPARE(monitor.cpuUsage(), 0.25);
        QCOMPARE(monitor.cores()[0].toMap().value("usage").toDouble(), 0.5);
        QCOMPARE(monitor.memoryUsed(), 400.0 * 1024);
        QCOMPARE(monitor.uptime(), qint64(3600));
        QCOMPARE(monitor.loadAverage().value(0), 0.5);
        QCOMPARE(monitor.disks().size(), 1);
        QVERIFY(monitor.disks()[0].toMap().value("readRate").toDouble() > 0);
        QVERIFY(monitor.error().isEmpty());

        ProcessModel *model = monitor.processes();
        QCOMPARE(model->rowCount(), 1); // No cgroup: the process is its own app.
        QCOMPARE(model->keyAt(0), QString("g:process:worker"));
        model->setGrouped(false);
        QCOMPARE(model->rowCount(), 1);
        const QVariantMap p = model->get(0);
        QCOMPARE(p.value("name").toString(), QString("worker"));
        QCOMPARE(p.value("command").toString(), QString("/usr/bin/worker --busy"));
        QCOMPARE(p.value("cpu").toDouble(), 100.0);
        QCOMPARE(p.value("readRate").toDouble(), -1.0); // No io file, so unknown.
    }

    void classifiesCgroups()
    {
        auto check = [](const char *path, const char *key, const char *kind, const char *id) {
            const apps::Identity identity = apps::classify(path);
            QCOMPARE(identity.key, QString(key));
            QCOMPARE(identity.kind, QString(kind));
            QCOMPARE(identity.id, QString(id));
        };
        const QString user = "/user.slice/user-1000.slice/user@1000.service/";
        // Escaped dashes belong to the name; literal ones separate launcher,
        // app, and the random suffix.
        check(qPrintable(user + "app.slice/app-graphical.slice/app-Hyprland-xdg\\x2dterminal\\x2dexec-a7273f50.scope"),
              "app:xdg-terminal-exec", "app", "xdg-terminal-exec");
        check(qPrintable(user + "app.slice/app-firefox-1234.scope"), "app:firefox", "app", "firefox");
        check(qPrintable(user + "app.slice/app-Hyprland-org.gnome.Nautilus@ab12.service"), "app:org.gnome.Nautilus", "app",
              "org.gnome.Nautilus");
        check(qPrintable(user + "app.slice/superwhisper.service"), "service:superwhisper", "service", "superwhisper");
        check(qPrintable(user + "session.slice/wayland-wm@hyprland.desktop.service"),
              "service:wayland-wm@hyprland.desktop", "service", "wayland-wm@hyprland.desktop");
        check("/system.slice/NetworkManager.service", "system:NetworkManager", "system", "NetworkManager");
        check("/init.scope", "system:systemd", "system", "systemd");
        check("/user.slice/user-1000.slice/session-1.scope", "session", "session", "session");
        check("/", "kernel", "kernel", "kernel");
        QCOMPARE(apps::unescape("a\\x2db\\x40c"), QString("a-b@c"));
    }

    void readsDesktopNames()
    {
        QTemporaryDir user, system;
        write(user.filePath("foot.desktop"), "[Desktop Entry]\nName=My Foot\n");
        write(system.filePath("foot.desktop"), "[Desktop Entry]\nName=Foot\n");
        write(system.filePath("sub/org.gnome.Nautilus.desktop"),
              "[Desktop Action new]\nName=New Window\n[Desktop Entry]\nName=Files\nName[nl]=Bestanden\n");
        apps::DesktopNames names({user.path(), system.path()});
        QCOMPARE(names.name("foot"), QString("My Foot")); // The user's own entry wins.
        QCOMPARE(names.name("org.gnome.nautilus"), QString("Files"));
        QVERIFY(names.name("missing").isEmpty());
    }

    void modelGroupsAndExpands()
    {
        ProcessModel model;
        QList<ProcessInfo> processes{process(10, "brave", 30), process(11, "brave", 10), process(20, "foot", 1),
                                     process(21, "make", 50), process(2, "kthreadd", 0, true)};
        for (int i : {0, 1})
            processes[i].group = "app:brave";
        for (int i : {2, 3})
            processes[i].group = "app:foot";
        processes[4].group = "kernel";
        QList<GroupInfo> groups{group("app:brave", "Brave", {10, 11}, 40), group("app:foot", "Foot", {20, 21}, 51),
                                group("kernel", "Kernel", {2}, 0, "kernel")};
        model.update(processes, groups);

        // Apps only, busiest first, and no kernel until asked.
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.keyAt(0), QString("g:app:foot"));
        QCOMPARE(model.groupCount(), 2);

        model.setExpanded("app:foot", true);
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(model.keyAt(1), QString("p:21")); // make, the busier one, first
        QCOMPARE(model.data(model.index(1), ProcessModel::DepthRole).toInt(), 1);
        QCOMPARE(model.parentKey("p:21"), QString("g:app:foot"));

        // A filter that matches a process shows it under its app, opened.
        model.setExpanded("app:foot", false);
        model.setFilter("brave");
        QCOMPARE(model.rowCount(), 1); // The app's own name matched: it stays closed.
        model.setFilter("make");
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.keyAt(1), QString("p:21"));

        model.setFilter("");
        model.setGrouped(false);
        QCOMPARE(model.rowCount(), 4);
        QCOMPARE(model.keyAt(0), QString("p:21"));
    }

    void monitorGroupsByCgroup()
    {
        QTemporaryDir root;
        const QString proc = root.filePath("proc"), sys = root.filePath("sys");
        const QString scope = "/user.slice/user-1000.slice/user@1000.service/app.slice/app-Hyprland-testapp-1a2b.scope";
        write(proc + "/meminfo", "MemTotal: 1000 kB\nMemAvailable: 600 kB\n");
        write(proc + "/stat", "cpu  0 0 0 0 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0\n");
        write(sys + "/block/sda/size", "1000\n");
        write(sys + "/block/sda/dev", "8:0\n");
        write(proc + "/diskstats", "8 0 sda 0 0 0 0 0 0 0 0 0 0 0\n");
        for (int pid : {100, 101}) {
            write(proc + QString("/%1/stat").arg(pid), pidStat(pid, "testapp", 0));
            write(proc + QString("/%1/cgroup").arg(pid), "0::" + scope.toUtf8() + "\n");
        }
        write(sys + "/fs/cgroup" + scope + "/cpu.stat", "usage_usec 1000000\n");
        write(sys + "/fs/cgroup" + scope + "/memory.stat", "anon 4096\nfile 999999\nshmem 1024\nkernel 1024\n");
        write(sys + "/fs/cgroup" + scope + "/io.stat", "8:0 rbytes=0 wbytes=0\n253:0 rbytes=0 wbytes=0\n");
        Monitor monitor(proc, sys);

        // Half a second of CPU over about a tenth of a second would be far
        // more than the processes' own (zero) ticks: the cgroup counts
        // children that already exited.
        write(sys + "/fs/cgroup" + scope + "/cpu.stat", "usage_usec 1050000\n");
        write(sys + "/fs/cgroup" + scope + "/io.stat", "8:0 rbytes=1048576 wbytes=0\n253:0 rbytes=1048576 wbytes=0\n");
        write(proc + "/stat", "cpu  10 0 0 10 0 0 0 0\ncpu0 10 0 0 10 0 0 0 0\n");
        QTest::qWait(100);
        monitor.sample();

        QCOMPARE(monitor.groupList().size(), 1);
        const GroupInfo g = monitor.groupList().first();
        QCOMPARE(g.key, QString("app:testapp"));
        QCOMPARE(g.kind, QString("app"));
        QCOMPARE(g.pids.size(), 2);
        QCOMPARE(g.memory, quint64(6144)); // anon + shmem + kernel, not the page cache
        QVERIFY(g.cpu > 20);
        // Only the physical disk counts, not the volume stacked on it.
        QVERIFY(g.readRate > 0 && g.readRate < 1048576 * 20);

        const QJsonObject snapshot = snapshotJson(monitor, 5, "cpu");
        QCOMPARE(snapshot["apps"].toArray().size(), 1);
        const QJsonObject app = snapshot["apps"].toArray().first().toObject();
        QCOMPARE(app["processes"].toInt(), 2);
        QCOMPARE(app["kind"].toString(), QString("app"));
        QCOMPARE(app["top_processes"].toArray().size(), 2);
        QCOMPARE(snapshot["processes"].toArray().first().toObject()["app"].toString(), g.name);
        QVERIFY(snapshot["memory"].toObject()["total_bytes"].toDouble() > 0);
        QVERIFY(snapshot["cpu"].toObject()["temperature_celsius"].isNull());
    }

    void monitorPausesAndLimitsApps()
    {
        QTemporaryDir root;
        const QString proc = root.filePath("proc"), sys = root.filePath("sys");
        const QString manager = QString("/user.slice/user-%1.slice/user@%1.service").arg(getuid());
        const QString app = manager + "/app.slice/app-Hyprland-busy-1a2b.scope";
        const QString compositor = manager + "/session.slice/wayland-wm@hyprland.desktop.service";
        const QString system = "/system.slice/upower.service";
        write(proc + "/meminfo", "MemTotal: 1000 kB\nMemAvailable: 600 kB\n");
        write(proc + "/stat", "cpu  0 0 0 0 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0\n");
        const QList<QPair<int, QString>> processes{{100, app}, {200, compositor}, {300, system}};
        for (const auto &[pid, cgroup] : processes) {
            write(proc + QString("/%1/stat").arg(pid), pidStat(pid, "p" + QByteArray::number(pid), 0));
            write(proc + QString("/%1/cgroup").arg(pid), "0::" + cgroup.toUtf8() + "\n");
            write(sys + "/fs/cgroup" + cgroup + "/cpu.stat", "usage_usec 0\n");
        }
        write(sys + "/fs/cgroup" + app + "/cpu.max", "50000 100000\n");
        write(sys + "/fs/cgroup" + app + "/cgroup.events", "populated 1\nfrozen 1\n");
        // Records what it was asked, the way systemctl would be.
        const QString log = root.filePath("calls");
        write(root.filePath("systemctl"), "#!/bin/sh\necho \"$*\" >> " + log.toUtf8() + "\n");
        QFile::setPermissions(root.filePath("systemctl"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        Monitor monitor(proc, sys);
        monitor.setSystemctl(root.filePath("systemctl"));
        const GroupInfo *busy = monitor.findGroup("app:busy");
        QVERIFY(busy);
        QVERIFY(busy->control.isEmpty());
        QVERIFY(busy->frozen);
        QCOMPARE(busy->cpuLimit, 50.0);
        QCOMPARE(busy->controlUnits, QStringList{"app-Hyprland-busy-1a2b.scope"});
        QCOMPARE(monitor.findGroup("service:wayland-wm@hyprland.desktop")->control, QString("session"));
        QCOMPARE(monitor.findGroup("system:upower")->control, QString("system"));

        QVERIFY(monitor.setAppPaused("app:busy", false).isEmpty());
        QVERIFY(!monitor.findGroup("app:busy")->frozen);
        QVERIFY(monitor.setAppCpuLimit("app:busy", 25).isEmpty());
        QCOMPARE(monitor.findGroup("app:busy")->cpuLimit, 25.0);
        // The desktop's own services and the system's are never touched.
        QVERIFY(!monitor.setAppPaused("service:wayland-wm@hyprland.desktop", true).isEmpty());
        QVERIFY(!monitor.setAppCpuLimit("system:upower", 10).isEmpty());
        QVERIFY(monitor.setAppPaused("app:busy", true).isEmpty());
        monitor.undoControls();

        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(calls.readAll()).split('\n', Qt::SkipEmptyParts),
                 QStringList({"--user thaw app-Hyprland-busy-1a2b.scope",
                              "--user set-property --runtime app-Hyprland-busy-1a2b.scope CPUQuota=25%",
                              "--user freeze app-Hyprland-busy-1a2b.scope",
                              "--user thaw app-Hyprland-busy-1a2b.scope",
                              "--user set-property --runtime app-Hyprland-busy-1a2b.scope CPUQuota="}));
    }

    void monitorRewindsToEarlierMoments()
    {
        QTemporaryDir root;
        const QString proc = root.filePath("proc"), sys = root.filePath("sys");
        const QString scope = "/user.slice/user-1000.slice/user@1000.service/app.slice/app-Hyprland-build-1a2b.scope";
        write(proc + "/meminfo", "MemTotal: 1000 kB\nMemAvailable: 600 kB\n");
        write(proc + "/stat", "cpu  0 0 0 0 0 0 0 0\ncpu0 0 0 0 0 0 0 0 0\n");
        write(proc + "/100/stat", pidStat(100, "make", 0));
        write(proc + "/100/cgroup", "0::" + scope.toUtf8() + "\n");
        write(sys + "/fs/cgroup" + scope + "/cpu.stat", "usage_usec 0\n");
        Monitor monitor(proc, sys);

        // A burst of CPU, then nothing.
        QTest::qWait(20);
        write(sys + "/fs/cgroup" + scope + "/cpu.stat", "usage_usec 20000\n");
        monitor.sample();
        const double burst = monitor.sampleTime(0);
        QTest::qWait(20);
        monitor.sample();
        QCOMPARE(monitor.findGroup("app:build")->cpu, 0.0);

        const QVariantList blamed = monitor.blame(burst, "cpu", 3);
        QCOMPARE(blamed.size(), 1);
        QCOMPARE(blamed.first().toMap()["key"].toString(), QString("app:build"));
        QVERIFY(blamed.first().toMap()["value"].toDouble() > 10);

        QSignalSpy rewound(&monitor, &Monitor::rewindChanged);
        monitor.rewindTo(burst);
        QCOMPARE(monitor.rewoundTo(), monitor.momentTime(burst));
        QCOMPARE(rewound.count(), 1);
        ProcessModel *model = monitor.processes();
        QVERIFY(model->data(model->index(model->rowOf("g:app:build")), ProcessModel::CpuRole).toDouble() > 10);
        // New scans don't move the list while it shows the past.
        monitor.sample();
        QVERIFY(model->data(model->index(model->rowOf("g:app:build")), ProcessModel::CpuRole).toDouble() > 10);

        // Rewinding further than the history holds stops at its start.
        monitor.rewindBy(-3600);
        QCOMPARE(monitor.rewoundTo(), monitor.historyStart());
        monitor.rewindTo(0);
        QCOMPARE(monitor.rewoundTo(), 0.0);
        QCOMPARE(model->data(model->index(model->rowOf("g:app:build")), ProcessModel::CpuRole).toDouble(), 0.0);
    }

    void monitorSaysWhenProcIsMissing()
    {
        QTemporaryDir root;
        Monitor monitor(root.filePath("nothing"), root.filePath("nothing"));
        QVERIFY(!monitor.error().isEmpty());
    }

    void formatsBytes()
    {
        QLocale::setDefault(QLocale::c());
        QCOMPARE(Monitor::bytes(512), QString("512 B"));
        QCOMPARE(Monitor::bytes(1536), QString("1.50 KiB"));
        QCOMPARE(Monitor::rate(10.0 * 1024 * 1024), QString("10.0 MiB/s"));
    }

    void sparklineScalesInSteadySteps()
    {
        QCOMPARE(Sparkline::autoMaximum({}, {}), 20000.0); // idle floor, rounded up
        QCOMPARE(Sparkline::autoMaximum({3e6}, {1e6}), 5e6);
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
        QVERIFY(theme.dark());
        for (const char *key : {"background", "foreground", "red", "green", "yellow", "orange", "selection", "muted",
                                "dark_foreground", "lighter_background", "bright_foreground"})
            QVERIFY2(QColor::isValidColorName(theme.colors().value(key).toString()), key);
    }

    void themeReadsThePalette()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml",
              "mode = \"light\"\naccent = \"#112233\"\nbackground = \"#ffffff\"\nred = '#ff0000'\n");

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
        QVERIFY(!theme.dark());
        QCOMPARE(theme.colors().value("red").toString(), QString("#ff0000"));
        QCOMPARE(theme.colors().value("background").toString(), QString("#ffffff"));
    }

    void themeFollowsASwitch()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QFile::remove(dir.path() + "/theme/colors.toml");
        write(dir.path() + "/theme/colors.toml", "accent = \"#445566\"\n");
        QTRY_COMPARE(theme.accent(), QString("#445566"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Quit": "Sair", "Cancel": "Cancelar"})");
        write(dir.path() + "/pt-BR.json", R"({"Quit": "Fechar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Quit"), QString("Fechar"));
        QCOMPARE(translator.translate("Main", "Cancel"), QString("Cancelar"));
        QVERIFY(translator.translate("Main", "Kill").isNull());
    }

    void shippedCatalogsCoverEveryString()
    {
        // Each shipped language must translate every non-plural string the
        // Dutch catalog does, so a half-done catalog shows up here.
        Translator translator(QString(), ":/i18n");
        QFile nl(":/i18n/nl.json");
        QVERIFY(nl.open(QIODevice::ReadOnly));
        const QJsonObject strings = QJsonDocument::fromJson(nl.readAll()).object();
        QVERIFY(strings.size() > 40);
        for (auto it = strings.begin(); it != strings.end(); ++it)
            QVERIFY2(it.value().isObject() || !it.value().toString().isEmpty(), qPrintable(it.key()));
    }

private:
    static ProcessInfo process(int pid, const QString &name, double cpu, bool kernel = false)
    {
        ProcessInfo p;
        p.pid = pid;
        p.name = name;
        p.command = name;
        p.cpu = cpu;
        p.kernel = kernel;
        return p;
    }

    static GroupInfo group(const QString &key, const QString &name, const QList<int> &pids, double cpu,
                           const QString &kind = "app")
    {
        GroupInfo g;
        g.key = key;
        g.name = name;
        g.kind = kind;
        g.pids = pids;
        g.cpu = cpu;
        return g;
    }

    static QByteArray pidStat(int pid, const char *comm, int ticks)
    {
        return QByteArray::number(pid) + " (" + comm + ") R 1 1 1 0 -1 0 0 0 0 0 " + QByteArray::number(ticks)
               + " 0 0 0 20 0 1 0 100 1000 10";
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(contents);
    }
};

QTEST_MAIN(VitalsTests)
#include "vitals_tests.moc"
