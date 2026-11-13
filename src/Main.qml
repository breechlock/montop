import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1280
    height: 860
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: "Vitals"
    color: theme.colors.background

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: theme.colors.background
    Material.foreground: theme.colors.foreground
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool showCpu: true
    property bool showMemory: true
    property bool showDisks: true
    property bool showNetwork: true
    property bool showProcesses: true
    // Single-letter shortcuts stand down while the filter has the keyboard
    // or a question is open, so typing doesn't kill processes.
    readonly property bool typing: processes.filtering || confirm.visible
    readonly property bool leftColumn: showMemory || showDisks || showNetwork

    // The frame clock every graph shares: 0 when a sample lands, 1 when the
    // next is due. Advanced once per frame while there's motion to show and
    // the window is visible; idle otherwise, so a paused or hidden window
    // costs nothing.
    property real phase: 1
    property double sampledAt: 0
    Connections {
        target: monitor
        function onSampled() {
            win.sampledAt = Date.now()
            win.phase = 0
        }
        function onPausedChanged() {
            if (monitor.paused)
                win.phase = 1
        }
    }
    // Graphs move 2 pixels a sample, so the clock ticks about as often as
    // they cross a device pixel, and each tick that moves nothing draws
    // nothing. A timer rather than a FrameAnimation: a running animation
    // keeps Qt redrawing at the display's full rate even when nothing changed.
    Timer {
        interval: Math.max(16, Math.floor(monitor.interval / (2 * Screen.devicePixelRatio) / 2))
        repeat: true
        running: win.phase < 1 && !monitor.paused && win.visibility !== Window.Minimized
        onTriggered: win.phase = Math.min(1, (Date.now() - win.sampledAt) / monitor.interval)
    }

    property string message
    property bool messageIsError: false
    function say(text, error) {
        message = text
        messageIsError = !!error
        messageTimer.restart()
    }
    Timer { id: messageTimer; interval: 4000; onTriggered: win.message = "" }

    function askToEnd(force) {
        const p = processes.selected
        if (!p)
            return
        confirm.pid = p.pid
        confirm.name = p.name
        confirm.pids = p.isGroup ? p.pids : []
        confirm.kind = p.isGroup ? p.kind : ""
        confirm.force = force
        confirm.open()
    }

    function duration(seconds) {
        const d = Math.floor(seconds / 86400), h = Math.floor(seconds % 86400 / 3600), m = Math.floor(seconds % 3600 / 60)
        if (d > 0)
            return qsTr("%1d %2h").arg(d).arg(h)
        if (h > 0)
            return qsTr("%1h %2m").arg(h).arg(m)
        return qsTr("%1m").arg(m)
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequences: ["Q", "Ctrl+Q"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: monitor.paused = !monitor.paused }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false
            else if (monitor.processes.filter.length > 0)
                processes.clearFilter()
        }
    }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: !win.typing && win.showProcesses; onActivated: processes.startFilter() }
    Shortcut { sequences: ["Down", "J"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.move(1) }
    Shortcut { sequences: ["Up", "K"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.move(-1) }
    Shortcut { sequence: "PgDown"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.page(1) }
    Shortcut { sequence: "PgUp"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.page(-1) }
    Shortcut { sequences: ["Home", "G"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.select(0) }
    Shortcut { sequences: ["End", "Shift+G"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.select(monitor.processes.count - 1) }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: monitor.processes.cycleSort() }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: monitor.processes.descending = !monitor.processes.descending }
    Shortcut {
        sequence: "Shift+K"
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: {
            monitor.processes.showKernelThreads = !monitor.processes.showKernelThreads
            win.say(monitor.processes.showKernelThreads ? qsTr("Showing kernel threads") : qsTr("Hiding kernel threads"))
        }
    }
    Shortcut { sequence: "A"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.toggleGrouped() }
    Shortcut { sequences: ["Right", "L", "Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.expand() }
    Shortcut { sequences: ["Left", "H"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: processes.collapse() }
    Shortcut { sequence: "X"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.askToEnd(false) }
    Shortcut { sequence: "Shift+X"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.askToEnd(true) }
    Shortcut {
        sequences: ["+", "="]
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: { monitor.slower(); win.say(qsTr("Updating every %1 s").arg((monitor.interval / 1000).toLocaleString(Qt.locale(), 'f', 3).replace(/[.,]?0+$/, ""))) }
    }
    Shortcut {
        sequence: "-"
        context: Qt.ApplicationShortcut
        enabled: !win.typing
        onActivated: { monitor.faster(); win.say(qsTr("Updating every %1 s").arg((monitor.interval / 1000).toLocaleString(Qt.locale(), 'f', 3).replace(/[.,]?0+$/, ""))) }
    }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showCpu = !win.showCpu }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showMemory = !win.showMemory }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showDisks = !win.showDisks }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showNetwork = !win.showNetwork }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.showProcesses = !win.showProcesses }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            // Who and how long, in one quiet line.
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Label { text: monitor.hostname; color: theme.accent; font.bold: true }
                Label { text: monitor.kernel; color: theme.colors.dark_foreground }
                Label {
                    text: { translator.language; return qsTr("up %1").arg(win.duration(monitor.uptime)) }
                    color: theme.colors.dark_foreground
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: monitor.paused
                    text: qsTr("Paused")
                    color: theme.colors.yellow
                    font.bold: true
                }
                Label {
                    text: { translator.language; return qsTr("every %1 s").arg((monitor.interval / 1000).toLocaleString(Qt.locale(), 'f', 3).replace(/[.,]?0+$/, "")) }
                    color: theme.colors.dark_foreground
                }
            }

            Label {
                Layout.fillWidth: true
                visible: monitor.error.length > 0
                text: monitor.error
                color: theme.colors.red
                wrapMode: Text.Wrap
            }

            CpuPanel {
                visible: win.showCpu
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(150, win.height * 0.26)
                Layout.fillHeight: !win.leftColumn && !win.showProcesses
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 10
                visible: win.leftColumn || win.showProcesses

                ColumnLayout {
                    visible: win.leftColumn
                    Layout.fillHeight: true
                    Layout.fillWidth: !win.showProcesses
                    Layout.preferredWidth: Math.max(340, win.width * 0.34)
                    spacing: 8

                    MemoryPanel {
                        visible: win.showMemory
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 150
                    }
                    DiskPanel {
                        visible: win.showDisks
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 150
                    }
                    NetworkPanel {
                        visible: win.showNetwork
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 120
                    }
                }

                ProcessPanel {
                    id: processes
                    visible: win.showProcesses
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            // The selected process's full command line, or what just happened.
            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Label {
                    Layout.fillWidth: true
                    text: win.message.length > 0 ? win.message
                        : !processes.selected ? ""
                        : processes.selected.isGroup ? processes.selected.name + "  ·  " + processes.selected.command
                        : processes.selected.pid + "  " + processes.selected.command
                    color: win.message.length > 0 ? (win.messageIsError ? theme.colors.red : theme.accent) : theme.colors.dark_foreground
                    elide: Text.ElideRight
                }
                Label {
                    text: qsTr("? shortcuts")
                    color: theme.colors.dark_foreground
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }
    }

    Popup {
        id: confirm

        property int pid
        property string name
        property bool force
        // Every process of an app, when the selection is an app.
        property var pids: []
        property string kind
        readonly property bool group: pids.length > 0
        // Services and the session are not apps people opened; ending one can
        // take the desktop with it (the compositor is a service, too).
        readonly property bool critical: group && (kind === "service" || kind === "system" || kind === "session")

        anchors.centerIn: parent
        modal: true
        focus: true
        padding: 20
        closePolicy: Popup.CloseOnEscape
        // Material's own dimmer greys a dark window out; darken it instead.
        Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, theme.dark ? 0.55 : 0.3) }
        onOpened: confirmButton.forceActiveFocus()

        background: Rectangle {
            color: theme.colors.lighter_background
            border.width: 1
            border.color: confirm.force ? theme.colors.red : theme.accent
            radius: theme.radius
        }

        function accept() {
            if (group) {
                const result = monitor.signalProcesses(pids, force)
                close()
                if (result.failed === 0)
                    win.say(force ? qsTr("Killed %1").arg(name) : qsTr("Asked %1 to end").arg(name))
                else if (result.error === 1)
                    win.say(qsTr("Not allowed to signal %n of %1's processes: they belong to another user", "", result.failed).arg(name), true)
                else
                    win.say(qsTr("Couldn't signal %n of %1's processes, error %2", "", result.failed).arg(name).arg(result.error), true)
                return
            }
            const error = monitor.signalProcess(pid, force)
            close()
            if (error === 0)
                win.say(force ? qsTr("Killed %1 (%2)").arg(name).arg(pid) : qsTr("Asked %1 (%2) to end").arg(name).arg(pid))
            else if (error === 1)
                win.say(qsTr("Not allowed to signal %1 (%2): it belongs to another user").arg(name).arg(pid), true)
            else if (error === 3)
                win.say(qsTr("%1 (%2) has already exited").arg(name).arg(pid), true)
            else
                win.say(qsTr("Couldn't signal %1 (%2), error %3").arg(name).arg(pid).arg(error), true)
        }

        contentItem: ColumnLayout {
            spacing: 14
            Keys.onReturnPressed: confirm.accept()
            Keys.onEnterPressed: confirm.accept()

            Label {
                text: confirm.group
                    ? (confirm.force ? qsTr("Kill %1 and its %n processes?", "", confirm.pids.length).arg(confirm.name)
                                     : qsTr("End %1 and its %n processes?", "", confirm.pids.length).arg(confirm.name))
                    : (confirm.force ? qsTr("Kill %1 (%2)?").arg(confirm.name).arg(confirm.pid)
                                     : qsTr("End %1 (%2)?").arg(confirm.name).arg(confirm.pid))
                color: theme.colors.bright_foreground
                font.bold: true
                font.pixelSize: 15
            }
            Label {
                Layout.maximumWidth: 420
                wrapMode: Text.Wrap
                text: confirm.force
                    ? qsTr("SIGKILL stops it immediately. Unsaved work in it is lost.")
                    : qsTr("SIGTERM asks it to close, so it can save and clean up first.")
                color: theme.colors.foreground
            }
            Label {
                visible: confirm.critical
                Layout.maximumWidth: 420
                wrapMode: Text.Wrap
                text: qsTr("%1 is part of your system, not an app you opened. Ending it can close your session or break what depends on it.").arg(confirm.name)
                color: theme.colors.red
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: confirm.close()
                }
                Button {
                    id: confirmButton
                    text: confirm.force ? qsTr("Kill") : qsTr("End")
                    highlighted: true
                    Material.accent: confirm.force ? theme.colors.red : theme.accent
                    onClicked: confirm.accept()
                    Keys.onReturnPressed: confirm.accept()
                    Keys.onEnterPressed: confirm.accept()
                }
            }
        }
    }
}
