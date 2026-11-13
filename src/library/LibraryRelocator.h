#pragma once

#include <QString>
#include <QStringList>

namespace opendeck {
namespace library {

class DatabaseManager;

struct RelocationReport {
    int checked = 0;    // tracks whose stored path no longer exists
    int relocated = 0;  // tracks that were found again
    QStringList relocatedTitles;

    [[nodiscard]] QString summary() const;
};

// LibraryRelocator repairs tracks whose audio file moved to another mount
// point. This happens constantly with removable media: a USB stick formatted
// or relabelled is mounted as /run/media/<user>/MIUSB instead of the UUID that
// was stored in library.db.
//
// It never guesses by title alone. For each missing track it tries, in order:
//   1. the same path suffix under every mounted volume (1..5 components),
//   2. the original file name inside <volume>/Contents/ (a previous export),
//   3. the exact SHA-256 of the file (only for small volumes), because the
//      exporter copies audio byte for byte.
class LibraryRelocator {
  public:
    // `searchRoots` overrides the mounted-volume scan; used by the tests (and
    // by callers that know exactly where to look).
    [[nodiscard]] static RelocationReport relocate(DatabaseManager& db, QString* error,
                                                   const QStringList& searchRoots = {});
};

} // namespace library
} // namespace opendeck
