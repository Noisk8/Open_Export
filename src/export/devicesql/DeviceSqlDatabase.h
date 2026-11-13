#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>

#include "export/devicesql/DeviceSqlPage.h"

namespace opendeck {
namespace device {

// The table types of an `export.pdb` file. The numbering is fixed by the
// format; every table from 0 to 19 has to be present in the file header even
// when it holds no rows.
namespace table {
constexpr quint32 kTracks = 0;
constexpr quint32 kGenres = 1;
constexpr quint32 kArtists = 2;
constexpr quint32 kAlbums = 3;
constexpr quint32 kLabels = 4;
constexpr quint32 kKeys = 5;
constexpr quint32 kColors = 6;
constexpr quint32 kPlaylistTree = 7;
constexpr quint32 kPlaylistEntries = 8;
constexpr quint32 kHistoryPlaylists = 11;
constexpr quint32 kHistoryEntries = 12;
constexpr quint32 kArtwork = 13;
constexpr quint32 kColumns = 16;
constexpr quint32 kCategories = 17;
constexpr quint32 kSortMenu = 18;
constexpr quint32 kHistory = 19;

constexpr int kCount = 20;
} // namespace table

// DeviceSqlDatabase assembles the pages of a DeviceSQL file.
//
// Every table starts with a sentinel (index) page, and continues with as many
// data pages as needed. The file header holds a directory of the twenty table
// types that a player walks through when it resolves a row.
class DeviceSqlDatabase {
  public:
    DeviceSqlDatabase();

    // Creates the table of the given type. Tables are created in ascending
    // type order, which is what rekordbox does.
    void createTable(quint32 type);

    // Appends a row to a table, allocating a new data page when needed.
    void appendRow(quint32 type, const QByteArray& row);

    // Number of rows stored in a table.
    [[nodiscard]] int rowCount(quint32 type) const;

    // Renders the whole file.
    [[nodiscard]] QByteArray serialize() const;

  private:
    struct Table {
        quint32 type = 0;
        quint32 sentinelPage = 0;
        quint32 lastPage = 0;
        QList<QByteArray> rows;
    };

    [[nodiscard]] Table* tableFor(quint32 type);
    [[nodiscard]] const Table* tableFor(quint32 type) const;

    QList<Table> m_tables;
    QHash<quint32, int> m_tableIndex;
    QList<QByteArray> m_pages; // index == page number; 0 is the file header
    quint32 m_nextPage = 1;
};

} // namespace device
} // namespace opendeck
