#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "core/models/track.h"

namespace opendeck {
namespace device {

// AnlzData is the analysis of one track, ready to be written as ANLZ files.
struct AnlzData {
    QString devicePath;             // "/Contents/Artist/Album/track.mp3"
    double duration = 0.0;          // seconds
    QList<core::Beat> beats;        // the beat grid
    QList<core::Cue> cues;          // hot cues, memory cues and loops
    QByteArray peaks;               // mono envelope, 150 columns per second
};

// AnlzWriter writes the analysis files of a Pioneer export.
//
// Rekordbox stores the analysis of a track in up to three files that live
// next to each other:
//
//   ANLZ0000.DAT  path, VBR index, beat grid, monochrome preview waveforms
//                 and the classic cue lists;
//   ANLZ0000.EXT  detail and colour waveforms plus the extended cue lists
//                 that carry colours and comments;
//   ANLZ0000.2EX  the three band waveforms of the CDJ-3000.
//
// Every integer of these files is big endian, the opposite of `export.pdb`.
//
// See <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html>.
class AnlzWriter {
  public:
    // Builds the .DAT file of a track.
    [[nodiscard]] static QByteArray buildDat(const AnlzData& data);

    // Builds the .EXT file of a track.
    [[nodiscard]] static QByteArray buildExt(const AnlzData& data);

    // Builds the .2EX file of a track.
    [[nodiscard]] static QByteArray build2Ex(const AnlzData& data);

    // The directory that holds the analysis of a track, relative to the root
    // of the media, for example "/PIONEER/USBANLZ/P05A/0002ABCD".
    //
    // The name is a hash of the path of the audio file: the CDJ-3000
    // recomputes it and looks the analysis up there, ignoring the path that
    // the database stores.
    [[nodiscard]] static QString analysisDirectory(const QString& devicePath);

    // The path of the .DAT file of a track, relative to the root of the media.
    [[nodiscard]] static QString analysisPath(const QString& devicePath);
};

} // namespace device
} // namespace opendeck
