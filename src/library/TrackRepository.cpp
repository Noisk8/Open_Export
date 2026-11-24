#include "library/TrackRepository.h"
#include "library/Savepoint.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "library/DatabaseManager.h"

namespace opendeck {
namespace library {

namespace {

// A default constructed QString is null, and the SQLite driver binds null
// strings as SQL NULL, which the NOT NULL constraints reject. Every text
// value is therefore normalised to an empty (but not null) string.
QString text(const QString& value) {
    return value.isNull() ? QString::fromUtf8("") : value;
}

constexpr const char* kTrackColumns =
        "id, title, artist, album, genre, label, comment, isrc, mix_name, composer, "
        "year, duration, file_path, file_hash, sample_rate, sample_depth, bitrate, "
        "channels, bpm, musical_key, track_number, disc_number, rating, color_id, "
        "analysis_status, created_at, date_added";
} // namespace

TrackRepository::TrackRepository(DatabaseManager& db) : m_db(db) {
}

qint64 TrackRepository::insertTrack(const core::Track& track) {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO tracks
            (title, artist, album, genre, label, comment, isrc, mix_name, composer,
             year, duration, file_path, file_hash, sample_rate, sample_depth, bitrate,
             channels, bpm, musical_key, track_number, disc_number, rating, color_id,
             analysis_status, date_added)
        VALUES
            (:title, :artist, :album, :genre, :label, :comment, :isrc, :mix_name, :composer,
             :year, :duration, :file_path, :file_hash, :sample_rate, :sample_depth, :bitrate,
             :channels, :bpm, :musical_key, :track_number, :disc_number, :rating, :color_id,
             :analysis_status, :date_added)
    )SQL"));
    q.bindValue(QStringLiteral(":title"), text(track.title));
    q.bindValue(QStringLiteral(":artist"), text(track.artist));
    q.bindValue(QStringLiteral(":album"), text(track.album));
    q.bindValue(QStringLiteral(":genre"), text(track.genre));
    q.bindValue(QStringLiteral(":label"), text(track.label));
    q.bindValue(QStringLiteral(":comment"), text(track.comment));
    q.bindValue(QStringLiteral(":isrc"), text(track.isrc));
    q.bindValue(QStringLiteral(":mix_name"), text(track.mixName));
    q.bindValue(QStringLiteral(":composer"), text(track.composer));
    q.bindValue(QStringLiteral(":year"), track.year);
    q.bindValue(QStringLiteral(":duration"), track.duration);
    q.bindValue(QStringLiteral(":file_path"), text(track.filePath));
    q.bindValue(QStringLiteral(":file_hash"), text(track.fileHash));
    q.bindValue(QStringLiteral(":sample_rate"), track.sampleRate);
    q.bindValue(QStringLiteral(":sample_depth"), track.sampleDepth);
    q.bindValue(QStringLiteral(":bitrate"), track.bitrate);
    q.bindValue(QStringLiteral(":channels"), track.channels);
    q.bindValue(QStringLiteral(":bpm"), track.bpm);
    q.bindValue(QStringLiteral(":musical_key"), text(track.musicalKey));
    q.bindValue(QStringLiteral(":track_number"), track.trackNumber);
    q.bindValue(QStringLiteral(":disc_number"), track.discNumber);
    q.bindValue(QStringLiteral(":rating"), track.rating);
    q.bindValue(QStringLiteral(":color_id"), track.colorId);
    q.bindValue(QStringLiteral(":analysis_status"),
            static_cast<int>(track.analysisStatus));
    q.bindValue(QStringLiteral(":date_added"),
            text(track.dateAdded.isValid() ? track.dateAdded.toString(Qt::ISODate)
                                           : (track.createdAt.isValid()
                                                      ? track.createdAt.toString(Qt::ISODate)
                                                      : QDateTime::currentDateTime()
                                                                .toString(Qt::ISODate))));

    if (!q.exec()) {
        qWarning("No se pudo insertar la pista: %s", qPrintable(q.lastError().text()));
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool TrackRepository::removeTracks(const QList<qint64>& ids) {
    Savepoint transaction(m_db.database());
    QSqlQuery query(m_db.database());
    if (!transaction.valid() || !query.prepare(QStringLiteral("DELETE FROM tracks WHERE id = ?")))
        return false;
    for (qint64 id : ids) {
        query.bindValue(0, id);
        if (!query.exec()) return false;
    }
    return transaction.commit();
}

bool TrackRepository::updateTrack(const core::Track& track) {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(R"SQL(
        UPDATE tracks SET
            title = :title, artist = :artist, album = :album, genre = :genre,
            label = :label, comment = :comment, isrc = :isrc, mix_name = :mix_name,
            composer = :composer, year = :year, duration = :duration,
            file_path = :file_path, sample_rate = :sample_rate,
            sample_depth = :sample_depth, bitrate = :bitrate, channels = :channels,
            bpm = :bpm, musical_key = :musical_key, track_number = :track_number,
            disc_number = :disc_number, rating = :rating, color_id = :color_id,
            analysis_status = :analysis_status
        WHERE id = :id
    )SQL"));
    q.bindValue(QStringLiteral(":id"), track.id);
    q.bindValue(QStringLiteral(":title"), text(track.title));
    q.bindValue(QStringLiteral(":artist"), text(track.artist));
    q.bindValue(QStringLiteral(":album"), text(track.album));
    q.bindValue(QStringLiteral(":genre"), text(track.genre));
    q.bindValue(QStringLiteral(":label"), text(track.label));
    q.bindValue(QStringLiteral(":comment"), text(track.comment));
    q.bindValue(QStringLiteral(":isrc"), text(track.isrc));
    q.bindValue(QStringLiteral(":mix_name"), text(track.mixName));
    q.bindValue(QStringLiteral(":composer"), text(track.composer));
    q.bindValue(QStringLiteral(":year"), track.year);
    q.bindValue(QStringLiteral(":duration"), track.duration);
    q.bindValue(QStringLiteral(":file_path"), text(track.filePath));
    q.bindValue(QStringLiteral(":sample_rate"), track.sampleRate);
    q.bindValue(QStringLiteral(":sample_depth"), track.sampleDepth);
    q.bindValue(QStringLiteral(":bitrate"), track.bitrate);
    q.bindValue(QStringLiteral(":channels"), track.channels);
    q.bindValue(QStringLiteral(":bpm"), track.bpm);
    q.bindValue(QStringLiteral(":musical_key"), text(track.musicalKey));
    q.bindValue(QStringLiteral(":track_number"), track.trackNumber);
    q.bindValue(QStringLiteral(":disc_number"), track.discNumber);
    q.bindValue(QStringLiteral(":rating"), track.rating);
    q.bindValue(QStringLiteral(":color_id"), track.colorId);
    q.bindValue(QStringLiteral(":analysis_status"),
            static_cast<int>(track.analysisStatus));
    return q.exec();
}

core::Track TrackRepository::readTrack(const QSqlQuery& q) const {
    core::Track t;
    t.id = q.value(0).toLongLong();
    t.title = q.value(1).toString();
    t.artist = q.value(2).toString();
    t.album = q.value(3).toString();
    t.genre = q.value(4).toString();
    t.label = q.value(5).toString();
    t.comment = q.value(6).toString();
    t.isrc = q.value(7).toString();
    t.mixName = q.value(8).toString();
    t.composer = q.value(9).toString();
    t.year = q.value(10).toInt();
    t.duration = q.value(11).toDouble();
    t.filePath = q.value(12).toString();
    t.fileHash = q.value(13).toString();
    t.sampleRate = q.value(14).toInt();
    t.sampleDepth = q.value(15).toInt();
    t.bitrate = q.value(16).toInt();
    t.channels = q.value(17).toInt();
    t.bpm = q.value(18).toDouble();
    t.musicalKey = q.value(19).toString();
    t.trackNumber = q.value(20).toInt();
    t.discNumber = q.value(21).toInt();
    t.rating = q.value(22).toInt();
    t.colorId = q.value(23).toInt();
    t.analysisStatus = static_cast<core::AnalysisStatus>(q.value(24).toInt());
    t.createdAt = q.value(25).toDateTime();
    t.dateAdded = q.value(26).toDateTime();
    if (!t.dateAdded.isValid()) {
        t.dateAdded = t.createdAt;
    }
    return t;
}

QList<core::Track> TrackRepository::allTracks() {
    QList<core::Track> tracks;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT %1 FROM tracks ORDER BY created_at DESC, id DESC")
                      .arg(QString::fromLatin1(kTrackColumns)));
    if (!q.exec()) {
        return tracks;
    }
    while (q.next()) {
        tracks.append(readTrack(q));
    }
    return tracks;
}

QList<core::Track> TrackRepository::unanalyzedTracks() {
    QList<core::Track> tracks;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT %1 FROM tracks "
                             "WHERE analysis_status != :done ORDER BY id")
                      .arg(QString::fromLatin1(kTrackColumns)));
    q.bindValue(QStringLiteral(":done"), static_cast<int>(core::AnalysisStatus::Analyzed));
    if (!q.exec()) {
        return tracks;
    }
    while (q.next()) {
        tracks.append(readTrack(q));
    }
    return tracks;
}

QList<core::Track> TrackRepository::tracksByIds(const QList<qint64>& ids) {
    QList<core::Track> tracks;
    if (ids.isEmpty()) {
        return tracks;
    }
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT %1 FROM tracks WHERE id = :id")
                      .arg(QString::fromLatin1(kTrackColumns)));
    for (qint64 id : ids) {
        q.bindValue(QStringLiteral(":id"), id);
        if (!q.exec() || !q.next()) {
            continue;
        }
        tracks.append(readTrack(q));
    }
    return tracks;
}

bool TrackRepository::existsByHash(const QString& hash) const {
    if (hash.isEmpty()) {
        return false;
    }
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT 1 FROM tracks WHERE file_hash = :h LIMIT 1"));
    q.bindValue(QStringLiteral(":h"), hash);
    return q.exec() && q.next();
}

bool TrackRepository::existsByPath(const QString& path) const {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT 1 FROM tracks WHERE file_path = :p LIMIT 1"));
    q.bindValue(QStringLiteral(":p"), path);
    return q.exec() && q.next();
}

QList<core::Beat> TrackRepository::beats(qint64 trackId) const {
    QList<core::Beat> result;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
            "SELECT beat_number, time, tempo FROM track_beats "
            "WHERE track_id = :t ORDER BY position"));
    q.bindValue(QStringLiteral(":t"), trackId);
    if (!q.exec()) {
        return result;
    }
    while (q.next()) {
        core::Beat beat;
        beat.number = q.value(0).toInt();
        beat.time = q.value(1).toDouble();
        beat.tempo = q.value(2).toDouble();
        result.append(beat);
    }
    return result;
}

bool TrackRepository::registerVerifiedWav(const QString& sourceHash, const QString& path,
                                          const QString& wavHash) {
    if (sourceHash.isEmpty() || path.isEmpty() || wavHash.isEmpty()) return false;
    QSqlQuery query(m_db.database());
    query.prepare(QStringLiteral("INSERT INTO verified_wav VALUES (?, ?, ?) "
                                 "ON CONFLICT(source_hash) DO UPDATE SET path=excluded.path, "
                                 "wav_hash=excluded.wav_hash"));
    query.addBindValue(sourceHash);
    query.addBindValue(path);
    query.addBindValue(wavHash);
    return query.exec();
}

QPair<QString, QString> TrackRepository::verifiedWav(const QString& sourceHash) const {
    QSqlQuery query(m_db.database());
    query.prepare(QStringLiteral("SELECT path, wav_hash FROM verified_wav WHERE source_hash=?"));
    query.addBindValue(sourceHash);
    if (!query.exec() || !query.next()) return {};
    return {query.value(0).toString(), query.value(1).toString()};
}

bool TrackRepository::saveAnalysis(const core::Track& track, const QList<core::Beat>& beats,
                                   const QByteArray& peaks) {
    Savepoint savepoint(m_db.database());
    return savepoint.valid() && updateTrack(track) && replaceBeats(track.id, beats)
            && storeWaveform(track.id, 0, peaks) && savepoint.commit();
}

bool TrackRepository::replaceBeats(qint64 trackId, const QList<core::Beat>& beats) {
    Savepoint savepoint(m_db.database());
    if (!savepoint.valid()) return false;
    {
        QSqlQuery del(m_db.database());
        del.prepare(QStringLiteral("DELETE FROM track_beats WHERE track_id = :t"));
        del.bindValue(QStringLiteral(":t"), trackId);
        if (!del.exec()) {
            return false;
        }
        QSqlQuery ins(m_db.database());
        ins.prepare(QStringLiteral(
                "INSERT INTO track_beats (track_id, position, beat_number, time, tempo) "
                "VALUES (:t, :p, :n, :time, :tempo)"));
        for (int i = 0; i < beats.size(); ++i) {
            const core::Beat& beat = beats.at(i);
            ins.bindValue(QStringLiteral(":t"), trackId);
            ins.bindValue(QStringLiteral(":p"), i);
            ins.bindValue(QStringLiteral(":n"), beat.number);
            ins.bindValue(QStringLiteral(":time"), beat.time);
            ins.bindValue(QStringLiteral(":tempo"), beat.tempo);
            if (!ins.exec()) {
                return false;
            }
        }
    }
    return savepoint.commit();
}

QList<core::Cue> TrackRepository::cues(qint64 trackId) const {
    QList<core::Cue> result;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
            "SELECT kind, hot_cue, time, loop_time, color_id, name, comment "
            "FROM track_cues WHERE track_id = :t ORDER BY id"));
    q.bindValue(QStringLiteral(":t"), trackId);
    if (!q.exec()) {
        return result;
    }
    while (q.next()) {
        core::Cue cue;
        cue.kind = static_cast<core::CueKind>(q.value(0).toInt());
        cue.hotCueNumber = q.value(1).toInt();
        cue.time = q.value(2).toDouble();
        cue.loopTime = q.value(3).toDouble();
        cue.colorId = q.value(4).toInt();
        cue.name = q.value(5).toString();
        cue.comment = q.value(6).toString();
        result.append(cue);
    }
    return result;
}

bool TrackRepository::replaceCues(qint64 trackId, const QList<core::Cue>& cues) {
    m_db.database().transaction();
    {
        QSqlQuery del(m_db.database());
        del.prepare(QStringLiteral("DELETE FROM track_cues WHERE track_id = :t"));
        del.bindValue(QStringLiteral(":t"), trackId);
        if (!del.exec()) {
            m_db.database().rollback();
            return false;
        }
        QSqlQuery ins(m_db.database());
        ins.prepare(QStringLiteral(
                "INSERT INTO track_cues (track_id, kind, hot_cue, time, loop_time, "
                "color_id, name, comment) "
                "VALUES (:t, :kind, :hot, :time, :loop, :color, :name, :comment)"));
        for (const core::Cue& cue : cues) {
            ins.bindValue(QStringLiteral(":t"), trackId);
            ins.bindValue(QStringLiteral(":kind"), static_cast<int>(cue.kind));
            ins.bindValue(QStringLiteral(":hot"), cue.hotCueNumber);
            ins.bindValue(QStringLiteral(":time"), cue.time);
            ins.bindValue(QStringLiteral(":loop"), cue.loopTime);
            ins.bindValue(QStringLiteral(":color"), cue.colorId);
            ins.bindValue(QStringLiteral(":name"), text(cue.name));
            ins.bindValue(QStringLiteral(":comment"), text(cue.comment));
            if (!ins.exec()) {
                m_db.database().rollback();
                return false;
            }
        }
    }
    return m_db.database().commit();
}

QByteArray TrackRepository::waveform(qint64 trackId, int channel) const {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("SELECT peaks FROM track_waveform WHERE track_id = :t AND channel = :c"));
    q.bindValue(QStringLiteral(":t"), trackId);
    q.bindValue(QStringLiteral(":c"), channel);
    if (!q.exec() || !q.next()) {
        return {};
    }
    return q.value(0).toByteArray();
}

bool TrackRepository::storeWaveform(qint64 trackId, int channel, const QByteArray& peaks) {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
            "INSERT INTO track_waveform (track_id, channel, peaks) VALUES (:t, :c, :p) "
            "ON CONFLICT(track_id, channel) DO UPDATE SET peaks = :p"));
    q.bindValue(QStringLiteral(":t"), trackId);
    q.bindValue(QStringLiteral(":c"), channel);
    q.bindValue(QStringLiteral(":p"), peaks);
    return q.exec();
}

} // namespace library
} // namespace opendeck
