#pragma once

#include <QHash>
#include <QString>

// Which app a process belongs to, read from its cgroup. systemd puts every
// app it launches in a scope or service named after the app
// ("app-Hyprland-brave-1a2b.scope"), and Omarchy launches everything through
// it, so the cgroup says what a process is part of better than its parent
// chain does: a terminal's shells and builds, a browser's forty helpers.
namespace apps {

struct Identity {
    QString key;   // Stable across launches: "app:brave-browser", "system:NetworkManager".
    QString id;    // The app or unit name, unescaped: "brave-browser", "NetworkManager".
    QString kind;  // "app", "service", "system", "session", "kernel", or "other".
    QString unit;  // The cgroup's own name: "app-Hyprland-brave-1a2b.scope".
};

// Classifies a cgroup v2 path, as found after "0::" in /proc/<pid>/cgroup.
Identity classify(const QString &cgroupPath);

// systemd escapes unit names byte by byte as \xNN ("xdg\x2dterminal").
QString unescape(const QString &name);

// A name for a desktop's own component that has no desktop entry of its
// own: GNOME's and KDE's shells and helpers. Empty for anything else.
QString componentName(const QString &id);

// Display names from the desktop entries in the XDG data directories, keyed
// by lowercased file name without ".desktop".
class DesktopNames {
public:
    explicit DesktopNames(const QStringList &directories = {});
    // The entry's Name for an id, or an empty string when there is none.
    QString name(const QString &id) const;

private:
    QHash<QString, QString> m_names;
};

} // namespace apps
