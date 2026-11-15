import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Montop

Panel {
    id: root

    function percent(fraction) {
        return Math.round(fraction * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
    }

    title: qsTr("CPU")
    key: "1"
    detail: {
        translator.language
        const parts = [monitor.cpuModel]
        if (monitor.cpuFrequency > 0)
            parts.push(qsTr("%1 GHz").arg((monitor.cpuFrequency / 1000).toLocaleString(Qt.locale(), 'f', 2)))
        if (monitor.cpuTemperature >= 0)
            parts.push(Math.round(monitor.cpuTemperature) + " °C")
        return parts.join("  ·  ")
    }

    RowLayout {
        anchors.fill: parent
        spacing: 16

        Item {
            id: total
            Layout.fillWidth: true
            Layout.fillHeight: true

            Graph {
                anchors.fill: parent
                series: "cpu"
                blame: "cpu"
                maximum: 1
                color: theme.accent
            }


            Column {
                x: 8
                y: 4
                Label {
                    text: root.percent(monitor.cpuUsage)
                    font.pixelSize: 28
                    font.bold: true
                    color: theme.colors.bright_foreground
                }
                Label {
                    text: {
                        translator.language
                        return qsTr("load %1").arg(monitor.loadAverage.map(v => v.toLocaleString(Qt.locale(), 'f', 2)).join("  "))
                    }
                    color: theme.colors.dark_foreground
                }
            }
        }

        // Per-core gauges in as many columns as it takes to fit the height.
        Grid {
            id: grid
            readonly property int rowHeight: 18
            readonly property int rowsFit: Math.max(1, Math.floor(root.height / rowHeight) - 2)

            Layout.alignment: Qt.AlignTop
            flow: Grid.TopToBottom
            rows: Math.min(monitor.coreCount, rowsFit)
            columnSpacing: 18
            rowSpacing: 0

            // A count, not the list itself: a list model would rebuild every
            // gauge on every sample, and their animations with them.
            Repeater {
                model: monitor.coreCount
                delegate: Row {
                    required property int index
                    readonly property real usage: monitor.cores[index] ? monitor.cores[index].usage : 0
                    height: grid.rowHeight
                    spacing: 8

                    Label {
                        width: 28
                        text: "C" + index
                        color: theme.colors.dark_foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Meter {
                        id: gauge
                        width: 70
                        value: usage
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: 40
                        horizontalAlignment: Text.AlignRight
                        text: root.percent(usage)
                        color: theme.colors.foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
