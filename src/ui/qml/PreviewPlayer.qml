import QtQuick
import QtMultimedia

QtObject {
    id: root
    objectName: "previewPlayer"

    readonly property bool available: true
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool paused: player.playbackState === MediaPlayer.PausedState
    readonly property bool hasTrack: player.source.toString().length > 0
    readonly property bool seekable: player.seekable && duration > 0
    readonly property real position: player.position / 1000
    readonly property real duration: player.duration / 1000
    property alias volume: output.volume
    property alias muted: output.muted
    property string trackName: ""
    property string errorText: ""
    readonly property string status: errorText.length ? errorText
        : !hasTrack ? "Selecciona ▶ en una pista para escucharla"
        : playing ? "Reproduciendo" : paused ? "En pausa" : "Detenido"
    property real pendingPosition: -1

    property AudioOutput output: AudioOutput {
        id: output
        volume: 0.7
    }
    property MediaPlayer player: MediaPlayer {
        id: player
        audioOutput: output
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia)
                root.applyPendingPosition()
        }
        onSeekableChanged: root.applyPendingPosition()
        onDurationChanged: root.applyPendingPosition()
        onErrorOccurred: (error, errorString) => {
            root.pendingPosition = -1
            root.errorText = "No se pudo reproducir: " + errorString
        }
    }

    function applyPendingPosition() {
        if (pendingPosition >= 0 && player.seekable && player.duration > 0) {
            player.position = Math.min(pendingPosition, player.duration)
            pendingPosition = -1
        }
    }

    function play(path, seconds, title) {
        const localUrl = "file://" + path.split("/").map(encodeURIComponent).join("/")
        errorText = ""
        trackName = title || path.split("/").pop()
        const start = isFinite(seconds) ? Math.max(0, seconds) * 1000 : 0
        if (player.source.toString() === localUrl) {
            player.position = Math.min(start, player.duration)
        } else {
            player.stop()
            pendingPosition = start
            player.source = localUrl
        }
        player.play()
    }

    function togglePlayback() {
        if (!hasTrack) return
        if (playing) player.pause()
        else {
            if (player.mediaStatus === MediaPlayer.EndOfMedia) player.position = 0
            player.play()
        }
    }

    function seek(seconds) {
        if (seekable && isFinite(seconds))
            player.position = Math.round(Math.max(0, Math.min(seconds, duration)) * 1000)
    }

    function stop() {
        pendingPosition = -1
        player.stop()
    }
}
