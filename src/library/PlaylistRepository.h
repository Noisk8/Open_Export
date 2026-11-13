#pragma once

#include <QList>

#include "core/models/playlist.h"

namespace opendeck {
namespace library {

class DatabaseManager;

// PlaylistRepository is the data-access layer for the "playlists" and
// "playlist_entries" tables.
//
// The hierarchy is stored with a self-referencing `parent_id`, exactly like
// the `playlist_tree` table of a DeviceSQL export: a row with `parent_id = 0`
// lives at the root of the menu.
class PlaylistRepository {
  public:
    explicit PlaylistRepository(DatabaseManager& db);

    // Returns every playlist, folders included, ordered by sort order, with
    // their entries loaded.
    QList<core::Playlist> allPlaylists();

    // Returns only the playlists that contain tracks.
    QList<core::Playlist> allPlaylistsWithEntries();

    // Creates or updates a playlist and replaces its entries.
    qint64 savePlaylist(const core::Playlist& playlist);

    // Appends a track to a playlist at the given position.
    bool addTrack(qint64 playlistId, qint64 trackId, int position);

    // Removes a playlist and everything below it.
    bool removePlaylist(qint64 playlistId);

  private:
    DatabaseManager& m_db;
};

} // namespace library
} // namespace opendeck
