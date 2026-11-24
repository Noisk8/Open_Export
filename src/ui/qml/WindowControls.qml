import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    required property var targetWindow
    height: 40
    color: "#191B20"
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        spacing: 4
        Text {
            Layout.fillWidth: true
            text: targetWindow.title
            color: "white"
            elide: Text.ElideRight
            MouseArea {
                anchors.fill: parent
                onPressed: targetWindow.startSystemMove()
                onDoubleClicked: targetWindow.visibility === Window.Maximized
                    ? targetWindow.showNormal() : targetWindow.showMaximized()
            }
        }
        OdeButton {
            text: "−"
            Accessible.name: "Minimizar ventana"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: targetWindow.showMinimized()
        }
        OdeButton {
            text: targetWindow.visibility === Window.Maximized ? "❐" : "□"
            Accessible.name: "Maximizar o restaurar ventana"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: targetWindow.visibility === Window.Maximized
                ? targetWindow.showNormal() : targetWindow.showMaximized()
        }
        OdeButton {
            text: "×"
            Accessible.name: "Cerrar ventana"
            ToolTip.visible: hovered
            ToolTip.text: Accessible.name
            onClicked: targetWindow.close()
        }
    }
}
