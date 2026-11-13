#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "export/ExportModel.h"
#include "export/devicesql/DeviceSqlDatabase.h"

namespace opendeck {
namespace device {

// ResolvedTrack is a track whose references to the interned tables (artist,
// album, genre, label and key) have already been turned into row ids.
struct ResolvedTrack {
    ExportTrack item;
    quint32 artistId = 0;
    quint32 albumId = 0;
    quint32 genreId = 0;
    quint32 labelId = 0;
    quint32 keyId = 0;
};

// DeviceSqlWriter renders an ExportLibrary as a DeviceSQL database, the
// `export.pdb` file that a Pioneer player reads to browse tracks and
// playlists.
//
// The layout follows the reverse engineered format documented in
// docs/rekordbox-export.md:
//
//   * twenty tables, all of them present in the file header, even the empty
//     ones, so that the player can walk the directory without surprises;
//   * every table starts with a sentinel page and continues with data pages;
//   * the small tables (genres, artists, albums, labels, keys, colours) hold
//     the values that the track rows refer to by id;
//   * the playlist tree holds the menu structure and the playlist entries
//     hold the ordered track references.
//
// See <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/>.
class DeviceSqlWriter {
  public:
    void setLibrary(const ExportLibrary& library);
    void setCreationDate(const QString& isoDate);

    // Renders the whole `export.pdb`.
    [[nodiscard]] QByteArray build() const;

  private:
    [[nodiscard]] QByteArray buildTrackRow(const ResolvedTrack& track) const;
    [[nodiscard]] QByteArray buildPlaylistRow(const ExportPlaylist& playlist) const;
    [[nodiscard]] QByteArray buildPlaylistEntryRow(quint32 entryIndex, quint32 trackId,
                                                   quint32 playlistId) const;
    [[nodiscard]] QByteArray buildGenreRow(quint32 id, const QString& name) const;
    [[nodiscard]] QByteArray buildLabelRow(quint32 id, const QString& name) const;
    [[nodiscard]] QByteArray buildKeyRow(quint32 id, const QString& name) const;
    [[nodiscard]] QByteArray buildArtistRow(quint32 id, const QString& name) const;
    [[nodiscard]] QByteArray buildAlbumRow(quint32 id, quint32 artistId,
                                           const QString& name) const;
    [[nodiscard]] QByteArray buildColorRow(quint32 id, const QString& name) const;
    [[nodiscard]] QByteArray buildArtworkRow(quint32 id, const QString& path) const;
    [[nodiscard]] QByteArray buildColumnRow(quint16 id, quint16 kind,
                                             const QString& name) const;
    [[nodiscard]] QByteArray buildCategoryRow(quint16 itemId, quint16 sequence,
                                               quint32 flags) const;
    [[nodiscard]] QByteArray buildSortRow(quint16 itemId, quint16 sequence,
                                           quint32 visible) const;
    [[nodiscard]] QByteArray buildHistoryRow(quint32 trackCount,
                                              const QString& date) const;

    ExportLibrary m_library;
    QString m_creationDate;
};

} // namespace device
} // namespace opendeck
