import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Separate ApplicationWindow (NOT a Popup) for lossless FLAC -> WAV
// conversion. It is instantiated from Main.qml as:
//     ConverterWindow { id: converterWindow }
// and opened explicitly with .show(), so it stays hidden (visible: false) and
// has no Component.onCompleted that would display it.
ApplicationWindow {
    id: converterWindow

    width: 760
    height: 540
    minimumWidth: 620
    minimumHeight: 420
    title: "Convertir FLAC → WAV"
    color: "#101114"
    visible: false
    palette.window: "#191B20"
    palette.windowText: "#FFFFFF"
    palette.base: "#101114"
    palette.text: "#FFFFFF"
    palette.placeholderText: "#9CA3AF"
    palette.button: "#24272E"
    palette.buttonText: "#FFFFFF"

    // Palette shared with Main.qml.
    header: WindowControls { targetWindow: converterWindow }

    readonly property color bg: "#101114"
    readonly property color panel: "#191B20"
    readonly property color raised: "#24272E"
    readonly property color border: "#2B2E35"
    readonly property color textPrimary: "#FFFFFF"
    readonly property color textSecondary: "#9CA3AF"
    readonly property color accent: "#EF4444"
    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"

    function statusLabel(s) {
        switch (s) {
        case 1: return "Convirtiendo"
        case 2: return "OK"
        case 3: return "Error"
        default: return "Pendiente"
        }
    }

    function statusColor(s) {
        switch (s) {
        case 1: return converterWindow.warning
        case 2: return converterWindow.success
        case 3: return converterWindow.accent
        default: return "#6B7280"
        }
    }

    FileDialog {
        id: flacDialog
        title: "Selecciona archivos FLAC"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Archivos FLAC (*.flac)"]
        onAccepted: converter.addFiles(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        title: "Selecciona una carpeta de origen"
        onAccepted: converter.addFolder(selectedFolder)
    }

    FolderDialog {
        id: outputDialog
        title: "Selecciona la carpeta de destino"
        onAccepted: converter.setOutputDirectory(selectedFolder)
    }

    // Declared before the content so it sits behind it (lower z). Drop events
    // still reach it even though buttons/list items are painted on top.
    DropArea {
        id: dropArea
        anchors.fill: parent
        onDropped: (drop) => { dropHighlight.visible = false; converter.addFiles(drop.urls) }
        onEntered: (drag) => dropHighlight.visible = true
        onExited: dropHighlight.visible = false

        Rectangle {
            id: dropHighlight
            anchors.fill: parent
            visible: false
            color: "transparent"
            border.color: converterWindow.accent
            border.width: 2
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        // ---- Header --------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: "CONVERTIDOR FLAC → WAV"
                color: converterWindow.textPrimary
                font.pixelSize: 16
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: "PCM sin pérdida · conserva sample rate, canales, profundidad de bits y todos los tags (incluida la carátula) · verifica el PCM resultante"
                color: converterWindow.textSecondary
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }

        // ---- Action bar ----------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            OdeButton { text: "Añadir FLAC…"; enabled: !converter.running; onClicked: flacDialog.open() }
            OdeButton { text: "Añadir carpeta…"; enabled: !converter.running; onClicked: folderDialog.open() }
            OdeButton { text: "Carpeta de destino…"; enabled: !converter.running; onClicked: outputDialog.open() }
            Item { Layout.fillWidth: true }
            OdeButton {
                visible: converter.running
                text: "Cancelar"
                onClicked: converter.cancel()
            }
            OdeButton { text: "Limpiar"; enabled: !converter.running; onClicked: converter.clear() }
            OdeButton {
                primary: true
                text: "Convertir"
                enabled: !converter.running && converter.count > 0
                onClicked: converter.start()
            }
        }

        // ---- Output directory ----------------------------------------------
        Text {
            Layout.fillWidth: true
            text: "Carpeta de destino: " + (converter.outputDirectory || "—")
            color: converterWindow.textSecondary
            font.pixelSize: 11
            elide: Text.ElideMiddle
        }

        // ---- Progress ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                radius: 2
                color: converterWindow.border
                Rectangle {
                    width: parent.width * converter.progress / 100
                    height: parent.height
                    radius: 2
                    color: converterWindow.accent
                }
            }
            Text {
                text: converter.completedCount + " / " + converter.count
                color: converterWindow.textSecondary
                font.pixelSize: 11
            }
        }
        Text {
            Layout.fillWidth: true
            text: converter.statusText
            color: converterWindow.textSecondary
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }

        // ---- File list / empty state ---------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: converterWindow.panel
            border.color: converterWindow.border
            border.width: 1
            clip: true

            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: converter
                visible: converter.count > 0
                spacing: 4

                delegate: Rectangle {
                    width: listView.width
                    height: 56
                    radius: 5
                    color: converterWindow.raised
                    border.color: converterWindow.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 8
                        spacing: 10

                        // Status badge: 0 gris, 1 warning, 2 success, 3 accent.
                        Rectangle {
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 22
                            radius: 11
                            color: "transparent"
                            border.color: converterWindow.statusColor(model.status)
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: model.progressLabel || converterWindow.statusLabel(model.status)
                                color: converterWindow.statusColor(model.status)
                                font.pixelSize: 10
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: model.fileName
                                color: converterWindow.textPrimary
                                font.pixelSize: 12
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.message || ""
                                color: converterWindow.textSecondary
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: model.sampleRate / 1000 + " kHz · " + model.bitDepth + " bit · " + model.channels + " ch · " + model.tagCount + " tags"
                                color: converterWindow.textSecondary
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                text: model.lossless ? "PCM verificado" : ""
                                color: converterWindow.success
                                font.pixelSize: 10
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        OdeButton {
                            Layout.preferredWidth: 30
                            text: "✕"
                            onClicked: converter.removeAt(index)
                        }
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                visible: converter.count === 0
                spacing: 10
                width: Math.min(420, parent.width - 40)

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64; height: 64; radius: 32
                    color: converterWindow.raised
                    border.color: converterWindow.accent
                    border.width: 2
                    Text {
                        anchors.centerIn: parent
                        text: "↓"
                        color: converterWindow.accent
                        font.pixelSize: 30
                    }
                }
                Text {
                    width: parent.width
                    text: "Arrastra archivos .flac aquí o pulsa «Añadir FLAC…»"
                    color: converterWindow.textSecondary
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
