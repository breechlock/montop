import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every GPU with what its driver reports: how busy it is over time, its
// memory, and its clock and sensors. A discrete GPU that's asleep is left
// asleep, and one whose driver reports nothing says so.
Panel {
    id: root

    readonly property var gpus: monitor.gpus

    function percent(fraction) {
        return Math.round(fraction * 100).toLocaleString(Qt.locale(), 'f', 0) + "%"
    }
    // Clock, temperature, power and fan, as far as they're known.
    function facts(gpu) {
        translator.language
        const parts = []
        if (gpu.mhz > 0)
            parts.push(qsTr("%1 GHz").arg((gpu.mhz / 1000).toLocaleString(Qt.locale(), 'f', 2)))
        if (gpu.temperature >= 0)
            parts.push(Math.round(gpu.temperature) + " °C")
        if (gpu.watts >= 0)
            parts.push(gpu.watts.toLocaleString(Qt.locale(), 'f', 1) + " W")
        if (gpu.fan >= 0)
            parts.push(qsTr("fan %1").arg(root.percent(gpu.fan / 100)))
        return parts.join("  ·  ")
    }

    title: qsTr("GPU")
    key: "6"
    detail: gpus.length === 1 ? gpus[0].name : qsTr("%n GPUs", "", gpus.length)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        // A count, not the list: a list model would rebuild every graph on
        // every sample.
        Repeater {
            model: root.gpus.length
            delegate: ColumnLayout {
                id: section
                required property int index
                readonly property var gpu: root.gpus[index] || ({})

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4

                // With more than one GPU each says which it is.
                Label {
                    visible: root.gpus.length > 1
                    Layout.fillWidth: true
                    text: section.gpu.name || ""
                    color: theme.colors.foreground
                    font.bold: true
                    elide: Text.ElideRight
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 36

                    Graph {
                        anchors.fill: parent
                        visible: section.gpu.busy >= 0
                        series: "gpu/" + section.index
                        blame: section.gpu.perApp ? "gpu" : ""
                        maximum: 1
                        color: theme.colors.cyan
                    }
                    Column {
                        x: 8
                        y: 4
                        width: parent.width - 16
                        Label {
                            text: section.gpu.asleep ? qsTr("Asleep")
                                : section.gpu.busy >= 0 ? root.percent(section.gpu.busy) : "–"
                            font.pixelSize: 28
                            font.bold: true
                            color: section.gpu.asleep ? theme.colors.dark_foreground : theme.colors.bright_foreground
                        }
                        Label {
                            width: parent.width
                            text: section.gpu.asleep ? qsTr("Powered down until an app needs it.") : section.gpu.note || ""
                            visible: text.length > 0
                            color: theme.colors.dark_foreground
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // Its own memory where it has some; otherwise what the apps'
                // GPU buffers hold of the memory it shares with the CPU.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !section.gpu.asleep && (section.gpu.memoryTotal > 0 || section.gpu.appMemory > 0)

                    Label {
                        text: section.gpu.memoryTotal > 0 ? qsTr("Memory") : qsTr("Apps hold")
                        color: theme.colors.dark_foreground
                        elide: Text.ElideRight
                    }
                    Meter {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 24
                        visible: section.gpu.memoryTotal > 0
                        value: section.gpu.memoryTotal > 0 ? section.gpu.memoryUsed / section.gpu.memoryTotal : 0
                    }
                    Item { Layout.fillWidth: true; visible: !(section.gpu.memoryTotal > 0) }
                    Label {
                        horizontalAlignment: Text.AlignRight
                        text: section.gpu.memoryTotal > 0
                              ? qsTr("%1 of %2").arg(monitor.bytes(section.gpu.memoryUsed)).arg(monitor.bytes(section.gpu.memoryTotal))
                              : monitor.bytes(section.gpu.appMemory)
                        color: theme.colors.foreground
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0 && !section.gpu.asleep
                    text: root.facts(section.gpu)
                    color: theme.colors.dark_foreground
                    elide: Text.ElideRight
                }
            }
        }
    }
}
