import QtQuick

// A thin horizontal gauge. By default it colors itself by how full it is,
// green to yellow to red from the theme, like btop's meters.
Item {
    id: meter

    property real value: 0
    property color color: value < 0.5 ? theme.colors.green : value < 0.8 ? theme.colors.yellow : theme.colors.red

    implicitHeight: 6
    implicitWidth: 80

    Rectangle {
        anchors.fill: parent
        radius: Math.min(3, theme.radius)
        color: theme.colors.selection
    }

    Rectangle {
        height: parent.height
        width: Math.max(0, Math.min(1, meter.value)) * parent.width
        radius: Math.min(3, theme.radius)
        color: meter.color

        Behavior on width {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
    }
}
