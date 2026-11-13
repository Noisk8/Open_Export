#include "library/PlaylistRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "library/DatabaseManager.h"

namespace opendeck {
namespace library {

namespace {
// A default constructed QString is null, and the SQLite driver binds null
// strings as SQL NULL. Every text value is normalised to an empty string.
QString text(const QString& value) {
    return value.isNull() ? QString::fromUtf8("") : value;
}
} // namespace

PlaylistRepository::PlaylistRepository(DatabaseManager& db) : m_db(db) {
}

QList<core::Playlist> PlaylistRepository::allPlaylists() {
    QList<core::Playlist> playlists;
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
            "SELECT id, name, parent_id, sort_order, is_folder FROM playlists "
            "ORDER BY sort_order, id"));
    if (!q.exec()) {
        return playlists;
    }
    while (q.next()) {
        core::Playlist playlist;
        playlist.id = q.value(0).toLongLong();
        playlist.name = q.value(1).toString();
        playlist.parentId = q.value(2).toLongLong();
        playlist.sortOrder = q.value(3).toInt();
        playlist.isFolder = q.value(4).toInt() != 0;
        playlists.append(playlist);
    }

    // Load the entries of every playlist in a single pass.
    QSqlQuery entries(m_db.database());
    entries.prepare(QStringLiteral(
            "SELECT playlist_id, track_id FROM playlist_entries ORDER BY playlist_id, position"));
    if (!entries.exec()) {
        return playlists;
    }
    int current = 0;
    qint64 currentPlaylist = -1;
    while (entries.next()) {
        const qint64 playlistId = entries.value(0).toLongLong();
        const qint64 trackId = entries.value(1).toLongLong();
        if (playlistId != currentPlaylist) {
            currentPlaylist = playlistId;
            while (current < playlists.size() && playlists.at(current).id != playlistId) {
                ++current;
            }
        }
        if (current < playlists.size() && playlists.at(current).id == playlistId) {
            playlists[current].trackIds.append(trackId);
        }
    }
    return playlists;
}

QList<core::Playlist> PlaylistRepository::allPlaylistsWithEntries() {
    QList<core::Playlist> playlists;
    for (const core::Playlist& playlist : allPlaylists()) {
        if (!playlist.trackIds.isEmpty()) {
            playlists.append(playlist);
        }
    }
    return playlists;
}

qint64 PlaylistRepository::savePlaylist(const core::Playlist& playlist) {
    qint64 id = playlist.id;
    if (id <= 0) {
        QSqlQuery ins(m_db.database());
        ins.prepare(QStringLiteral(
                "INSERT INTO playlists (name, parent_id, sort_order, is_folder) "
                "VALUES (:name, :parent, :sort, :folder)"));
        ins.bindValue(QStringLiteral(":name"), text(playlist.name));
        // A parent of zero means "at the root of the menu", which the foreign
        // key expresses as NULL.
        if (playlist.parentId > 0) {
            ins.bindValue(QStringLiteral(":parent"), playlist.parentId);
        } else {
            ins.bindValue(QStringLiteral(":parent"), QVariant());
        }
        ins.bindValue(QStringLiteral(":sort"), playlist.sortOrder);
        ins.bindValue(QStringLiteral(":folder"), playlist.isFolder ? 1 : 0);
        if (!ins.exec()) {
            return 0;
        }
        id = ins.lastInsertId().toLongLong();
    } else {
        QSqlQuery upd(m_db.database());
        upd.prepare(QStringLiteral(
                "UPDATE playlists SET name = :name, parent_id = :parent, "
                "sort_order = :sort, is_folder = :folder WHERE id = :id"));
        upd.bindValue(QStringLiteral(":name"), text(playlist.name));
        if (playlist.parentId > 0) {
            upd.bindValue(QStringLiteral(":parent"), playlist.parentId);
        } else {
            upd.bindValue(QStringLiteral(":parent"), QVariant());
        }
        upd.bindValue(QStringLiteral(":sort"), playlist.sortOrder);
        upd.bindValue(QStringLiteral(":folder"), playlist.isFolder ? 1 : 0);
        upd.bindValue(QStringLiteral(":id"), id);
        if (!upd.exec()) {
            return 0;
        }
    }

    QSqlQuery del(m_db.database());
    del.prepare(QStringLiteral("DELETE FROM playlist_entries WHERE playlist_id = :p"));
    del.bindValue(QStringLiteral(":p"), id);
    if (!del.exec()) {
        return 0;
    }

    QSqlQuery ins(m_db.database());
    ins.prepare(QStringLiteral(
            "INSERT INTO playlist_entries (playlist_id, track_id, position) "
            "VALUES (:p, :t, :pos)"));
    for (int i = 0; i < playlist.trackIds.size(); ++i) {
        ins.bindValue(QStringLiteral(":p"), id);
        ins.bindValue(QStringLiteral(":t"), playlist.trackIds.at(i));
        ins.bindValue(QStringLiteral(":pos"), i);
        if (!ins.exec()) {
            return 0;
        }
    }
    return id;
}

bool PlaylistRepository::addTrack(qint64 playlistId, qint64 trackId, int position) {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral(
            "INSERT INTO playlist_entries (playlist_id, track_id, position) "
            "VALUES (:p, :t, :pos)"));
    q.bindValue(QStringLiteral(":p"), playlistId);
    q.bindValue(QStringLiteral(":t"), trackId);
    q.bindValue(QStringLiteral(":pos"), position);
    return q.exec();
}

bool PlaylistRepository::removePlaylist(qint64 playlistId) {
    QSqlQuery q(m_db.database());
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), playlistId);
    return q.exec();
}

} // namespace library
} // namespace opendeck
