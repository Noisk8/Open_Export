import QtQuick
import QtQuick.Controls

// Dark CheckBox matching the project palette.
CheckBox {
    id: control

    implicitHeight: 28
    spacing: 6

    contentItem: Text {
        text: control.text
        font.pixelSize: 11
        color: "#9CA3AF"
        leftPadding: control.indicator.width + 8
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 3
        color: "#101114"
        border.color: "#2B2E35"
        border.width: 1

        Rectangle {
            width: 10
            height: 10
            radius: 2
            anchors.centerIn: parent
            color: "#EF4444"
            visible: control.checked
        }
    }
}
