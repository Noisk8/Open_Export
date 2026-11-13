#pragma once

#include <QList>
#include <QString>
#include <functional>

#include "core/models/playlist.h"
#include "export/ExportModel.h"

namespace opendeck {
namespace library {
class DatabaseManager;
}

namespace device {

// ExportOptions configures an export.
struct ExportOptions {
    // The root of the media that is written, for example the root of a USB
    // stick. The directory has to exist.
    QString destination;

    // Copies the audio files next to the database.
    bool copyAudio = true;

    // Writes the ANLZ analysis files (beat grid, waveform and cues).
    bool writeAnalysis = true;

    // Writes the cover art of the tracks.
    bool writeArtwork = true;

    // Writes exportLibrary.db (Device Library Plus / OneLibrary), the
    // SQLCipher database used by the 2023+ Pioneer players.
    bool writeDeviceLibrary = true;

    // Reuses the audio and ANLZ files of an existing export when their content
    // has not changed. The databases are always rewritten.
    bool incremental = false;
};

// ExportReport summarises what an export produced.
struct ExportReport {
    int trackCount = 0;
    int playlistCount = 0;
    int analysisCount = 0;
    int artworkCount = 0;
    int skippedTracks = 0;  // incremental: reused without copying
    int removedTracks = 0;  // incremental: stale files deleted
    bool cancelled = false;
    QStringList warnings;
};

// ExportCallbacks lets a worker thread report progress and react to a cancel
// request without the exporter knowing anything about threads or the UI.
struct ExportCallbacks {
    // Called once per track: (current, total, short description).
    std::function<void(int, int, const QString&)> progress;
    // Polled between tracks; returning true aborts the export cleanly.
    std::function<bool()> cancelled;
};

// ExportManager turns the local library into a media that a Pioneer player can
// read.
//
// The layout of the media is the one that rekordbox uses:
//
//   /Contents/<artist>/<album>/<title>.<ext>   the audio
//   /PIONEER/rekordbox/export.pdb              the DeviceSQL database
//   /PIONEER/USBANLZ/P###/<hash>/ANLZ0000.*    the analysis of every track
//   /PIONEER/Artwork/<folder>/a<n>.jpg         the cover art
class ExportManager {
  public:
    // A manager without a database can still write an already prepared
    // library; that is what the export worker thread uses.
    ExportManager() = default;
    explicit ExportManager(library::DatabaseManager& db) : m_db(&db) {}

    // Builds the description of the export out of the given playlists. Every
    // track that the playlists refer to is exported, with its analysis data
    // preloaded so that write() is free of database access.
    [[nodiscard]] ExportLibrary buildLibrary(const QList<core::Playlist>& playlists) const;

    // Writes the export to the configured destination. This is the slow part
    // (audio can be gigabytes): call it from a worker thread.
    static ExportReport write(const ExportLibrary& library, const ExportOptions& options,
                              QString* error, const ExportCallbacks& callbacks = {});

    // The path of the audio file of a track, relative to the root of the
    // media. It follows the layout of rekordbox: a folder per artist and a
    // folder per album.
    [[nodiscard]] static QString audioPath(const core::Track& track);

    // The DeviceSQL file type of an audio file, from its extension.
    [[nodiscard]] static int fileTypeFor(const QString& filePath);

  private:
    // The local database, only needed to build the library (never to write it).
    library::DatabaseManager* m_db = nullptr;
};

} // namespace device
} // namespace opendeck
