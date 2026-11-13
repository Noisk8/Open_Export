#pragma once

#include <QList>
#include <QSqlQuery>

#include "core/models/track.h"

namespace opendeck {
namespace library {

class DatabaseManager;

// TrackRepository is the data-access layer for the "tracks" table.
class TrackRepository {
  public:
    explicit TrackRepository(DatabaseManager& db);

    // Inserts a track and returns its new id (0 on failure).
    qint64 insertTrack(const core::Track& track);

    // Updates the mutable metadata of an existing track.
    bool updateTrack(const core::Track& track);

    // Returns every track ordered by creation date, newest first.
    QList<core::Track> allTracks();

    // Returns the tracks that have not been analysed yet.
    QList<core::Track> unanalyzedTracks();

    // Returns the tracks with the given ids, in the given order. Missing ids
    // are silently skipped.
    QList<core::Track> tracksByIds(const QList<qint64>& ids);

    // Persistent identity helpers: a track is a duplicate if its content hash
    // (or, failing that, its path) is already known.
    [[nodiscard]] bool existsByHash(const QString& hash) const;
    [[nodiscard]] bool existsByPath(const QString& path) const;

    // Musical analysis, stored in the auxiliary tables of schema v2.
    QList<core::Beat> beats(qint64 trackId) const;
    bool replaceBeats(qint64 trackId, const QList<core::Beat>& beats);
    QList<core::Cue> cues(qint64 trackId) const;
    bool replaceCues(qint64 trackId, const QList<core::Cue>& cues);

    // Waveform peaks for one channel, stored as raw bytes (0..255 per peak).
    QByteArray waveform(qint64 trackId, int channel) const;
    bool storeWaveform(qint64 trackId, int channel, const QByteArray& peaks);

  private:
    core::Track readTrack(const QSqlQuery& q) const;

    DatabaseManager& m_db;
};

} // namespace library
} // namespace opendeck
