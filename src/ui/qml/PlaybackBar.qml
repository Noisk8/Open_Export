import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    required property var controller
    implicitHeight: 94
    color: "#191B20"
    border.color: "#2B2E35"

    function timeLabel(seconds) {
        const total = Math.max(0, Math.floor(seconds || 0))
        return Math.floor(total / 60) + ":" + (total % 60).toString().padStart(2, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: bar.controller.trackName || "REPRODUCTOR"
                font.bold: true
                color: "#FFFFFF"
                elide: Text.ElideRight
            }
            Label {
                Layout.maximumWidth: bar.width * 0.55
                text: bar.controller.status
                color: bar.controller.errorText.length ? "#EF4444" : "#9CA3AF"
                elide: Text.ElideRight
                ToolTip.visible: statusHover.hovered
                ToolTip.text: text
                HoverHandler { id: statusHover }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            OdeButton {
                objectName: "playPauseButton"
                text: bar.controller.playing ? "Ⅱ Pausar" : "▶ Reproducir"
                Layout.preferredWidth: 112
                enabled: bar.controller.hasTrack && !bar.controller.errorText.length
                Accessible.name: bar.controller.playing ? "Pausar canción" : "Reproducir canción"
                onClicked: bar.controller.togglePlayback()
            }
            OdeButton {
                text: "■"
                enabled: bar.controller.hasTrack
                Accessible.name: "Detener y volver al inicio"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: bar.controller.stop()
            }
            Label {
                text: bar.timeLabel(positionSlider.pressed ? positionSlider.value : bar.controller.position)
                color: "#FFFFFF"
                Layout.preferredWidth: 40
            }
            Slider {
                id: positionSlider
                objectName: "playbackPosition"
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                from: 0
                to: Math.max(1, bar.controller.duration)
                enabled: bar.controller.seekable
                Accessible.name: "Posición de reproducción"
                onMoved: bar.controller.seek(value)
                Binding {
                    target: positionSlider
                    property: "value"
                    value: bar.controller.position
                    when: !positionSlider.pressed
                }
            }
            Label { text: bar.timeLabel(bar.controller.duration); color: "#9CA3AF"; Layout.preferredWidth: 40 }
            OdeButton {
                text: bar.controller.muted ? "Activar audio" : "Silenciar"
                Layout.preferredWidth: 94
                Accessible.name: text
                onClicked: bar.controller.muted = !bar.controller.muted
            }
            Slider {
                objectName: "playbackVolume"
                Layout.preferredWidth: bar.width > 850 ? 125 : 80
                from: 0
                to: 1
                value: bar.controller.volume
                Accessible.name: "Volumen"
                onMoved: bar.controller.volume = value
            }
            Label {
                text: Math.round(bar.controller.volume * 100) + "%"
                color: bar.controller.muted ? "#6B7280" : "#FFFFFF"
                Layout.preferredWidth: 34
            }
        }
    }
}
