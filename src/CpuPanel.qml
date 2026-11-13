import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Vitals

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
            Layout.fillWidth: true
            Layout.fillHeight: true

            Sparkline {
                anchors.fill: parent
                values: monitor.cpuHistory
                maximum: 1
                color: theme.accent
                gridColor: theme.colors.selection
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

            Repeater {
                model: monitor.cores
                delegate: Row {
                    required property var modelData
                    required property int index
                    height: grid.rowHeight
                    spacing: 8

                    Label {
                        width: 28
                        text: "C" + index
                        color: theme.colors.dark_foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Meter {
                        width: 70
                        value: modelData.usage
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: 40
                        horizontalAlignment: Text.AlignRight
                        text: root.percent(modelData.usage)
                        color: theme.colors.foreground
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }
}
