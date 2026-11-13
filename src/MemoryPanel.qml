import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Panel {
    id: root

    function amount(kind) {
        switch (kind) {
        case "used": return monitor.memoryUsed
        case "cached": return monitor.memoryCached
        case "available": return monitor.memoryAvailable
        case "swap": return monitor.swapUsed
        }
        return 0
    }

    title: qsTr("Memory")
    key: "2"
    detail: monitor.bytes(monitor.memoryTotal)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Graph {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 24
            series: "memory"
            maximum: 1
            color: theme.colors.magenta
        }

        // The rows are fixed and read their numbers live, so they aren't
        // rebuilt on every sample and their gauges can glide.
        Repeater {
            model: ["used", "cached", "available", "swap"]
            delegate: RowLayout {
                id: memoryRow
                required property string modelData
                readonly property real total: modelData === "swap" ? monitor.swapTotal : monitor.memoryTotal
                readonly property real amount: root.amount(modelData)

                Layout.fillWidth: true
                spacing: 10
                visible: total > 0

                Label {
                    Layout.preferredWidth: 100
                    text: {
                        switch (memoryRow.modelData) {
                        case "used": return qsTr("Used")
                        case "cached": return qsTr("Cached")
                        case "available": return qsTr("Available")
                        }
                        return qsTr("Swap")
                    }
                    color: theme.colors.dark_foreground
                    elide: Text.ElideRight
                }
                Meter {
                    Layout.fillWidth: true
                    value: memoryRow.total > 0 ? memoryRow.amount / memoryRow.total : 0
                    color: memoryRow.modelData === "cached" ? theme.colors.blue
                         : memoryRow.modelData === "available" ? theme.colors.cyan
                         : value < 0.5 ? theme.colors.green : value < 0.8 ? theme.colors.yellow : theme.colors.red
                }
                Label {
                    Layout.preferredWidth: 80
                    horizontalAlignment: Text.AlignRight
                    text: monitor.bytes(memoryRow.amount)
                    color: theme.colors.foreground
                }
            }
        }
    }
}
