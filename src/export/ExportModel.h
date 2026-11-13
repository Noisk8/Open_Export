#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "core/models/track.h"

namespace opendeck {
namespace device {

// ExportTrack is a track prepared for a Pioneer export.
//
// It carries the library metadata plus the three paths that the player needs
// to find the file and its analysis on the exported media. All the paths are
// volume absolute and use forward slashes, exactly like the ones rekordbox
// writes.
struct ExportTrack {
    core::Track track;
    quint32 deviceId = 0;   // id of the row in the DeviceSQL track table
    QString fileName;       // "track.mp3"
    QString devicePath;     // "/Contents/Artist/Album/track.mp3"
    QString analyzePath;    // "/PIONEER/USBANLZ/P0AB/0123ABCD/ANLZ0000.DAT"
    QString artworkPath;    // "/PIONEER/Artwork/00001/a1.jpg"
    qint64 fileSize = 0;    // bytes
    int fileType = 0;       // DeviceSQL file type enum
    quint32 artworkId = 0;  // 0 = no artwork

    // Musical analysis, resolved on the GUI thread so that the export worker
    // never has to touch the SQLite connection.
    QList<core::Beat> beats;
    QList<core::Cue> cues;
    QByteArray peaks;        // 150 columns per second
};

// ExportPlaylist is a playlist or folder prepared for a Pioneer export.
//
// `parentId` refers to the DeviceSQL id of the enclosing folder; zero means
// that the node lives at the root of the playlist menu.
struct ExportPlaylist {
    quint32 id = 0;
    QString name;
    quint32 parentId = 0;
    quint32 sortOrder = 0;
    bool isFolder = false;
    QList<quint32> trackIds; // DeviceSQL ids of the tracks, in order
};

// ExportLibrary is the whole content of an export.
struct ExportLibrary {
    QList<ExportTrack> tracks;
    QList<ExportPlaylist> playlists;
};

} // namespace device
} // namespace opendeck
