import QtQuick
import QtQuick.Controls

// Reusable dark Button. Every button in the app uses this component so all of
// them share the same look and the same height (implicitHeight: 34).
// The surrounding layout decides the final height (no Layout.preferredHeight).
Button {
    id: control

    property bool primary: false
    property color accentColor: "#EF4444"

    implicitHeight: 34
    padding: 8

    background: Rectangle {
        radius: 5
        color: control.down ? control.accentColor
             : control.hovered ? "#31353E"
             : (control.primary ? control.accentColor : "#24272E")
        border.color: control.primary ? control.accentColor : "#2B2E35"
        border.width: 1
        opacity: control.enabled ? 1 : 0.45
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? "#FFFFFF" : "#6B7280"
        font.pixelSize: 12
        font.bold: control.primary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
