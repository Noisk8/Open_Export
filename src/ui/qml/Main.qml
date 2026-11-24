import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    visible: true
    width: 1440
    height: 800
    minimumWidth: 1000
    minimumHeight: 700
    title: qsTr("Open Deck Export")
    color: "#101114"
    palette.window: "#191B20"
    palette.windowText: "#FFFFFF"
    palette.base: "#101114"
    palette.text: "#FFFFFF"
    palette.placeholderText: "#9CA3AF"
    palette.button: "#24272E"
    palette.buttonText: "#FFFFFF"
    palette.highlight: "#EF4444"
    palette.highlightedText: "#FFFFFF"

    PreviewPlayer { id: preview }

    header: WindowControls { targetWindow: window }

    readonly property color bg: "#101114"
    readonly property color panel: "#191B20"
    readonly property color raised: "#24272E"
    readonly property color border: "#2B2E35"
    readonly property color textPrimary: "#FFFFFF"
    readonly property color textSecondary: "#9CA3AF"
    readonly property color accent: "#EF4444"
    readonly property color success: "#22C55E"
    readonly property color warning: "#F59E0B"

    onClosing: (event) => {
        if (exporter.running || analysis.running || converter.running || library.importing) {
            event.accepted = false
            window.setStatus("Hay tareas en curso. Espera a que terminen o cancélalas antes de cerrar.", false)
        }
    }

    // Playlist that receives the tracks added with the "+" button.
    property int selectedPlaylistId: 0
    property string selectedPlaylistName: ""
    property bool selectedIsFolder: false
    property var exportIds: []
    property int playlistRevision: 0
    function refreshFolders() { playlistRevision++; }
    function createNode(folder) {
        const id = playlists.createPlaylist(newPlaylistName.text, folderDestination.currentValue || 0, folder)
        if (id > 0) {
            selectedPlaylistId = id
            selectedPlaylistName = newPlaylistName.text.trim()
            selectedIsFolder = folder
            tracks.playlistId = 0
            newPlaylistName.text = ""
        }
    }
    Connections {
        target: playlists
        function onPlaylistsChanged() { window.refreshFolders() }
        function onErrorChanged() { if (playlists.lastError.length) window.setStatus(playlists.lastError, false) }
    }
    FileDialog {
        id: importFilesDialog
        title: "Importar música"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio (*.mp3 *.wav *.flac *.aiff *.aif *.m4a *.aac *.ogg)", "Todos los archivos (*)"]
        onAccepted: library.importAsync(selectedFiles)
    }
    FolderDialog {
        id: importFolderDialog
        title: "Importar carpeta de música"
        onAccepted: library.importAsync([selectedFolder])
    }

    // The track table hides columns that do not fit in a narrow window.
    readonly property bool showArtist: centralPane.width > 600
    readonly property bool showDetails: centralPane.width > 850
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

    Timer {
        id: missingNoticeTimer
        interval: 6000
        onTriggered: exportStatus.text = ""
    }

    function setStatus(message, ok) {
        missingNoticeTimer.stop()
        exportStatus.text = message
        exportStatus.color = ok ? window.success : window.accent
    }

    Dialog {
        id: removeMissingDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(420, window.width - 32)
        modal: true
        title: "Quitar pistas sin archivo"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property var trackIds: []
        onOpened: standardButton(Dialog.Ok).text = "Quitar pistas"
        contentItem: Label {
            text: "Se quitarán hasta " + removeMissingDialog.trackIds.length
                + " pistas de la biblioteca y de sus listas, incluidos sus cues y análisis. "
                + "Los archivos de audio se conservan. Si la música está en un disco desconectado, conéctalo primero."
            wrapMode: Text.WordWrap
            color: window.textPrimary
        }
        onAccepted: {
            const n = library.removeMissing(trackIds)
            window.setStatus(n < 0 ? "No se pudieron quitar las pistas"
                : n === 0 ? "Las pistas ya están disponibles o se quitaron anteriormente"
                : ("Pistas quitadas de la biblioteca: " + n), n >= 0)
            missingNoticeTimer.restart()
        }
    }

    property string pendingExportPath: ""
    property bool waitingForAnalysis: false
    property bool analyzeBeforeExport: false
    property var exportPreview: ({})
    function doExport(destination) {
        openExportConfirmation(destination, false)
    }
    function openExportConfirmation(destination, withAnalysis) {
        if (waitingForAnalysis || exporter.running) return
        analyzeBeforeExport = withAnalysis === true
        devicePopup.close()
        exportPreview = exporter.summary(destination)
        pendingExportPath = exportPreview.destination
        exportConfirm.open()
    }
    function analyzeAndExport(destination) {
        openExportConfirmation(destination, true)
    }
    Dialog {
        id: exportConfirm
        anchors.centerIn: Overlay.overlay
        width: Math.min(600, window.width - 40)
        modal: true
        title: "Revisar exportación"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: standardButton(Dialog.Ok).enabled = window.exportPreview.tracks > window.exportPreview.missing
                && (window.exportPreview.needsWav || 0) === 0
        onRejected: window.analyzeBeforeExport = false
        onAccepted: {
            if (window.analyzeBeforeExport) {
                window.waitingForAnalysis = true
                analysis.analyzeTracks(exporter.selectedTrackIds(), exporter.useWavForFlac)
            } else exporter.exportTo(window.pendingExportPath)
            window.analyzeBeforeExport = false
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Destino: " + (window.exportPreview.destination || "")
                + "\n" + (window.exportPreview.playlists || 0) + " listas · " + (window.exportPreview.tracks || 0) + " pistas únicas"
                + "\nAudio: " + ((window.exportPreview.audioBytes || 0) / 1048576).toFixed(1) + " MiB (más bases, análisis y temporales)"
                + "\nEspacio disponible: " + (window.exportPreview.availableBytes >= 0 ? (window.exportPreview.availableBytes / 1048576).toFixed(1) + " MiB" : "desconocido")
                + ((window.exportPreview.needsWav || 0) > 0
                    ? "\nFLAC sin WAV verificado disponible: " + window.exportPreview.needsWav
                    : "\nArchivos ausentes que se omitirán: " + (window.exportPreview.missing || 0))
                + "\nLa biblioteca del destino se reemplaza por esta selección. No se fusiona con otras listas del USB."
                + (window.exportPreview.availableBytes >= 0 && window.exportPreview.audioBytes > window.exportPreview.availableBytes
                    ? "\nEl audio supera el espacio libre; revisa el destino (una exportación incremental puede reutilizar archivos)." : "")
        }
    }

    Connections {
        target: exporter
        function onFinished(ok, message) {
            window.setStatus(message, ok)
            devices.refresh()
        }
    }

    Connections {
        target: analysis
        function onFinished(analyzed, failed) {
            if (window.waitingForAnalysis) {
                window.waitingForAnalysis = false
                window.setStatus("Análisis terminado", true)
                if (!analysis.cancelled && failed === 0) exporter.exportTo(window.pendingExportPath)
                else window.setStatus("Quedan pistas sin analizar. Revisa el resultado antes de exportar.", false)
            }
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
        height: Math.min(680, window.height - 40)
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
                OdeButton {
                    Layout.preferredHeight: 34
                    text: "Actualizar"
                    onClicked: devices.refresh()
                }
            }

            OdeCheckBox {
                text: "Exportar todas las playlists"
                checked: exporter.allPlaylists
                onToggled: exporter.allPlaylists = checked
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                visible: !exporter.allPlaylists
                model: playlists
                clip: true
                delegate: OdeCheckBox {
                    width: ListView.view.width
                    text: "  ".repeat(model.depth) + (model.isFolder ? "▸ " : "♫ ") + model.name
                    checked: window.exportIds.indexOf(model.playlistId) >= 0
                    onToggled: {
                        let ids = window.exportIds.slice()
                        if (checked) ids.push(model.playlistId)
                        else ids = ids.filter(id => id !== model.playlistId)
                        window.exportIds = ids
                        exporter.selectedPlaylists = ids
                    }
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
                        text: analysis.pendingCount + " pistas sin analizar: el beat grid, la tonalidad y la waveform saldrán vacíos. Pulsa «Analizar ahora» o usa «Analizar y exportar» en el dispositivo."
                        color: window.warning
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    OdeButton {
                        Layout.preferredHeight: 30
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
                    height: 76
                    radius: 6
                    color: window.raised
                    border.color: model.fat32 ? window.success : window.warning
                    border.width: 1
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
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
                            Text {
                                text: model.writable ? "Escribible" : "Sólo lectura"
                                color: model.writable ? window.textSecondary : window.accent
                                font.pixelSize: 11
                            }
                        }
                        RowLayout {
                            spacing: 6
                            Layout.alignment: Qt.AlignVCenter
                            OdeButton {
                                Layout.preferredWidth: 150
                                Layout.preferredHeight: 32
                                visible: analysis.pendingCount > 0 && !analysis.running
                                enabled: model.writable && analysis.ffmpegAvailable
                                text: "Analizar + exportar"
                                onClicked: window.analyzeAndExport(model.rootPath)
                            }
                            OdeButton {
                                Layout.preferredWidth: 116
                                Layout.preferredHeight: 32
                                Layout.alignment: Qt.AlignVCenter
                                text: "Exportar aquí"
                                primary: true
                                enabled: model.writable
                                onClicked: window.doExport(model.rootPath)
                            }
                        }
                    }
                }
            }

            OdeCheckBox {
                Layout.fillWidth: true
                text: "Perfil XDJ-RX original"
                checked: exporter.xdjRxProfile
                onToggled: exporter.xdjRxProfile = checked
            }
            OdeCheckBox {
                Layout.fillWidth: true
                text: "OneLibrary (exportLibrary.db)"
                enabled: !exporter.xdjRxProfile
                checked: !exporter.xdjRxProfile && exporter.writeDeviceLibrary
                onToggled: exporter.writeDeviceLibrary = checked
            }
            OdeCheckBox {
                Layout.fillWidth: true
                text: "Exportar los FLAC como WAV verificado"
                enabled: !exporter.xdjRxProfile
                checked: exporter.useWavForFlac
                onToggled: exporter.useWavForFlac = checked
            }
            OdeCheckBox {
                Layout.fillWidth: true
                text: "Exportación incremental (reutilizar audio y ANLZ sin cambios)"
                checked: exporter.incremental
                onToggled: exporter.incremental = checked
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                OdeButton {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 34
                    text: "Otra carpeta…"
                    onClicked: { devicePopup.close(); folderDialog.open() }
                }
                Item { Layout.fillWidth: true }
                OdeButton {
                    Layout.preferredWidth: 90
                    Layout.preferredHeight: 34
                    text: "Cerrar"
                    onClicked: devicePopup.close()
                }
            }
        }
    }

    // Fase 3: hot cues, memory cues and loops of the selected track.
    Popup {
        id: cuePopup
        property string audioPath: ""
        anchors.centerIn: Overlay.overlay
        width: Math.min(820, window.width - 40)
        height: Math.min(620, window.height - 40)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property string trackName: ""
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
                    text: "CUES · " + cuePopup.trackName
                    color: window.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Text {
                    text: cues.cueCount + " cues"
                    color: window.textSecondary
                    font.pixelSize: 11
                }
                OdeButton {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 30
                    text: "Cerrar"
                    onClicked: cuePopup.close()
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: cues
                spacing: 4
                delegate: Rectangle {
                    width: ListView.view.width
                    height: 44
                    radius: 5
                    color: window.raised
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 66
                            Layout.preferredHeight: 22
                            radius: 4
                            color: "#101114"
                            Text {
                                anchors.centerIn: parent
                                text: model.label
                                color: model.kind === 0 ? window.accent : window.success
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                        Text {
                            text: cues.timeLabel(model.time)
                                  + (model.kind === 2 ? " → " + cues.timeLabel(model.loopTime) : "")
                            color: window.textPrimary
                            font.pixelSize: 12
                            Layout.preferredWidth: 130
                        }
                        Text {
                            Layout.fillWidth: true
                            text: model.comment || model.name || "—"
                            color: window.textSecondary
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        OdeButton { text: "▶"; enabled: preview.available; onClicked: preview.play(cuePopup.audioPath, model.time, cuePopup.trackName) }
                        OdeButton {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 28
                            text: "✕"
                            onClicked: cues.removeCue(index)
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: window.border }

            GridLayout {
                Layout.fillWidth: true
                columns: 4
                rowSpacing: 6
                columnSpacing: 8

                ComboBox {
                    id: cueKind
                    Layout.fillWidth: true
                    model: ["Hot cue", "Memory cue", "Loop"]
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                    contentItem: Text { text: cueKind.displayText; color: window.textPrimary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                }
                ComboBox {
                    id: cueHotNumber
                    Layout.fillWidth: true
                    visible: cueKind.currentIndex === 0
                    model: ["A", "B", "C", "D", "E", "F", "G", "H"]
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                    contentItem: Text { text: cueHotNumber.displayText; color: window.textPrimary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                }
                ComboBox {
                    id: cueColor
                    Layout.fillWidth: true
                    model: ["Sin color", "Pink", "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple"]
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                    contentItem: Text { text: cueColor.displayText; color: window.textPrimary; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
                }
                OdeButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    text: "Añadir cue"
                    primary: true
                    onClicked: {
                        const t = parseFloat(cueTime.text)
                        const end = parseFloat(cueLoopEnd.text)
                        const kind = cueKind.currentIndex
                        const hot = kind === 0 ? cueHotNumber.currentIndex + 1 : 0
                        if (!isFinite(t) || t < 0) { window.setStatus("Tiempo inválido", false); return }
                        cues.addCue(kind, t, kind === 2 && isFinite(end) ? end : t,
                                    hot, cueColor.currentIndex, cueName.text, cueComment.text)
                        cueTime.text = ""; cueLoopEnd.text = ""; cueName.text = ""; cueComment.text = ""
                    }
                }

                TextField {
                    id: cueTime
                    Layout.fillWidth: true
                    placeholderText: "Tiempo (s)"
                    color: window.textPrimary
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                }
                TextField {
                    id: cueLoopEnd
                    Layout.fillWidth: true
                    visible: cueKind.currentIndex === 2
                    placeholderText: "Fin del loop (s)"
                    color: window.textPrimary
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                }
                TextField {
                    id: cueName
                    Layout.fillWidth: true
                    placeholderText: "Nombre"
                    color: window.textPrimary
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                }
                TextField {
                    id: cueComment
                    Layout.fillWidth: true
                    placeholderText: "Comentario"
                    color: window.textPrimary
                    background: Rectangle { radius: 4; color: "#101114"; border.color: window.border; border.width: 1 }
                }
            }
            PlaybackBar { Layout.fillWidth: true; controller: preview }
        }
    }

    footer: ColumnLayout {
        spacing: 0
        PlaybackBar { Layout.fillWidth: true; controller: preview }
        ToolBar {
            Layout.fillWidth: true
            implicitHeight: 38
            RowLayout {
                anchors.fill: parent
                OdeButton { text: "Importar archivos…"; enabled: !library.importing; onClicked: importFilesDialog.open() }
                OdeButton { text: "Importar carpeta…"; enabled: !library.importing; onClicked: importFolderDialog.open() }
                OdeButton { text: "Cancelar importación"; visible: library.importing; onClicked: library.cancelImport() }
                Label { Layout.fillWidth: true; text: library.importStatus; elide: Text.ElideRight }
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
                OdeButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: analysis.running ? "Analizando…" : "Analizar biblioteca"
                    enabled: !library.importing && !analysis.running && !exporter.running
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: library.missingCount > 0
                    Text {
                        Layout.fillWidth: true
                        text: library.missingCount + " pistas sin archivo: se omitirán al exportar"
                        color: warning
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        OdeButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            enabled: !library.importing && !analysis.running && !exporter.running
                            text: "Relocalizar"
                            onClicked: {
                                const n = library.relocateMissing()
                                window.setStatus(n > 0
                                    ? ("Relocalizadas " + n + " pistas")
                                    : library.missingCount === 0 ? "Todos los archivos están disponibles"
                                    : "No se encontraron los archivos; conecta el disco o quita las pistas ausentes",
                                    n > 0 || library.missingCount === 0)
                                missingNoticeTimer.restart()
                            }
                        }
                        OdeButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            enabled: !library.importing && !analysis.running && !exporter.running
                            text: "Quitar ausentes"
                            onClicked: {
                                const ids = library.missingTrackIds()
                                if (ids.length > 0) {
                                    removeMissingDialog.trackIds = ids
                                    removeMissingDialog.open()
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: playlists.totalTrackCount + " pistas en listas · " + library.trackCount + " en biblioteca"
                    color: success
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                }

                OdeButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    text: exporter.running ? "Exportando…" : "Exportar a USB…"
                    primary: true
                    enabled: playlists.totalTrackCount > 0 && !library.importing && !exporter.running && !analysis.running
                    onClicked: { devices.refresh(); devicePopup.open() }
                }

                OdeButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    text: "Convertir FLAC → WAV"
                    onClicked: converterWindow.show()
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
                OdeButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
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
                    dropHighlight.visible = false
                    if (drop.hasUrls) library.importAsync(drop.urls)
                }
                onEntered: (drag) => { if (!drag.hasUrls) drag.accepted = false; else dropHighlight.visible = true }
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
                        text: tracks.playlistId > 0 ? window.selectedPlaylistName : "BIBLIOTECA"
                        color: textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Text {
                        text: tracks.count + " pistas"
                        color: textSecondary
                        font.pixelSize: 12
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    OdeButton { text: "Seleccionar todo"; onClicked: tracks.selectVisible() }
                    OdeButton { text: "Limpiar"; onClicked: tracks.clearSelection() }
                    OdeButton {
                        text: tracks.playlistId > 0 ? "Quitar" : "Añadir"
                        enabled: tracks.selectedIds.length > 0 && window.selectedPlaylistId > 0 && !window.selectedIsFolder
                        onClicked: {
                            if (playlists.editTracks(window.selectedPlaylistId, tracks.selectedIds, tracks.playlistId > 0)) tracks.clearSelection()
                        }
                    }
                    Label { text: tracks.selectedIds.length + " selec."; color: textSecondary }
                }
                RowLayout {
                    Label { text: "Ordenar"; color: textSecondary }
                    ComboBox {
                        id: trackSort
                        model: ["Orden del set", "Título", "Artista", "BPM", "Tono", "Duración"]
                        onActivated: tracks.sortBy(["", "title", "artist", "bpm", "key", "duration"][currentIndex], descending.checked)
                    }
                    OdeCheckBox {
                        id: descending
                        text: "Descendente"
                        onToggled: tracks.sortBy(["", "title", "artist", "bpm", "key", "duration"][trackSort.currentIndex], checked)
                    }
                }
                TextField {
                    Layout.fillWidth: true
                    placeholderText: "Buscar título, artista, álbum o género"
                    onTextChanged: tracks.search = text
                }
                Text {
                    visible: tracks.count === 0
                    text: tracks.search.length > 0 ? "No hay coincidencias." : "Lista vacía. Abre Toda la música y añade pistas con +."
                    color: textSecondary
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    spacing: 0
                    Item { Layout.preferredWidth: 96 }
                    Text { text: "#"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 36 }
                    Text { text: "Título"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; Layout.minimumWidth: 110 }
                    Text { text: "Artista"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: window.artistWidth; visible: window.showArtist }
                    Text { text: "BPM"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 58; visible: window.showDetails }
                    Text { text: "Tono"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 48; visible: window.showDetails }
                    Text { text: "Dur."; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 56; visible: window.showDetails }
                    Text { text: "Estado"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 72; visible: window.showDetails }
                    Text { text: "+"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignHCenter }
                    Text { text: "Cues"; color: textSecondary; font.pixelSize: 12; font.bold: true; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: border }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: tracks
                    footer: Rectangle {
                        width: ListView.view.width
                        height: tracks.playlistId > 0 && trackSort.currentIndex === 0 && !descending.checked ? 30 : 0
                        visible: height > 0
                        color: raised
                        Text { anchors.centerIn: parent; text: "Soltar aquí para mover al final del set"; color: textSecondary; font.pixelSize: 11 }
                        DropArea {
                            anchors.fill: parent
                            keys: ["track-order"]
                            onDropped: (drop) => playlists.moveTrack(tracks.playlistId, drop.source.itemId, 0)
                        }
                    }
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 44
                        color: index % 2 === 0 ? panel : bg
                        radius: 4
                        DropArea {
                            anchors.fill: parent
                            keys: ["track-order"]
                            onDropped: (drop) => {
                                if (tracks.playlistId > 0 && trackSort.currentIndex === 0 && !descending.checked)
                                    playlists.moveTrack(tracks.playlistId, drop.source.itemId, model.trackId)
                            }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            spacing: 0
                            DragHandle {
                                kind: "track-order"
                                itemId: model.trackId
                                allowed: tracks.playlistId > 0 && trackSort.currentIndex === 0 && !descending.checked
                                Layout.preferredWidth: 24
                            }
                            OdeCheckBox {
                                Layout.preferredWidth: 32
                                checked: tracks.selectedIds.indexOf(model.trackId) >= 0
                                onToggled: tracks.toggleSelection(model.trackId)
                            }
                            OdeButton {
                                Layout.preferredWidth: 40
                                text: "▶"
                                enabled: preview.available
                                onClicked: preview.play(model.filePath, 0, model.title)
                            }
                            Text { text: index + 1; color: textSecondary; font.pixelSize: 13; Layout.preferredWidth: 36 }
                            Text { text: model.title || "(sin título)"; color: textPrimary; font.pixelSize: 14; Layout.fillWidth: true; Layout.minimumWidth: 110; elide: Text.ElideRight }
                            Text { text: model.artist || "—"; color: textSecondary; font.pixelSize: 13; Layout.preferredWidth: window.artistWidth; elide: Text.ElideRight; visible: window.showArtist }
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
                            OdeButton {
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 30
                                text: tracks.playlistId > 0 ? "−" : "+"
                                enabled: window.selectedPlaylistId > 0 && !window.selectedIsFolder
                                ToolTip.visible: hovered
                                ToolTip.text: tracks.playlistId > 0 ? "Quitar de esta lista (conservar archivo)"
                                    : "Añadir a «" + window.selectedPlaylistName + "»"
                                onClicked: {
                                    const ok = tracks.playlistId > 0
                                        ? playlists.removeTrack(window.selectedPlaylistId, model.trackId)
                                        : playlists.addTrack(window.selectedPlaylistId, model.trackId)
                                    window.setStatus(ok ? "Lista actualizada" : "No se pudo actualizar la lista", ok)
                                }
                            }
                            OdeButton {
                                Layout.preferredWidth: 44
                                Layout.preferredHeight: 30
                                text: "Cues"
                                ToolTip.visible: hovered
                                ToolTip.text: "Editar hot cues, memory cues y loops"
                                onClicked: {
                                    cues.setTrack(model.trackId)
                                    cues.duration = model.duration
                                    cuePopup.trackName = model.title || model.filePath
                                    cuePopup.audioPath = model.filePath
                                    cuePopup.open()
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 290
            Layout.fillHeight: true
            color: panel
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                Text { text: "PLAYLISTS Y CARPETAS"; color: textPrimary; font.bold: true }
                Text {
                    text: window.selectedPlaylistId ? "Selección: " + window.selectedPlaylistName : "Crea una lista para organizar tu música"
                    color: textSecondary
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                OdeButton { text: "Toda la música / añadir pistas"; Layout.fillWidth: true; onClicked: tracks.playlistId = 0 }
                ListView {
                    id: playlistTree
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: playlists
                    clip: true
                    spacing: 3
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 40
                        color: model.playlistId === window.selectedPlaylistId ? "#3A2020" : raised
                        DropArea {
                            anchors.fill: parent
                            keys: ["playlist-order"]
                            onDropped: (drop) => playlists.movePlaylist(drop.source.itemId, model.parentId, model.playlistId)
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Math.min(60, model.depth * 14)
                            DragHandle { kind: "playlist-order"; itemId: model.playlistId }
                            ItemDelegate {
                                Layout.fillWidth: true
                                text: (model.isFolder ? "▾ " : "♫ ") + model.name
                                contentItem: Text { text: parent.text; color: textPrimary; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                                onClicked: {
                                    window.selectedPlaylistId = model.playlistId
                                    window.selectedPlaylistName = model.name
                                    window.selectedIsFolder = model.isFolder
                                    tracks.playlistId = model.isFolder ? 0 : model.playlistId
                                }
                            }
                            Label { text: model.isFolder ? "" : model.trackCount; color: textSecondary; rightPadding: 6 }
                        }
                    }
                }
                RowLayout {
                    enabled: window.selectedPlaylistId > 0
                    OdeButton { text: "Nombre"; onClicked: { renameField.text = window.selectedPlaylistName; renameDialog.open() } }
                    OdeButton { text: "Mover"; onClicked: moveDialog.open() }
                    OdeButton { text: "Eliminar"; onClicked: deleteDialog.open() }
                }
                Label { text: "Crear dentro de"; color: textSecondary }
                ComboBox {
                    id: folderDestination
                    Layout.fillWidth: true
                    model: { window.playlistRevision; return playlists.folders() }
                    textRole: "name"
                    valueRole: "id"
                }
                TextField { id: newPlaylistName; Layout.fillWidth: true; placeholderText: "Nombre de lista o carpeta"; onAccepted: window.createNode(false) }
                RowLayout {
                    OdeButton { text: "+ Lista"; onClicked: window.createNode(false) }
                    OdeButton { text: "+ Carpeta"; onClicked: window.createNode(true) }
                }
                Text { text: "Arrastra ⠿ para cambiar el orden. Usa Mover para cambiar de carpeta."; color: textSecondary; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }
    Dialog {
        id: moveDialog
        title: "Mover a carpeta"
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ComboBox {
            id: moveDestination
            model: { window.playlistRevision; return playlists.folders() }
            textRole: "name"
            valueRole: "id"
            width: 260
        }
        onAccepted: playlists.movePlaylist(window.selectedPlaylistId, moveDestination.currentValue || 0)
    }

    Dialog {
        id: renameDialog
        anchors.centerIn: Overlay.overlay
        title: "Renombrar lista"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renameField; placeholderText: "Nombre" }
        onAccepted: {
            const ok = playlists.renamePlaylist(window.selectedPlaylistId, renameField.text)
            if (ok) window.selectedPlaylistName = renameField.text.trim()
            else window.setStatus("No se pudo renombrar la lista", false)
        }
    }
    Dialog {
        id: deleteDialog
        anchors.centerIn: Overlay.overlay
        title: "Eliminar «" + window.selectedPlaylistName + "»"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "Se elimina la lista o carpeta y sus listas descendientes. Los archivos de audio se conservan." }
        onAccepted: {
            if (playlists.removePlaylist(window.selectedPlaylistId)) {
                window.selectedPlaylistId = 0
                window.selectedPlaylistName = ""
                window.selectedIsFolder = false
                tracks.playlistId = 0
            } else window.setStatus("No se pudo eliminar la lista", false)
        }
    }

    // Ventana independiente del conversor FLAC → WAV. Se abre desde la barra
    // izquierda; no es modal y puede permanecer abierta mientras se exporta.
    ConverterWindow {
        id: converterWindow
    }
}
