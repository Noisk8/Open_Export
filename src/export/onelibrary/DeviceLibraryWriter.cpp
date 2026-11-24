#include "export/onelibrary/DeviceLibraryWriter.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include <QCryptographicHash>

#include "export/MenuData.h"
#include "export/onelibrary/SqlCipherWriter.h"

namespace opendeck {
namespace device {
namespace onelibrary {

namespace {

bool exec(QSqlDatabase& db, const QString& statement, QString* error) {
    QSqlQuery query(db);
    if (query.exec(statement)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("OneLibrary: %1 (%2)")
                         .arg(query.lastError().text(), statement.left(80));
    }
    return false;
}

// The 22 tables of a Device Library Plus database.
const char* kSchema[] = {
        R"SQL(CREATE TABLE content(
            content_id integer primary key, title varchar, titleForSearch varchar,
            subtitle varchar, bpmx100 integer, length integer, trackNo integer,
            discNo integer, artist_id_artist integer, artist_id_remixer integer,
            artist_id_originalArtist integer, artist_id_composer integer,
            artist_id_lyricist integer, album_id integer, genre_id integer,
            label_id integer, key_id integer, color_id integer, image_id integer,
            djComment varchar, rating integer, releaseYear integer, releaseDate varchar,
            dateCreated varchar, dateAdded varchar, path varchar, fileName varchar,
            fileSize integer, fileType integer, bitrate integer, bitDepth integer,
            samplingRate integer, isrc varchar, djPlayCount integer,
            isHotCueAutoLoadOn integer, isKuvoDeliverStatusOn integer,
            kuvoDeliveryComment varchar, masterDbId integer, masterContentId integer,
            analysisDataFilePath varchar, analysedBits integer, contentLink integer,
            hasModified integer, cueUpdateCount integer, analysisDataUpdateCount integer,
            informationUpdateCount integer))SQL",
        R"SQL(CREATE TABLE artist(artist_id integer primary key, name varchar,
            nameForSearch varchar))SQL",
        R"SQL(CREATE TABLE album(album_id integer primary key, name varchar,
            artist_id integer, image_id integer, isComplation integer,
            nameForSearch varchar))SQL",
        "CREATE TABLE genre(genre_id integer primary key, name varchar)",
        "CREATE TABLE label(label_id integer primary key, name varchar)",
        "CREATE TABLE key(key_id integer primary key, name varchar)",
        "CREATE TABLE color(color_id integer primary key, name varchar)",
        "CREATE TABLE image(image_id integer primary key, path varchar)",
        R"SQL(CREATE TABLE cue(
            cue_id integer primary key, content_id integer, kind integer,
            colorTableIndex integer, cueComment varchar, isActiveLoop integer,
            beatLoopNumerator integer, beatLoopDenominator integer,
            inUsec integer, outUsec integer, in150FramePerSec integer,
            out150FramePerSec integer, inMpegFrameNumber integer,
            outMpegFrameNumber integer, inMpegAbs integer, outMpegAbs integer,
            inDecodingStartFramePosition integer, outDecodingStartFramePosition integer,
            inFileOffsetInBlock integer, OutFileOffsetInBlock integer,
            inNumberOfSampleInBlock integer, outNumberOfSampleInBlock integer))SQL",
        R"SQL(CREATE TABLE playlist(playlist_id integer primary key, sequenceNo integer,
            name varchar, image_id integer, attribute integer,
            playlist_id_parent integer))SQL",
        R"SQL(CREATE TABLE playlist_content(playlist_id integer, content_id integer,
            sequenceNo integer))SQL",
        R"SQL(CREATE TABLE history(history_id integer primary key, sequenceNo integer,
            name varchar, attribute integer, history_id_parent integer))SQL",
        R"SQL(CREATE TABLE history_content(history_id integer, content_id integer,
            sequenceNo integer))SQL",
        R"SQL(CREATE TABLE hotCueBankList(hotCueBankList_id integer primary key,
            sequenceNo integer, name varchar, image_id integer, attribute integer,
            hotCueBankList_id_parent integer))SQL",
        R"SQL(CREATE TABLE hotCueBankList_cue(hotCueBankList_id integer, cue_id integer,
            sequenceNo integer))SQL",
        R"SQL(CREATE TABLE myTag(myTag_id integer primary key, sequenceNo integer,
            name varchar, attribute integer, myTag_id_parent integer))SQL",
        R"SQL(CREATE TABLE myTag_content(myTag_id integer, content_id integer))SQL",
        R"SQL(CREATE TABLE menuItem(menuItem_id integer primary key, kind integer,
            name varchar))SQL",
        R"SQL(CREATE TABLE category(category_id integer primary key, menuItem_id integer,
            sequenceNo integer, isVisible integer))SQL",
        R"SQL(CREATE TABLE sort(sort_id integer primary key, menuItem_id integer,
            sequenceNo integer, isVisible integer, isSelectedAsSubColumn integer))SQL",
        R"SQL(CREATE TABLE property(deviceName varchar, dbVersion varchar,
            numberOfContents integer, createdDate varchar, backGroundColorType integer,
            myTagMasterDBID integer))SQL",
        R"SQL(CREATE TABLE recommendedLike(content_id_1 integer, content_id_2 integer,
            rating integer, createdDate integer))SQL",
};

// Runs a prepared insert and always reports which step failed.
bool insert(QSqlQuery& query, bool& ok, QString* error, const QString& what) {
    if (query.exec()) {
        return true;
    }
    ok = false;
    if (error) {
        *error = QStringLiteral("%1: %2").arg(what, query.lastError().text());
    }
    return false;
}

QString text(const QString& value) {
    return value.isNull() ? QString::fromUtf8("") : value;
}

QString searchName(const QString& value) {
    return text(value).toLower();
}

// Returns the id of `value` in an interned lookup table, inserting it if new.
int intern(QSqlDatabase& db, QHash<QString, int>& map, const QString& table,
           const QString& idColumn, const QString& nameColumn, const QString& value,
           QString* error) {
    if (value.isEmpty()) {
        return 0;
    }
    const auto it = map.constFind(value);
    if (it != map.constEnd()) {
        return it.value();
    }
    const int id = map.size() + 1;
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO %1 (%2, %3) VALUES (?, ?)")
                          .arg(table, idColumn, nameColumn));
    query.addBindValue(id);
    query.addBindValue(text(value));
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return 0;
    }
    map.insert(value, id);
    return id;
}

bool patchReservedBytes(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    QByteArray data = file.readAll();
    if (data.size() < 21 || (data.size() % SqlCipherWriter::kPageSize) != 0) {
        if (error) {
            *error = QStringLiteral("El SQLite temporal no tiene páginas de 4096 bytes");
        }
        return false;
    }
    // SQLite ignores `PRAGMA reserve_size` from SQL. Setting byte 20 of the
    // header and running VACUUM makes SQLite rewrite every page with 80 bytes
    // of free space at the end, which is where SQLCipher stores IV + HMAC.
    data[20] = char(SqlCipherWriter::kReservedBytes);
    if (!file.seek(0) || file.write(data) != data.size()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.close();
    return true;
}

bool vacuum(const QString& path, QString* error) {
    const QString connection =
            QStringLiteral("ode_dlp_vacuum_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (!db.open()) {
            if (error) {
                *error = db.lastError().text();
            }
        } else {
            QSqlQuery query(db);
            ok = query.exec(QStringLiteral("VACUUM"));
            if (!ok && error) {
                *error = query.lastError().text();
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

quint32 contentLinkFor(const QString& seed) {
    const QByteArray digest =
            QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Sha256);
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<quint8>(digest.at(i));
    }
    return (value & 0x0fffffffU) | 1U;
}

} // namespace

bool DeviceLibraryWriter::write(const QString& databasePath, const ExportLibrary& library,
                                const QString& createdDate, QString* error) {
    if (error) {
        error->clear();
    }
    const QFileInfo target(databasePath);
    if (!QDir().mkpath(target.absolutePath())) {
        if (error) {
            *error = QStringLiteral("No se pudo crear %1").arg(target.absolutePath());
        }
        return false;
    }
    const QString plainPath = databasePath + QStringLiteral(".plain");
    QFile::remove(plainPath);
    QFile::remove(databasePath);

    const QString connection =
            QStringLiteral("ode_dlp_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(plainPath);
        if (!db.open()) {
            if (error) {
                *error = db.lastError().text();
            }
        } else {
            ok = exec(db, QStringLiteral("PRAGMA page_size=4096"), error)
                    && exec(db, QStringLiteral("PRAGMA journal_mode=DELETE"), error)
                    && exec(db, QStringLiteral("PRAGMA encoding='UTF-8'"), error);
            for (const char* statement : kSchema) {
                ok = ok && exec(db, QString::fromUtf8(statement), error);
            }

            QHash<QString, int> artists, albums, genres, labels, keys, colors;
            QHash<quint32, QString> images;
            QHash<quint32, QList<qint64>> cueIds;

            for (const ExportTrack& item : library.tracks) {
                const core::Track& track = item.track;
                const int artistId = intern(db, artists, QStringLiteral("artist"),
                        QStringLiteral("artist_id"), QStringLiteral("name"), track.artist, error);
                const int composerId = intern(db, artists, QStringLiteral("artist"),
                        QStringLiteral("artist_id"), QStringLiteral("name"), track.composer, error);
                const int genreId = intern(db, genres, QStringLiteral("genre"),
                        QStringLiteral("genre_id"), QStringLiteral("name"), track.genre, error);
                const int labelId = intern(db, labels, QStringLiteral("label"),
                        QStringLiteral("label_id"), QStringLiteral("name"), track.label, error);
                const int keyId = intern(db, keys, QStringLiteral("key"),
                        QStringLiteral("key_id"), QStringLiteral("name"), track.musicalKey, error);
                const int colorId = track.colorId > 0 ? track.colorId : 0;

                int albumId = 0;
                if (!track.album.isEmpty()) {
                    const QString albumKey = track.album + QChar(0x1f) + track.artist;
                    const auto it = albums.constFind(albumKey);
                    if (it != albums.constEnd()) {
                        albumId = it.value();
                    } else {
                        albumId = albums.size() + 1;
                        QSqlQuery query(db);
                        query.prepare(QStringLiteral(
                                "INSERT INTO album (album_id, name, artist_id, image_id, "
                                "isComplation, nameForSearch) VALUES (?, ?, ?, 0, 0, ?)"));
                        query.addBindValue(albumId);
                        query.addBindValue(text(track.album));
                        query.addBindValue(artistId);
                        query.addBindValue(searchName(track.album));
                        if (!query.exec()) {
                            ok = false;
                            if (error) {
                                *error = query.lastError().text();
                            }
                        } else {
                            albums.insert(albumKey, albumId);
                        }
                    }
                }

                if (item.artworkId != 0 && !item.artworkPath.isEmpty()) {
                    images.insert(item.artworkId, item.artworkPath);
                }

                QSqlQuery query(db);
                query.prepare(QStringLiteral(R"SQL(
                    INSERT INTO content (
                        content_id, title, titleForSearch, subtitle, bpmx100, length,
                        trackNo, discNo, artist_id_artist, artist_id_remixer,
                        artist_id_originalArtist, artist_id_composer,
                        artist_id_lyricist, album_id, genre_id, label_id, key_id,
                        color_id, image_id, djComment, rating, releaseYear,
                        releaseDate, dateCreated, dateAdded, path, fileName,
                        fileSize, fileType, bitrate, bitDepth, samplingRate, isrc,
                        djPlayCount, isHotCueAutoLoadOn, isKuvoDeliverStatusOn,
                        kuvoDeliveryComment, masterDbId, masterContentId,
                        analysisDataFilePath, analysedBits, contentLink, hasModified,
                        cueUpdateCount, analysisDataUpdateCount, informationUpdateCount)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                            ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,
                            ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                )SQL"));
                query.addBindValue(item.deviceId);
                query.addBindValue(text(track.title.isEmpty()
                        ? QFileInfo(track.filePath).completeBaseName() : track.title));
                query.addBindValue(searchName(track.title));
                query.addBindValue(QString::fromUtf8(""));            // subtitle
                query.addBindValue(qRound(track.bpm * 100.0));
                query.addBindValue(qRound(track.duration));
                query.addBindValue(track.trackNumber);
                query.addBindValue(track.discNumber);
                query.addBindValue(artistId);
                query.addBindValue(0);                                 // remixer
                query.addBindValue(0);                                 // original artist
                query.addBindValue(composerId);
                query.addBindValue(0);                                 // lyricist
                query.addBindValue(albumId);
                query.addBindValue(genreId);
                query.addBindValue(labelId);
                query.addBindValue(keyId);
                query.addBindValue(colorId);
                query.addBindValue(item.artworkId);
                query.addBindValue(text(track.comment));
                query.addBindValue(track.rating);
                query.addBindValue(track.year);
                query.addBindValue(QString::fromUtf8(""));            // releaseDate
                query.addBindValue(createdDate);
                query.addBindValue(createdDate);
                query.addBindValue(item.devicePath);
                query.addBindValue(item.fileName);
                query.addBindValue(item.fileSize);
                query.addBindValue(item.fileType);
                query.addBindValue(track.bitrate);
                query.addBindValue(track.sampleDepth);
                query.addBindValue(track.sampleRate);
                query.addBindValue(text(track.isrc));
                query.addBindValue(0);                                 // djPlayCount
                query.addBindValue(1);                                 // isHotCueAutoLoadOn
                query.addBindValue(1);                                 // isKuvoDeliverStatusOn
                query.addBindValue(QString::fromUtf8(""));            // kuvoDeliveryComment
                query.addBindValue(0);                                 // masterDbId
                query.addBindValue(contentLinkFor(item.devicePath));  // masterContentId
                query.addBindValue(item.analyzePath);
                query.addBindValue(item.peaks.isEmpty() ? 0 : 41);    // analysedBits
                query.addBindValue(contentLinkFor(item.devicePath));  // contentLink
                query.addBindValue(0);                                 // hasModified
                query.addBindValue(item.cues.isEmpty() ? 0 : 1);      // cueUpdateCount
                query.addBindValue(1);                                 // analysisDataUpdateCount
                query.addBindValue(1);                                 // informationUpdateCount
                if (!query.exec()) {
                    ok = false;
                    if (error) {
                        *error = QStringLiteral("content: %1").arg(query.lastError().text());
                    }
                }

                // Cue points, at microsecond precision.
                int cueId = 0;
                for (const core::Cue& cue : item.cues) {
                    ++cueId;
                    QSqlQuery cueQuery(db);
                    cueQuery.prepare(QStringLiteral(R"SQL(
                        INSERT INTO cue (cue_id, content_id, kind, colorTableIndex,
                            cueComment, isActiveLoop, beatLoopNumerator,
                            beatLoopDenominator, inUsec, outUsec,
                            in150FramePerSec, out150FramePerSec)
                        VALUES (?, ?, ?, ?, ?, ?, 0, 0, ?, ?, ?, ?)
                    )SQL"));
                    const qint64 exportedId = static_cast<qint64>(item.deviceId) * 1000 + cueId;
                    if (!library.banks.isEmpty()) cueIds[item.deviceId].append(exportedId);
                    cueQuery.addBindValue(exportedId);
                    cueQuery.addBindValue(item.deviceId);
                    cueQuery.addBindValue(static_cast<int>(cue.kind) + 1);
                    cueQuery.addBindValue(cue.colorId);
                    cueQuery.addBindValue(text(cue.comment.isEmpty() ? cue.name
                                                                     : cue.comment));
                    cueQuery.addBindValue(cue.kind == core::CueKind::Loop ? 1 : 0);
                    cueQuery.addBindValue(qRound(cue.time * 1000000.0));
                    cueQuery.addBindValue(qRound(cue.loopTime * 1000000.0));
                    cueQuery.addBindValue(qRound(cue.time * 150.0));
                    cueQuery.addBindValue(qRound(cue.loopTime * 150.0));
                    if (!cueQuery.exec()) {
                        ok = false;
                        if (error) {
                            *error = cueQuery.lastError().text();
                        }
                    }
                }
            }

            // Fixed lookup tables.
            for (int i = 0; i < 8 && ok; ++i) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO color (color_id, name) VALUES (?, ?)"));
                query.addBindValue(i + 1);
                query.addBindValue(QString::fromLatin1(kColorNames[i]));
                insert(query, ok, error, QStringLiteral("color"));
            }
            for (auto it = images.constBegin(); it != images.constEnd() && ok; ++it) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO image (image_id, path) VALUES (?, ?)"));
                query.addBindValue(it.key());
                query.addBindValue(it.value());
                insert(query, ok, error, QStringLiteral("image"));
            }

            // Playlists and their entries.
            for (const ExportPlaylist& playlist : library.playlists) {
                QSqlQuery node(db);
                node.prepare(QStringLiteral(
                        "INSERT INTO playlist (playlist_id, sequenceNo, name, image_id, "
                        "attribute, playlist_id_parent) VALUES (?, ?, ?, 0, ?, ?)"));
                node.addBindValue(playlist.id);
                node.addBindValue(playlist.sortOrder);
                node.addBindValue(text(playlist.name));
                node.addBindValue(playlist.isFolder ? 1 : 0);
                if (playlist.parentId > 0) {
                    node.addBindValue(playlist.parentId);
                } else {
                    node.addBindValue(QVariant());
                }
                insert(node, ok, error, QStringLiteral("playlist"));
                for (int i = 0; i < playlist.trackIds.size(); ++i) {
                    QSqlQuery entry(db);
                    entry.prepare(QStringLiteral(
                            "INSERT INTO playlist_content (playlist_id, content_id, "
                            "sequenceNo) VALUES (?, ?, ?)"));
                    entry.addBindValue(playlist.id);
                    entry.addBindValue(playlist.trackIds.at(i));
                    entry.addBindValue(i + 1);
                    insert(entry, ok, error, QStringLiteral("playlist_content"));
                }
            }

            for (qsizetype i = 0; ok && i < library.banks.size(); ++i) {
                const auto& bank = library.banks.at(i);
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO hotCueBankList VALUES (?, ?, ?, 0, 0, NULL)"));
                query.addBindValue(i + 1);
                query.addBindValue(i + 1);
                query.addBindValue(bank.name);
                insert(query, ok, error, QStringLiteral("hotCueBankList"));
                QSet<int> positions;
                for (const auto& entry : bank.entries) {
                    if (!ok) break;
                    const auto cues = cueIds.constFind(entry.trackId);
                    if (entry.position < 1 || entry.position > 8 || positions.contains(entry.position)
                            || cues == cueIds.cend() || entry.cueIndex < 0
                            || entry.cueIndex >= cues->size()) {
                        if (error) *error = QStringLiteral("Referencia inválida en el banco «%1»").arg(bank.name);
                        ok = false;
                        break;
                    }
                    positions.insert(entry.position);
                    QSqlQuery slot(db);
                    slot.prepare(QStringLiteral("INSERT INTO hotCueBankList_cue VALUES (?, ?, ?)"));
                    slot.addBindValue(i + 1);
                    slot.addBindValue(cues->at(entry.cueIndex));
                    slot.addBindValue(entry.position);
                    insert(slot, ok, error, QStringLiteral("hotCueBankList_cue"));
                }
            }

            // Browse menus.
            for (const ColumnEntry& column : kColumns) {
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO menuItem (menuItem_id, kind, name) VALUES (?, ?, ?)"));
                query.addBindValue(column.id);
                query.addBindValue(column.kind);
                query.addBindValue(QString::fromUtf8(kMenuWrapperOpen)
                        + QString::fromLatin1(column.name)
                        + QString::fromUtf8(kMenuWrapperClose));
                insert(query, ok, error, QStringLiteral("menuItem"));
            }
            for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i) {
                const MenuEntry& entry = kCategories[i];
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO category (category_id, menuItem_id, sequenceNo, "
                        "isVisible) VALUES (?, ?, ?, ?)"));
                query.addBindValue(i + 1);
                query.addBindValue(entry.itemId);
                query.addBindValue(entry.sequence);
                query.addBindValue(entry.flags & 1);
                insert(query, ok, error, QStringLiteral("category"));
            }
            for (int i = 0; i < static_cast<int>(std::size(kSortMenu)); ++i) {
                const MenuEntry& entry = kSortMenu[i];
                QSqlQuery query(db);
                query.prepare(QStringLiteral(
                        "INSERT INTO sort (sort_id, menuItem_id, sequenceNo, isVisible, "
                        "isSelectedAsSubColumn) VALUES (?, ?, ?, ?, 0)"));
                query.addBindValue(i + 1);
                query.addBindValue(entry.itemId);
                query.addBindValue(entry.sequence);
                query.addBindValue(entry.flags & 1);
                insert(query, ok, error, QStringLiteral("sort"));
            }

            QSqlQuery property(db);
            property.prepare(QStringLiteral(
                    "INSERT INTO property (deviceName, dbVersion, numberOfContents, "
                    "createdDate, backGroundColorType, myTagMasterDBID) "
                    "VALUES (?, '1000', ?, ?, 0, 0)"));
            property.addBindValue(QStringLiteral("Open Deck Export"));
            property.addBindValue(library.tracks.size());
            property.addBindValue(createdDate);
            insert(property, ok, error, QStringLiteral("property"));

            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);

    if (!ok) {
        if (error && error->isEmpty()) {
            *error = QStringLiteral("No se pudieron insertar los datos de OneLibrary");
        }
        QFile::remove(plainPath);
        return false;
    }
    if (!patchReservedBytes(plainPath, error) || !vacuum(plainPath, error)) {
        QFile::remove(plainPath);
        return false;
    }

    if (!SqlCipherWriter::encryptFile(plainPath, databasePath, error)) {
        QFile::remove(plainPath);
        return false;
    }
    QFile::remove(plainPath);
    QFile::remove(plainPath + QStringLiteral("-journal"));
    return true;
}

} // namespace onelibrary
} // namespace device
} // namespace opendeck
