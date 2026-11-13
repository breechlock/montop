import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vitals

Panel {
    id: root

    title: qsTr("Disks")
    key: "3"

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Reads above the line, writes below, on one scale per device.
        Repeater {
            model: monitor.disks
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        spacing: 8
                        Label { text: modelData.name; color: theme.colors.bright_foreground }
                        Label {
                            // Gives way first when the row runs out of room.
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            text: modelData.model
                            color: theme.colors.dark_foreground
                            elide: Text.ElideRight
                        }
                    }
                    Label { text: "▲ " + monitor.rate(modelData.readRate); color: theme.colors.green }
                    Label { text: "▼ " + monitor.rate(modelData.writeRate); color: theme.colors.red }
                    Label {
                        text: qsTr("%1% busy").arg(Math.round(modelData.busy * 100))
                        color: theme.colors.dark_foreground
                    }
                }
                Sparkline {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 28
                    values: modelData.readHistory
                    mirror: modelData.writeHistory
                    color: theme.colors.green
                    mirrorColor: theme.colors.red
                    gridColor: theme.colors.selection
                }
            }
        }

        Repeater {
            model: monitor.filesystems
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10

                Label {
                    Layout.preferredWidth: 100
                    text: modelData.path
                    color: theme.colors.dark_foreground
                    elide: Text.ElideMiddle
                }
                Meter {
                    Layout.fillWidth: true
                    value: modelData.share
                }
                Label {
                    text: qsTr("%1 of %2").arg(monitor.bytes(modelData.used)).arg(monitor.bytes(modelData.total))
                    color: theme.colors.foreground
                }
            }
        }
    }
}
