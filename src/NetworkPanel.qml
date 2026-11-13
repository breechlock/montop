import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        // the rest are listed with their totals. The model is a count so
        // delegates live on between samples.
        Repeater {
            model: monitor.networks.length
            delegate: ColumnLayout {
                id: link
                required property int index
                readonly property var info: monitor.networks[index] || ({})
                readonly property bool up: !!info.up
                readonly property bool graphed: up && index < 2

                Layout.fillWidth: true
                Layout.fillHeight: graphed
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label {
                        Layout.fillWidth: true
                        text: link.info.name || ""
                        color: link.up ? theme.colors.bright_foreground : theme.colors.dark_foreground
                        elide: Text.ElideRight
                    }
                    Label {
                        text: "▼ " + (link.up ? monitor.rate(link.info.rxRate || 0) : monitor.bytes(link.info.rxTotal || 0))
                        color: link.up ? theme.colors.cyan : theme.colors.dark_foreground
                    }
                    Label {
                        text: "▲ " + (link.up ? monitor.rate(link.info.txRate || 0) : monitor.bytes(link.info.txTotal || 0))
                        color: link.up ? theme.colors.magenta : theme.colors.dark_foreground
                    }
                }
                Graph {
                    visible: link.graphed
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 28
                    series: "net/" + (link.info.name || "") + "/rx"
                    mirrorSeries: "net/" + (link.info.name || "") + "/tx"
                    color: theme.colors.cyan
                    mirrorColor: theme.colors.magenta
                }
                Label {
                    visible: link.graphed
                    text: qsTr("Total ▼ %1  ▲ %2").arg(monitor.bytes(link.info.rxTotal || 0)).arg(monitor.bytes(link.info.txTotal || 0))
                    color: theme.colors.dark_foreground
                }
            }
        }
    }
}
