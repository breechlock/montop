#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Follows the current Omarchy theme live: the whole palette from
// colors.toml (not just the accent, since a monitor needs its reds, yellows
// and greens), light or dark mode, the monospace font, and Hyprland's window
// rounding.
class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    Q_PROPERTY(QString font READ font CONSTANT)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_colors.value("accent").toString(); }
    QString accentForeground() const;
    QVariantMap colors() const { return m_colors; }
    bool dark() const { return m_dark; }
    int radius() const { return m_radius; }
    QString font() const { return m_font; }

    // Every `key = "#rrggbb"` in colors.toml, plus `mode`.
    static QHash<QString, QString> readColors(const QString &path);
    // Fills in keys a theme leaves out from the ones it has, so older or
    // hand-made themes still give every role a sensible color.
    static QVariantMap complete(const QHash<QString, QString> &colors, bool *dark = nullptr);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QVariantMap m_colors;
    bool m_dark = true;
    QString m_font;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
