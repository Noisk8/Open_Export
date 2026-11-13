#pragma once

#include <QList>
#include <QString>

namespace opendeck {
namespace core {

// Playlist is a named, ordered list of tracks.
//
// The local library allows playlists to be nested: a playlist whose
// `parentId` is non-zero lives inside a folder. The DeviceSQL export mirrors
// this with the `playlist_tree` table (type 7), where every node carries a
// `parent_id` and a `raw_is_folder` flag, and the `playlist_entries` table
// (type 8) carries the ordered track references.
struct Playlist {
    qint64 id = 0;
    QString name;
    qint64 parentId = 0; // 0 = top level
    int sortOrder = 0;
    bool isFolder = false;
    QList<qint64> trackIds; // Ordered; repeated tracks are allowed.
};

} // namespace core
} // namespace opendeck
