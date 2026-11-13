#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

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
        model.update({process(10, "zsh", 1.0), process(11, "brave", 40.0), process(12, "kworker/0:1", 0, true),
                      process(13, "Hyprland", 5.0)});
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.pidAt(0), 11); // CPU, highest first

        model.setSortColumn(ProcessModel::Name);
        QCOMPARE(model.pidAt(0), 11); // brave, Hyprland, zsh: A to Z, ignoring case
        QCOMPARE(model.pidAt(1), 13);

        model.setFilter("hypr");
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.rowOf(13), 0);

        model.setFilter("");
        model.setShowKernelThreads(true);
        QCOMPARE(model.rowCount(), 4);
    }

    void modelKeepsRowsAcrossUpdates()
    {
        ProcessModel model;
        model.update({process(1, "a", 1), process(2, "b", 2)});
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        model.update({process(2, "b", 0), process(3, "c", 9)});
        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model.pidAt(0), 3);
        QCOMPARE(model.pidAt(1), 2);
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
        QCOMPARE(model->rowCount(), 1);
        const QVariantMap p = model->get(0);
        QCOMPARE(p.value("name").toString(), QString("worker"));
        QCOMPARE(p.value("command").toString(), QString("/usr/bin/worker --busy"));
        QCOMPARE(p.value("cpu").toDouble(), 100.0);
        QCOMPARE(p.value("readRate").toDouble(), -1.0); // No io file, so unknown.
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
