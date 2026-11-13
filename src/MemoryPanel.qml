import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vitals

Panel {
    id: root

    title: qsTr("Memory")
    key: "2"
    detail: monitor.bytes(monitor.memoryTotal)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Sparkline {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 24
            values: monitor.memoryHistory
            maximum: 1
            color: theme.colors.magenta
            gridColor: theme.colors.selection
        }

        Repeater {
            model: [
                { label: qsTr("Used"), value: monitor.memoryUsed, total: monitor.memoryTotal, color: "" },
                { label: qsTr("Cached"), value: monitor.memoryCached, total: monitor.memoryTotal, color: theme.colors.blue },
                { label: qsTr("Available"), value: monitor.memoryAvailable, total: monitor.memoryTotal, color: theme.colors.cyan },
                { label: qsTr("Swap"), value: monitor.swapUsed, total: monitor.swapTotal, color: "" },
            ]
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 10
                visible: modelData.total > 0

                Label {
                    Layout.preferredWidth: 100
                    text: modelData.label
                    color: theme.colors.dark_foreground
                    elide: Text.ElideRight
                }
                Meter {
                    Layout.fillWidth: true
                    value: modelData.total > 0 ? modelData.value / modelData.total : 0
                    color: modelData.color !== "" ? modelData.color
                         : value < 0.5 ? theme.colors.green : value < 0.8 ? theme.colors.yellow : theme.colors.red
                }
                Label {
                    Layout.preferredWidth: 80
                    horizontalAlignment: Text.AlignRight
                    text: monitor.bytes(modelData.value)
                    color: theme.colors.foreground
                }
            }
        }
    }
}
