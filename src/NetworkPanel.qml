import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vitals

Panel {
    id: root

    title: qsTr("Network")
    key: "4"

    Label {
        anchors.centerIn: parent
        visible: monitor.networks.length === 0
        text: qsTr("No network interfaces")
        color: theme.colors.dark_foreground
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        // Connected interfaces get a graph, downloads above and uploads below;
        // the rest are listed with their totals.
        Repeater {
            model: monitor.networks
            delegate: ColumnLayout {
                required property var modelData
                required property int index
                readonly property bool graphed: modelData.up && index < 2

                Layout.fillWidth: true
                Layout.fillHeight: graphed
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: modelData.up ? theme.colors.bright_foreground : theme.colors.dark_foreground
                        elide: Text.ElideRight
                    }
                    Label {
                        text: "▼ " + (modelData.up ? monitor.rate(modelData.rxRate) : monitor.bytes(modelData.rxTotal))
                        color: modelData.up ? theme.colors.cyan : theme.colors.dark_foreground
                    }
                    Label {
                        text: "▲ " + (modelData.up ? monitor.rate(modelData.txRate) : monitor.bytes(modelData.txTotal))
                        color: modelData.up ? theme.colors.magenta : theme.colors.dark_foreground
                    }
                }
                Sparkline {
                    visible: graphed
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 28
                    values: modelData.rxHistory
                    mirror: modelData.txHistory
                    color: theme.colors.cyan
                    mirrorColor: theme.colors.magenta
                    gridColor: theme.colors.selection
                }
                Label {
                    visible: graphed
                    text: qsTr("Total ▼ %1  ▲ %2").arg(monitor.bytes(modelData.rxTotal)).arg(monitor.bytes(modelData.txTotal))
                    color: theme.colors.dark_foreground
                }
            }
        }
    }
}
