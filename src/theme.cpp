// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    // Omarchy sets the desktop's monospace font through fontconfig, which is
    // where Qt's fixed font comes from too.
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();

    m_colors = complete({}, &m_dark);
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QHash<QString, QString> Theme::readColors(const QString &path) {
    QHash<QString, QString> colors;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return colors;
    static const QRegularExpression entry(R"re(^\s*([A-Za-z0-9_]+)\s*=\s*["']([^"']*)["'])re",
                                          QRegularExpression::MultilineOption);
    auto it = entry.globalMatch(QString::fromUtf8(file.readAll()));
    while (it.hasNext()) {
        const auto match = it.next();
        const QString key = match.captured(1), value = match.captured(2).trimmed();
        if (key == "mode" || QColor::isValidColorName(value))
            colors.insert(key, value);
    }
    return colors;
}

static QString mix(const QString &a, const QString &b, double t) {
    const QColor x(a), y(b);
    return QColor::fromRgbF(x.redF() + (y.redF() - x.redF()) * t, x.greenF() + (y.greenF() - x.greenF()) * t,
                            x.blueF() + (y.blueF() - x.blueF()) * t).name();
}

QVariantMap Theme::complete(const QHash<QString, QString> &given, bool *darkOut) {
    QHash<QString, QString> c = given;
    auto take = [&](const QString &key, const QString &fallback) {
        if (!QColor::isValidColorName(c.value(key)))
            c.insert(key, fallback);
    };

    // Omarchy's own default when no theme is there at all.
    take("background", "#121212");
    take("foreground", "#d4d4d8");
    take("accent", "#FFD60A");
    const bool dark = c.value("mode").isEmpty() ? QColor(c["background"]).lightnessF() < 0.5 : c.value("mode") != "light";

    // Older themes number their terminal colors instead of naming them.
    const QStringList named{"red", "green", "yellow", "blue", "magenta", "cyan"};
    for (int i = 0; i < named.size(); ++i) {
        take(named[i], c.value(QString("color%1").arg(i + 1), QStringList{"#f7768e", "#9ece6a", "#e0af68", "#7aa2f7", "#bb9af7", "#7dcfff"}[i]));
        take("bright_" + named[i], c.value(QString("color%1").arg(i + 9), c[named[i]]));
    }
    take("orange", c["bright_yellow"]);
    take("dark_background", mix(c["background"], dark ? "#000000" : "#ffffff", 0.25));
    take("darker_background", mix(c["background"], dark ? "#000000" : "#ffffff", 0.45));
    take("lighter_background", mix(c["background"], c["foreground"], 0.08));
    take("selection", mix(c["background"], c["accent"], 0.18));
    take("muted", mix(c["background"], c["foreground"], 0.3));
    take("dark_foreground", mix(c["background"], c["foreground"], 0.55));
    take("light_foreground", mix(c["background"], c["foreground"], 0.85));
    take("bright_foreground", c["foreground"]);

    QVariantMap map;
    for (auto it = c.begin(); it != c.end(); ++it)
        if (it.key() != "mode")
            map.insert(it.key(), it.value());
    if (darkOut)
        *darkOut = dark;
    return map;
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(accent());
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    bool dark = true;
    const QVariantMap colors = complete(readColors(m_directory + "/theme/colors.toml"), &dark);
    if (colors != m_colors || dark != m_dark) {
        m_colors = colors;
        m_dark = dark;
        emit changed();
    }
}
