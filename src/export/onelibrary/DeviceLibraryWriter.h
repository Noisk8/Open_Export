#pragma once

#include <QString>

#include "export/ExportModel.h"

namespace opendeck {
namespace device {
namespace onelibrary {

// DeviceLibraryWriter generates `exportLibrary.db`, the OneLibrary / Device
// Library Plus database read by the newer Pioneer players (CDJ-3000X,
// XDJ-AZ, OPUS-QUAD, OMNIS-DUO) and by djay Pro / Traktor.
//
// It is a SQLite database with 22 tables, encrypted as SQLCipher 4. The
// legacy `export.pdb` is still written alongside it, so both generations of
// hardware are covered.
class DeviceLibraryWriter {
  public:
    // Writes the encrypted database at `databasePath`. `library` must already
    // carry the resolved artwork ids/paths (see ExportManager::write).
    [[nodiscard]] static bool write(const QString& databasePath,
                                    const ExportLibrary& library,
                                    const QString& createdDate, QString* error);
};

} // namespace onelibrary
} // namespace device
} // namespace opendeck
