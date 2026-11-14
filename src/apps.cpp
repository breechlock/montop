#include "apps.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>

namespace apps {

QString unescape(const QString &name) {
    QByteArray out;
    const QByteArray in = name.toUtf8();
    for (int i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 3 < in.size() && in[i + 1] == 'x') {
            bool ok = false;
            const int byte = in.mid(i + 2, 2).toInt(&ok, 16);
            if (ok) {
                out.append(char(byte));
                i += 3;
                continue;
            }
        }
        out.append(in[i]);
    }
    return QString::fromUtf8(out);
}

Identity classify(const QString &cgroupPath) {
    const QString path = cgroupPath.trimmed();
    if (path.isEmpty() || path == "/")
        return {"kernel", "kernel", "kernel", {}};

    const QString unit = path.section('/', -1);
    const bool user = path.contains("/user@");

    // Apps, by the XDG convention systemd and uwsm follow:
    //   app[-<launcher>]-<ApplicationID>-<RANDOM>.scope
    //   app[-<launcher>]-<ApplicationID>[@<RANDOM>].service
    // Dashes inside a name are escaped, so the literal dashes are the
    // separators, and the split must happen before unescaping.
    if (unit.startsWith("app-") && (unit.endsWith(".scope") || unit.endsWith(".service"))) {
        const bool scope = unit.endsWith(".scope");
        QString stem = unit.chopped(scope ? 6 : 8).mid(4);
        if (!scope)
            stem = stem.section('@', 0, 0);
        QStringList parts = stem.split('-');
        if (scope && parts.size() > 1)
            parts.removeLast(); // The random suffix.
        const QString id = unescape(parts.last());
        if (!id.isEmpty())
            return {"app:" + id, id, "app", unit};
    }

    // An app or service started over D-Bus by dbus-broker, as GNOME starts
    // Files: "dbus-:1.1-org.gnome.Nautilus@2.service". The bus address and
    // instance change every launch; the D-Bus name is what it is.
    static const QRegularExpression dbus("^dbus-:[0-9.]+-(.+)@[0-9]+\\.service$");
    const QRegularExpressionMatch activated = dbus.match(unit);
    if (user && activated.hasMatch()) {
        const QString id = unescape(activated.captured(1));
        return {"service:" + id, id, "service", unit};
    }

    if (unit.endsWith(".service")) {
        const QString id = unescape(unit.chopped(8));
        return user ? Identity{"service:" + id, id, "service", unit} : Identity{"system:" + id, id, "system", unit};
    }
    if (unit == "init.scope")
        return user ? Identity{"service:systemd", "systemd", "service", unit}
                    : Identity{"system:systemd", "systemd", "system", unit};
    static const QRegularExpression session("^session-\\w+\\.scope$");
    if (session.match(unit).hasMatch())
        return {"session", "session", "session", unit};
    return {"other:" + unescape(unit), unescape(unit), "other", unit};
}

QString componentName(const QString &id) {
    static const QHash<QString, QString> names{
        {"org.gnome.Shell", "GNOME Shell"},
        {"org.gnome.Shell.Notifications", "GNOME Shell notifications"},
        {"org.gnome.ScreenSaver", "GNOME screen saver"},
        {"gnome-session-manager", "GNOME Session"},
        {"plasma-plasmashell", "Plasma Shell"},
        {"plasma-kwin_wayland", "KWin"},
        {"plasma-kwin_x11", "KWin"},
    };
    const QString name = names.value(id);
    if (!name.isEmpty())
        return name;
    // GNOME's settings daemon runs as a service per plugin.
    if (id.startsWith("org.gnome.SettingsDaemon.") && id.size() > 25)
        return "GNOME Settings (" + id.mid(25) + ")";
    return {};
}

DesktopNames::DesktopNames(const QStringList &directories) {
    QStringList dirs = directories;
    if (dirs.isEmpty()) {
        dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        // The compositor runs as a session, whose entry lives apart from apps.
        for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            dirs.append(data + "/wayland-sessions");
    }
    // Earlier directories win, as the XDG spec has it: the user's own
    // entries override the system's.
    for (const QString &dir : dirs) {
        QDirIterator it(dir, {"*.desktop"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            const QString id = it.fileInfo().completeBaseName().toLower();
            if (m_names.contains(id))
                continue;
            QFile entry(file);
            if (!entry.open(QIODevice::ReadOnly))
                continue;
            bool inEntry = false;
            while (!entry.atEnd()) {
                const QByteArray line = entry.readLine().trimmed();
                if (line.startsWith('['))
                    inEntry = line == "[Desktop Entry]";
                else if (inEntry && line.startsWith("Name=")) {
                    m_names.insert(id, QString::fromUtf8(line.mid(5)).trimmed());
                    break;
                }
            }
        }
    }
}

QString DesktopNames::name(const QString &id) const {
    return m_names.value(id.toLower());
}

} // namespace apps
