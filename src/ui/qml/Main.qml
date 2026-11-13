import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 800
    minimumWidth: 1000
    minimumHeight: 600
    title: qsTr("Open Deck Export")
    color: "#101114"

    readonly property color bg: "#101114"
    readonly property color panel: "#191B20"
    readonly property color raised: "#24272E"
    readonly property color border: "#2B2E35"
    readonly property color textPrimary: "#FFFFFF"
    readonly property color textSecondary: "#9CA3AF"
    readonly property color accent: "#EF4444"
    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"

    // Playlist that receives the tracks added with the "+" button.
    property int selectedPlaylistId: 0
    property string selectedPlaylistName: ""

    // The track table hides columns that do not fit in a narrow window.
    readonly property bool showDetails: centralPane.width > 530
    readonly property int artistWidth: Math.max(96,
        Math.min(140, Math.round(centralPane.width * 0.26)))

    function statusLabel(s) {
        switch (s) {
        case 2: return "Analizada"
        case 1: return "Analizando"
        case 3: return "Error"
        default: return "Sin analizar"
        }
    }

    function formatDuration(secs) {
        if (!secs || secs <= 0) return "--:--"
        const total = Math.round(secs)
        const m = Math.floor(total / 60)
        const s = (total % 60).toString().padStart(2, "0")
        return m + ":" + s
    }

    function setStatus(message, ok) {
        exportStatus.text = message
        exportStatus.color = ok ? window.success : window.accent
    }

    function doExport(destination) {
        // Close the modal popup first: the export runs in the background and
        // a modal dialog left open would still block all interaction.
        devicePopup.close()
        exporter.exportTo(destination)
    }

    Connections {
        target: exporter
        function onFinished(ok, message) {
            window.setStatus(message, ok)
            devices.refresh()
        }
    }

    // Fallback for a folder that is not a removable volume (a test directory,
    // for example).
    FolderDialog {
        id: folderDialog
        title: "Elige la carpeta raíz del USB (o una carpeta de prueba)"
        onAccepted: window.doExport(selectedFolder)
    }

    // The export button lists the USB sticks that are actually plugged in.
    Popup {
        id: devicePopup
        anchors.centerIn: Overlay.overlay
        width: Math.min(560, window.width - 40)
        height: Math.min(430, window.height - 40)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 8
            color: window.panel
            border.color: window.border
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: "MEMORIAS USB DETECTADAS"
                    color: window.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                }
                Button {
                    text: "Actualizar"
                    onClicked: devices.refresh()
                }
            }

            // Warn when the analysis has not been run: without it the ANLZ
            // files have no beat grid, key or waveform.
            Rectangle {
                Layout.fillWidth: true
                visible: analysis.pendingCount > 0
                Layout.preferredHeight: warningColumn.implicitHeight + 16
                radius: 5
                color: "#33260F"
                border.color: window.warning
                border.width: 1
                ColumnLayout {
                    id: warningColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: analysis.pendingCount + " pistas sin analizar: el beat grid, la tonalidad y la waveform saldrán vacíos."
                        color: window.warning
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Button {
                        text: analysis.running ? "Analizando…" : "Analizar ahora"
                        enabled: !analysis.running && analysis.ffmpegAvailable
                        onClicked: analysis.analyzeAll()
                    }
                }
            }

            // No device connected.
            Column {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: devices.deviceCount === 0
                spacing: 8
                Text {
                    width: parent.width
                    text: "No se detectó ninguna memoria USB"
                    color: window.textPrimary
                    font.pixelSize: 14
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: "Conecta un pendrive formateado en FAT32 y pulsa «Actualizar». Los volúmenes internos se excluyen automáticamente."
                    color: window.textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: devices
                visible: devices.deviceCount > 0
                spacing: 6
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 78
                    radius: 6
                    color: window.raised
                    border.color: model.fat32 ? window.success : window.warning
                    border.width: 1
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 3
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "▣  " + model.name
                                color: window.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Text {
                                text: model.fat32 ? "FAT32" : model.fileSystem.toUpperCase()
                                color: model.fat32 ? window.success : window.warning
                                font.pixelSize: 11
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: model.summary
                            color: window.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: model.writable ? "Escribible" : "Sólo lectura"
                                color: model.writable ? window.textSecondary : window.accent
                                font.pixelSize: 11
                                Layout.fillWidth: true
                            }
                            Button {
                                text: "Exportar aquí"
                                enabled: model.writable
                                onClicked: window.doExport(model.rootPath)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: "Otra carpeta…"
                    onClicked: { devicePopup.close(); folderDialog.open() }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Cerrar"
                    onClicked: devicePopup.close()
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Sidebar -------------------------------------------------------
        Rectangle {
            // The window manager may force a small size (Wayland tiling), so
            // the side panels shrink instead of squeezing the track table.
            Layout.preferredWidth: Math.max(172, Math.min(216, window.width * 0.20))
            Layout.fillHeight: true
            color: panel

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                Text { text: "OPEN DECK"; color: textPrimary; font.pixelSize: 20; font.bold: true }
                Text { text: "EXPORT"; color: accent; font.pixelSize: 20; font.bold: true }

                Item { Layout.preferredHeight: 12 }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: border
                }

                Text { text: "Pasos"; color: textPrimary; font.pixelSize: 13; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: "1 · Arrastra tu música\n2 · Analiza la biblioteca\n3 · Crea una lista y pulsa +\n4 · Exporta al USB"
                    color: textSecondary
                    font.pixelSize: 11
                    lineHeight: 1.5
                    wrapMode: Text.WordWrap
                }

                Item { Layout.preferredHeight: 6 }

                Text { text: "ANÁLISIS"; color: textPrimary; font.pixelSize: 12; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    text: analysis.pendingCount + " pistas pendientes"
                    color: analysis.pendingCount > 0 ? warning : success
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
                Button {
                    Layout.fillWidth: true
                    text: analysis.running ? "Analizando…" : "Analizar biblioteca"
                    enabled: !analysis.running && !exporter.running
                             && analysis.pendingCount > 0 && analysis.ffmpegAvailable
                    onClicked: analysis.analyzeAll()
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    radius: 2
                    color: border
                    visible: analysis.running
                    Rectangle {
                        width: parent.width * analysis.progress / 100
                        height: parent.height
                        radius: 2
                        color: accent
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: analysis.ffmpegAvailable ? analysis.statusText : "ffmpeg no encontrado"
                    color: analysis.ffmpegAvailable ? textSecondary : accent
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }

                Text {
                    Layout.fillWidth: true
                    text: playlists.totalTrackCount + " pistas en listas · " + library.trackCount + " en biblioteca"
                    color: success
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: exporter.running ? "Exportando…" : "Exportar a USB…"
                    enabled: playlists.totalTrackCount > 0 && !exporter.running && !analysis.running
                    background: Rectangle {
                        radius: 6
                        color: parent.enabled ? window.accent : "#3A2020"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.pixelSize: 13
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: { devices.refresh(); devicePopup.open() }
                }

                // Progress of the background export, so the window never looks
                // frozen while gigabytes are copied to the stick.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 4
                    radius: 2
                    color: border
                    visible: exporter.running
                    Rectangle {
                        width: parent.width * exporter.progress / 100
                        height: parent.height
                        radius: 2
                        color: accent
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: exporter.running
                    text: exporter.statusText
                    color: textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
                Button {
                    Layout.fillWidth: true
                    visible: exporter.running
                    text: "Cancelar exportación"
                    onClicked: exporter.cancel()
                }

                Text {
                    id: exportStatus
                    Layout.fillWidth: true
                    text: ""
                    color: textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Text {
                    Layout.fillWidth: true
                    text: "Genera export.pdb + ANLZ para CDJ / XDJ-RX. Sin verificar en hardware."
                    color: textSecondary
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        // ---- Central content ----------------------------------------------
        Rectangle {
            id: centralPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: bg

            DropArea {
                id: dropArea
                anchors.fill: parent
                onDropped: (drop) => {
                    const n = library.importFiles(drop.urls)
                    window.setStatus(n > 0 ? ("Importadas " + n + " pistas") : "No se importó nada", n > 0)
                }
                onEntered: (drag) => dropHighlight.visible = true
                onExited: dropHighlight.visible = false

                Rectangle {
                    id: dropHighlight
                    anchors.fill: parent
                    visible: false
                    color: "transparent"
                    border.color: window.accent
                    border.width: 3
                }
            }

            // Empty state.
            Column {
                anchors.centerIn: parent
                visible: library.trackCount === 0
                spacing: 10
                width: 380

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 76; height: 76; radius: 38
                    color: panel
                    border.color: accent
                    border.width: 2
                    Text {
                        anchors.centerIn: parent
                        text: "↓"
                        color: accent
                        font.pixelSize: 34
                    }
                }
                Text {
                    width: parent.width
                    text: "Arrastra tus canciones aquí"
                    color: textPrimary
                    font.pixelSize: 24
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: "Suelta archivos de audio o carpetas. Después crea una lista en el panel de la derecha y añade pistas con el botón +."
                    color: textSecondary
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    width: parent.width
                    text: "MP3 · WAV · FLAC · AIFF · M4A · AAC · OGG"
                    color: textSecondary
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Track table.
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                visible: library.trackCount > 0
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "BIBLIOTECA"
                        color: textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Text {
                        text: library.trackCount + " pistas"
                        color: textSecondary
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    spacing: 0
                    Text { text: "#"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 36 }
                    Text { text: "Título"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 110 }
                    Text { text: "Artista"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: window.artistWidth }
                    Text { text: "BPM"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 58; visible: window.showDetails }
                    Text { text: "Tono"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 48; visible: window.showDetails }
                    Text { text: "Dur."; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 56; visible: window.showDetails }
                    Text { text: "Estado"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 72; visible: window.showDetails }
                    Text { text: "+"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignHCenter }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: border }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: library
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 44
                        color: index % 2 === 0 ? panel : bg
                        radius: 4
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            spacing: 0
                            Text { text: index + 1; color: textSecondary; font.pixelSize: 13; Layout.preferredWidth: 36 }
                            Text { text: model.title || "(sin título)"; color: textPrimary; font.pixelSize: 14; Layout.fillWidth: true; Layout.minimumWidth: 110; elide: Text.ElideRight }
                            Text { text: model.artist || "—"; color: textSecondary; font.pixelSize: 13; Layout.preferredWidth: window.artistWidth; elide: Text.ElideRight }
                            Text { text: model.bpm > 0 ? model.bpm.toFixed(1) : "—"; color: accent; font.pixelSize: 13; Layout.preferredWidth: 58; visible: window.showDetails }
                            Text { text: model.key || "—"; color: textPrimary; font.pixelSize: 13; Layout.preferredWidth: 48; visible: window.showDetails }
                            Text { text: window.formatDuration(model.duration); color: textSecondary; font.pixelSize: 13; Layout.preferredWidth: 56; visible: window.showDetails }
                            Text {
                                text: window.statusLabel(model.analysisStatus)
                                color: model.analysisStatus === 2 ? success
                                     : model.analysisStatus === 3 ? accent
                                     : textSecondary
                                font.pixelSize: 12
                                Layout.preferredWidth: 72
                                visible: window.showDetails
                            }
                            Button {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 30
                                text: "+"
                                enabled: window.selectedPlaylistId > 0
                                ToolTip.visible: hovered
                                ToolTip.text: enabled
                                    ? "Añadir a «" + window.selectedPlaylistName + "»"
                                    : "Selecciona primero una lista a la derecha"
                                background: Rectangle {
                                    radius: 4
                                    color: parent.enabled ? "#3A2020" : raised
                                    border.color: parent.enabled ? window.accent : border
                                    border.width: 1
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled ? window.accent : textSecondary
                                    font.pixelSize: 18
                                    font.bold: true
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                onClicked: {
                                    playlists.addTrack(window.selectedPlaylistId, model.trackId)
                                    window.setStatus("Añadida a «" + window.selectedPlaylistName + "»", true)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- Playlists -----------------------------------------------------
        Rectangle {
            Layout.preferredWidth: Math.max(224, Math.min(286, window.width * 0.28))
            Layout.fillHeight: true
            color: panel

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 1
                color: border
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Text { text: "PLAYLISTS"; color: textPrimary; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true }
                    Rectangle {
                        width: 26; height: 20; radius: 10
                        color: raised
                        Text {
                            anchors.centerIn: parent
                            text: playlists.playlistCount
                            color: textPrimary
                            font.pixelSize: 11
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: playlists.playlistCount === 0
                          ? "Todavía no hay listas. Crea una abajo."
                          : (window.selectedPlaylistId > 0
                             ? "Añadiendo a: «" + window.selectedPlaylistName + "»"
                             : "Haz clic en una lista para seleccionarla.")
                    color: window.selectedPlaylistId > 0 ? success : textSecondary
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: border }

                // Empty state of the list.
                Column {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: playlists.playlistCount === 0
                    spacing: 6
                    Text {
                        width: parent.width
                        text: "＋"
                        color: border
                        font.pixelSize: 40
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Text {
                        width: parent.width
                        text: "Las listas aparecerán aquí"
                        color: textSecondary
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: playlists
                    spacing: 3
                    visible: playlists.playlistCount > 0
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 42
                        radius: 5
                        color: model.playlistId === window.selectedPlaylistId ? "#3A2020"
                             : (index % 2 === 0 ? raised : "transparent")
                        border.color: model.playlistId === window.selectedPlaylistId
                                      ? window.accent : "transparent"
                        border.width: 1
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            Rectangle {
                                width: 9; height: 9; radius: 4
                                color: model.isFolder ? window.accent : "#6B7280"
                            }
                            Text {
                                text: model.name
                                color: textPrimary
                                font.pixelSize: 13
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                width: 24; height: 20; radius: 10
                                color: "#101114"
                                Text {
                                    anchors.centerIn: parent
                                    text: model.trackCount
                                    color: textSecondary
                                    font.pixelSize: 11
                                }
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                window.selectedPlaylistId = model.playlistId
                                window.selectedPlaylistName = model.name
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: border }

                Text { text: "Nueva lista"; color: textSecondary; font.pixelSize: 11 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: newPlaylistName
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        placeholderText: "Nombre de la lista"
                        color: textPrimary
                        background: Rectangle {
                            radius: 5
                            color: "#101114"
                            border.color: parent.activeFocus ? window.accent : border
                            border.width: 1
                        }
                        onAccepted: {
                            if (text.trim().length > 0) {
                                window.selectedPlaylistId = playlists.createPlaylist(text.trim())
                                window.selectedPlaylistName = text.trim()
                                text = ""
                            }
                        }
                    }
                    Button {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 36
                        text: "+"
                        background: Rectangle {
                            radius: 5
                            color: "#3A2020"
                            border.color: window.accent
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            color: window.accent
                            font.pixelSize: 18
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: {
                            if (newPlaylistName.text.trim().length > 0) {
                                window.selectedPlaylistId = playlists.createPlaylist(newPlaylistName.text.trim())
                                window.selectedPlaylistName = newPlaylistName.text.trim()
                                newPlaylistName.text = ""
                            }
                        }
                    }
                }
            }
        }
    }
}
