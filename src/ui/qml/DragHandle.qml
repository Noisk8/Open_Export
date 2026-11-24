import QtQuick
import QtQuick.Controls

Item {
    id: handle
    property string kind
    property double itemId
    property bool allowed: true
    width: 24
    height: 28
    Rectangle {
        id: token
        width: 24
        height: 28
        color: mouse.drag.active ? "#EF4444" : "transparent"
        z: 100
        property double itemId: handle.itemId
        Drag.active: mouse.drag.active
        Drag.source: token
        Drag.keys: [handle.kind]
        Drag.hotSpot.x: 12
        Drag.hotSpot.y: 14
        Text { anchors.centerIn: parent; text: "⠿"; color: handle.allowed ? "#9CA3AF" : "#444444" }
        MouseArea {
            id: mouse
            anchors.fill: parent
            enabled: handle.allowed
            drag.target: token
            onReleased: {
                token.Drag.drop()
                token.x = 0
                token.y = 0
            }
            onCanceled: { token.x = 0; token.y = 0 }
            cursorShape: Qt.OpenHandCursor
        }
    }
}
