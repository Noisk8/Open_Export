#include "export/devicesql/DeviceSqlWriter.h"

#include <QCryptographicHash>
#include <QDate>
#include <QFileInfo>

#include "export/devicesql/DeviceSqlRow.h"
#include "export/devicesql/DeviceSqlString.h"
#include "export/MenuData.h"

namespace opendeck {
namespace device {

namespace {

// ---------------------------------------------------------------------------
// Track row layout (DeviceSQL table 0).
// ---------------------------------------------------------------------------

constexpr int kTrackRowSize = 0x88;

constexpr int kOffSubtype = 0x00;
constexpr int kOffIndexShift = 0x02;
constexpr int kOffBitmask = 0x04;
constexpr int kOffSampleRate = 0x08;
constexpr int kOffComposerId = 0x0c;
constexpr int kOffFileSize = 0x10;
constexpr int kOffContentId = 0x14;
constexpr int kOffUnknown18 = 0x18;
constexpr int kOffUnknown1a = 0x1a;
constexpr int kOffArtworkId = 0x1c;
constexpr int kOffKeyId = 0x20;
constexpr int kOffOriginalArtistId = 0x24;
constexpr int kOffLabelId = 0x28;
constexpr int kOffRemixerId = 0x2c;
constexpr int kOffBitrate = 0x30;
constexpr int kOffTrackNumber = 0x34;
constexpr int kOffTempo = 0x38;
constexpr int kOffGenreId = 0x3c;
constexpr int kOffAlbumId = 0x40;
constexpr int kOffArtistId = 0x44;
constexpr int kOffTrackId = 0x48;
constexpr int kOffDiscNumber = 0x4c;
constexpr int kOffPlayCount = 0x4e;
constexpr int kOffYear = 0x50;
constexpr int kOffSampleDepth = 0x52;
constexpr int kOffDuration = 0x54;
constexpr int kOffUnknown26 = 0x56;
constexpr int kOffColorId = 0x58;
constexpr int kOffRating = 0x59;
constexpr int kOffFileType = 0x5a;
constexpr int kOffUnknown30 = 0x5c;
constexpr int kOffStringSlots = 0x5e;

// The order of the 21 strings of a track row.
enum TrackString {
    Isrc = 0,
    Texter = 1,
    InformationUpdateCount = 2,
    AnalysisUpdateCount = 3,
    CueUpdateCount = 4,
    Message = 5,
    KuvoPublic = 6,
    AutoloadHotCues = 7,
    UnknownString5 = 8,
    UnknownString6 = 9,
    DateAdded = 10,
    ReleaseDate = 11,
    MixName = 12,
    UnknownString7 = 13,
    AnalyzePath = 14,
    AnalyzeDate = 15,
    Comment = 16,
    Title = 17,
    UnknownString8 = 18,
    FileName = 19,
    FilePath = 20,
};

constexpr quint16 kTrackSubtype = 0x0024;
constexpr quint32 kTrackBitmask = 0x000c0700;
constexpr quint16 kTrackUnknown18 = 0x2fdb;
constexpr quint16 kTrackUnknown1a = 0x8f45;
constexpr quint16 kTrackUnknown26 = 41;
constexpr quint16 kTrackUnknown30 = 3;

// A stable 28 bit identifier for the track, used where rekordbox stores the
// id of the content in its master database.
quint32 contentIdFor(const QString& seed) {
    const QByteArray digest =
            QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<quint8>(digest.at(i));
    }
    return (value & 0x0fffffffU) | 1U;
}

QString isoDate(const QDate& date) {
    return date.isValid() ? date.toString(QStringLiteral("yyyy-MM-dd"))
                           : QStringLiteral("1970-01-01");
}

} // namespace

void DeviceSqlWriter::setLibrary(const ExportLibrary& library) {
    m_library = library;
}

void DeviceSqlWriter::setCreationDate(const QString& date) {
    m_creationDate = date;
}

QByteArray DeviceSqlWriter::build() const {
    DeviceSqlDatabase db;

    // --- Create the twenty tables, in order --------------------------------
    for (quint32 type = 0; type < table::kCount; ++type) {
        db.createTable(type);
    }

    // --- Intern the small tables -------------------------------------------
    // Rekordbox stores the values that the track rows refer to by id in the
    // small tables, and interns them in the order they are first seen.
    QHash<QString, quint32> artistIds;
    QHash<QString, quint32> genreIds;
    QHash<QString, quint32> labelIds;
    QHash<QString, quint32> keyIds;
    QHash<QString, quint32> albumIds;

    auto intern = [](QHash<QString, quint32>& map, const QString& value) -> quint32 {
        if (value.isEmpty()) {
            return 0;
        }
        const auto it = map.constFind(value);
        if (it != map.constEnd()) {
            return it.value();
        }
        const quint32 id = static_cast<quint32>(map.size() + 1);
        map.insert(value, id);
        return id;
    };

    struct Album {
        QString name;
        QString artist;
        quint32 id = 0;
    };
    QList<Album> albums;

    QList<ResolvedTrack> resolved;
    resolved.reserve(m_library.tracks.size());
    for (const ExportTrack& item : m_library.tracks) {
        const core::Track& track = item.track;
        ResolvedTrack entry;
        entry.item = item;
        entry.artistId = intern(artistIds, track.artist);
        entry.genreId = intern(genreIds, track.genre);
        entry.labelId = intern(labelIds, track.label);
        entry.keyId = intern(keyIds, track.musicalKey);
        if (!track.album.isEmpty()) {
            const QString key = track.album + QChar(0x1f) + track.artist;
            const auto it = albumIds.constFind(key);
            if (it != albumIds.constEnd()) {
                entry.albumId = it.value();
            } else {
                Album album;
                album.name = track.album;
                album.artist = track.artist;
                album.id = static_cast<quint32>(albums.size() + 1);
                albums.append(album);
                albumIds.insert(key, album.id);
                entry.albumId = album.id;
            }
        }
        resolved.append(entry);
    }

    // --- Tracks --------------------------------------------------------------
    for (const ResolvedTrack& entry : resolved) {
        db.appendRow(table::kTracks, buildTrackRow(entry));
    }

    // --- Genres, labels, keys, artists, albums ------------------------------
    for (auto it = genreIds.constBegin(); it != genreIds.constEnd(); ++it) {
        db.appendRow(table::kGenres, buildGenreRow(it.value(), it.key()));
    }
    for (auto it = labelIds.constBegin(); it != labelIds.constEnd(); ++it) {
        db.appendRow(table::kLabels, buildLabelRow(it.value(), it.key()));
    }
    for (auto it = keyIds.constBegin(); it != keyIds.constEnd(); ++it) {
        db.appendRow(table::kKeys, buildKeyRow(it.value(), it.key()));
    }
    for (auto it = artistIds.constBegin(); it != artistIds.constEnd(); ++it) {
        db.appendRow(table::kArtists, buildArtistRow(it.value(), it.key()));
    }
    for (const Album& album : albums) {
        db.appendRow(table::kAlbums,
                buildAlbumRow(album.id, artistIds.value(album.artist, 0), album.name));
    }

    // --- Colours -------------------------------------------------------------
    for (int i = 0; i < 8; ++i) {
        db.appendRow(table::kColors,
                buildColorRow(static_cast<quint32>(i + 1),
                        QString::fromLatin1(kColorNames[i])));
    }

    // --- Artwork -------------------------------------------------------------
    for (const ResolvedTrack& entry : resolved) {
        if (entry.item.artworkId == 0 || entry.item.artworkPath.isEmpty()) {
            continue;
        }
        db.appendRow(table::kArtwork,
                buildArtworkRow(entry.item.artworkId, entry.item.artworkPath));
    }

    // --- Playlists -----------------------------------------------------------
    for (const ExportPlaylist& playlist : m_library.playlists) {
        db.appendRow(table::kPlaylistTree, buildPlaylistRow(playlist));
        for (int i = 0; i < playlist.trackIds.size(); ++i) {
            db.appendRow(table::kPlaylistEntries,
                    buildPlaylistEntryRow(static_cast<quint32>(i + 1),
                            playlist.trackIds.at(i), playlist.id));
        }
    }

    // --- Browse menus --------------------------------------------------------
    for (const ColumnEntry& column : kColumns) {
        db.appendRow(table::kColumns,
                buildColumnRow(column.id, column.kind,
                        QString::fromUtf8(kMenuWrapperOpen) + QString::fromLatin1(column.name)
                                + QString::fromUtf8(kMenuWrapperClose)));
    }
    for (const MenuEntry& entry : kCategories) {
        db.appendRow(table::kCategories,
                buildCategoryRow(entry.itemId, entry.sequence, entry.flags));
    }
    for (const MenuEntry& entry : kSortMenu) {
        db.appendRow(table::kSortMenu,
                buildSortRow(entry.itemId, entry.sequence, entry.flags));
    }

    // --- Export summary ------------------------------------------------------
    db.appendRow(table::kHistory,
            buildHistoryRow(static_cast<quint32>(m_library.tracks.size()),
                    m_creationDate.isEmpty() ? isoDate(QDate::currentDate())
                                             : m_creationDate));

    return db.serialize();
}

QByteArray DeviceSqlWriter::buildTrackRow(const ResolvedTrack& entry) const {
    const core::Track& track = entry.item.track;
    DeviceSqlRow row(kTrackRowSize);

    row.setU16(kOffSubtype, kTrackSubtype);
    row.setU32(kOffBitmask, kTrackBitmask);
    row.setU32(kOffSampleRate, static_cast<quint32>(qMax(0, track.sampleRate)));
    row.setU32(kOffComposerId, 0);
    row.setU32(kOffFileSize, static_cast<quint32>(qMax(0LL, entry.item.fileSize)));
    row.setU32(kOffContentId, contentIdFor(track.filePath.isEmpty() ? track.fileHash
                                                                    : track.filePath));
    row.setU16(kOffUnknown18, kTrackUnknown18);
    row.setU16(kOffUnknown1a, kTrackUnknown1a);
    row.setU32(kOffArtworkId, entry.item.artworkId);
    row.setU32(kOffKeyId, entry.keyId);
    row.setU32(kOffOriginalArtistId, 0);
    row.setU32(kOffLabelId, entry.labelId);
    row.setU32(kOffRemixerId, 0);
    row.setU32(kOffBitrate, static_cast<quint32>(qMax(0, track.bitrate)));
    row.setU32(kOffTrackNumber, static_cast<quint32>(qMax(0, track.trackNumber)));
    row.setU32(kOffTempo, static_cast<quint32>(qRound(qMax(0.0, track.bpm) * 100.0)));
    row.setU32(kOffGenreId, entry.genreId);
    row.setU32(kOffAlbumId, entry.albumId);
    row.setU32(kOffArtistId, entry.artistId);
    row.setU32(kOffTrackId, entry.item.deviceId);
    row.setU16(kOffDiscNumber, static_cast<quint16>(qBound(0, track.discNumber, 65535)));
    row.setU16(kOffPlayCount, 0);
    row.setU16(kOffYear, static_cast<quint16>(qBound(0, track.year, 65535)));
    row.setU16(kOffSampleDepth, static_cast<quint16>(qBound(0, track.sampleDepth, 65535)));
    row.setU16(kOffDuration, static_cast<quint16>(qBound(0, qRound(track.duration), 65535)));
    row.setU16(kOffUnknown26, kTrackUnknown26);
    row.setU8(kOffColorId, static_cast<quint8>(qBound(0, track.colorId, 255)));
    row.setU8(kOffRating, static_cast<quint8>(qBound(0, track.rating, 5)));
    row.setU16(kOffFileType, static_cast<quint16>(entry.item.fileType));
    row.setU16(kOffUnknown30, kTrackUnknown30);

    // The strings, in the order the format expects.
    auto setSlot = [&row](int index, const DeviceSqlString& value) {
        row.setString(kOffStringSlots + 2 * index, value);
    };

    if (track.isrc.isEmpty()) {
        setSlot(Isrc, DeviceSqlString::text(QString()));
    } else {
        setSlot(Isrc, DeviceSqlString::isrc(track.isrc));
    }
    setSlot(Texter, DeviceSqlString::text(track.composer));
    setSlot(InformationUpdateCount, DeviceSqlString::text(QStringLiteral("1")));
    setSlot(AnalysisUpdateCount, DeviceSqlString::text(QStringLiteral("1")));
    setSlot(CueUpdateCount, DeviceSqlString::text(QString()));
    setSlot(Message, DeviceSqlString::text(QString()));
    setSlot(KuvoPublic, DeviceSqlString::text(QStringLiteral("ON")));
    setSlot(AutoloadHotCues, DeviceSqlString::text(QStringLiteral("ON")));
    setSlot(UnknownString5, DeviceSqlString::text(QString()));
    setSlot(UnknownString6, DeviceSqlString::text(QString()));
    setSlot(DateAdded,
            DeviceSqlString::text(isoDate(track.dateAdded.isValid() ? track.dateAdded.date()
                                                                     : QDate())));
    setSlot(ReleaseDate, DeviceSqlString::text(QString()));
    setSlot(MixName, DeviceSqlString::text(track.mixName));
    setSlot(UnknownString7, DeviceSqlString::text(QString()));
    setSlot(AnalyzePath, DeviceSqlString::text(entry.item.analyzePath));
    setSlot(AnalyzeDate, DeviceSqlString::text(QString()));
    setSlot(Comment, DeviceSqlString::text(track.comment));
    // rekordbox falls back to the file name when a track has no title tag.
    const QString title = track.title.isEmpty()
            ? QFileInfo(track.filePath).completeBaseName()
            : track.title;
    setSlot(Title, DeviceSqlString::text(title));
    setSlot(UnknownString8, DeviceSqlString::text(QString()));
    setSlot(FileName, DeviceSqlString::text(entry.item.fileName));
    setSlot(FilePath, DeviceSqlString::text(entry.item.devicePath));

    return row.bytes();
}

QByteArray DeviceSqlWriter::buildPlaylistRow(const ExportPlaylist& playlist) const {
    DeviceSqlRow row(20);
    row.setU32(0x00, playlist.parentId);
    row.setU32(0x04, 0);
    row.setU32(0x08, playlist.sortOrder);
    row.setU32(0x0c, playlist.id);
    row.setU32(0x10, playlist.isFolder ? 1u : 0u);
    row.appendString(DeviceSqlString::text(playlist.name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildPlaylistEntryRow(quint32 entryIndex, quint32 trackId,
                                                   quint32 playlistId) const {
    DeviceSqlRow row(12);
    row.setU32(0x00, entryIndex);
    row.setU32(0x04, trackId);
    row.setU32(0x08, playlistId);
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildGenreRow(quint32 id, const QString& name) const {
    DeviceSqlRow row(4);
    row.setU32(0x00, id);
    row.appendString(DeviceSqlString::text(name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildLabelRow(quint32 id, const QString& name) const {
    DeviceSqlRow row(4);
    row.setU32(0x00, id);
    row.appendString(DeviceSqlString::text(name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildKeyRow(quint32 id, const QString& name) const {
    DeviceSqlRow row(8);
    row.setU32(0x00, id);
    row.setU32(0x04, id);
    row.appendString(DeviceSqlString::text(name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildArtistRow(quint32 id, const QString& name) const {
    // subtype, index shift, id, 0x03 and the offset of the name.
    DeviceSqlRow row(10);
    row.setU16(0x00, 0x0060);
    row.setU32(0x04, id);
    row.setU8(0x08, 0x03);
    const int offset = row.appendString(DeviceSqlString::text(name));
    row.setU8(0x09, static_cast<quint8>(offset));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildAlbumRow(quint32 id, quint32 artistId,
                                           const QString& name) const {
    DeviceSqlRow row(22);
    row.setU16(0x00, 0x0080);
    row.setU32(0x08, artistId);
    row.setU32(0x0c, id);
    row.setU8(0x14, 0x03);
    const int offset = row.appendString(DeviceSqlString::text(name));
    row.setU8(0x15, static_cast<quint8>(offset));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildColorRow(quint32 id, const QString& name) const {
    DeviceSqlRow row(8);
    row.setU32(0x00, 0);
    row.setU8(0x04, static_cast<quint8>(id));
    row.setU16(0x05, static_cast<quint16>(id));
    row.setU8(0x07, 0);
    row.appendString(DeviceSqlString::text(name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildArtworkRow(quint32 id, const QString& path) const {
    DeviceSqlRow row(4);
    row.setU32(0x00, id);
    row.appendString(DeviceSqlString::text(path));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildColumnRow(quint16 id, quint16 kind,
                                             const QString& name) const {
    DeviceSqlRow row(4);
    row.setU16(0x00, id);
    row.setU16(0x02, kind);
    row.appendString(DeviceSqlString::text(name));
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildCategoryRow(quint16 itemId, quint16 sequence,
                                               quint32 flags) const {
    DeviceSqlRow row(8);
    row.setU16(0x00, itemId);
    row.setU16(0x02, sequence);
    row.setU32(0x04, flags);
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildSortRow(quint16 itemId, quint16 sequence,
                                           quint32 visible) const {
    DeviceSqlRow row(8);
    row.setU16(0x00, itemId);
    row.setU16(0x02, sequence);
    row.setU32(0x04, visible);
    return row.bytes();
}

QByteArray DeviceSqlWriter::buildHistoryRow(quint32 trackCount,
                                              const QString& date) const {
    DeviceSqlRow row(12);
    row.setU16(0x00, 0x0280);
    row.setU32(0x04, trackCount);
    row.setU32(0x08, 0);
    row.appendString(DeviceSqlString::text(date));
    row.appendRaw(QByteArray::fromHex("191e")); // 0x1e19, a version marker
    row.appendString(DeviceSqlString::text(QStringLiteral("1000")));
    row.appendString(DeviceSqlString::text(QString()));
    return row.bytes();
}

} // namespace device
} // namespace opendeck
