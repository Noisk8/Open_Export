#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace opendeck {
namespace core {

// AnalysisStatus mirrors the lifecycle of a track's musical analysis.
enum class AnalysisStatus {
    NotAnalyzed = 0,
    Analyzing = 1,
    Analyzed = 2,
    Failed = 3,
};

// A single beat of the analysed beat grid.
//
// `number` is the position inside the bar (1 = downbeat, 2..4 = the other
// beats), `time` is the offset in seconds from the start of the track and
// `tempo` is the BPM in effect at that beat.
struct Beat {
    int number = 1;
    double time = 0.0;
    double tempo = 0.0;
};

// The kind of cue stored for a track.
enum class CueKind {
    HotCue = 0, // hot cue A..H (1-based `hotCueNumber`)
    MemoryCue = 1,
    Loop = 2,
};

// A cue point, hot cue or loop.
//
// Hot cues are addressed by the player with the `hotCueNumber` (1 = A,
// 2 = B, ... 8 = H). Memory cues and loops use 0.
struct Cue {
    CueKind kind = CueKind::HotCue;
    int hotCueNumber = 0;
    double time = 0.0;     // seconds
    double loopTime = 0.0; // seconds; only meaningful for loops
    int colorId = 0;       // 0 = no colour, 1..8 = the rekordbox colour rows
    QString name;
    QString comment;
};

// Track is a plain value object describing a single imported track. It maps
// 1:1 onto the "tracks" table in the local SQLite library. Positions are stored
// with full precision where available; no early rounding to milliseconds.
//
// The fields mirror, one by one, the metadata that rekordbox stores in a
// DeviceSQL track row (see docs/rekordbox-export.md), so that the exporter can
// hand them to the player without having to guess anything.
struct Track {
    qint64 id = 0;
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString label;
    QString comment;
    QString isrc;
    QString mixName;
    QString composer;
    int year = 0;
    double duration = 0.0; // seconds, fractional allowed
    QString filePath;
    QString fileHash; // persistent identity, independent of path
    int sampleRate = 0;
    int sampleDepth = 0; // bits per sample
    int bitrate = 0;
    int channels = 0;
    double bpm = 0.0;
    QString musicalKey; // Camelot notation, e.g. "8A"
    int trackNumber = 0;
    int discNumber = 0;
    int rating = 0;  // 0..5 stars
    int colorId = 0; // 0 = none, 1..8 = the rekordbox colour rows
    AnalysisStatus analysisStatus = AnalysisStatus::NotAnalyzed;
    QDateTime createdAt;
    QDateTime dateAdded;
};

} // namespace core
} // namespace opendeck
