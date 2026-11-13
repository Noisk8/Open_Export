#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace opendeck {
namespace device {

// ExportValidator checks an export without a Pioneer player.
//
// It reads the generated files the way a player would and reports everything
// that does not add up. A clean report does not prove that the export works
// on hardware; it only proves that the structure is consistent.
class ExportValidator {
  public:
    // Validates the `export.pdb` of an export. Returns true when no problem
    // was found; the issues are appended to the list either way.
    static bool validateExport(const QString& exportRoot, QStringList* issues);

    // Validates the content of an `export.pdb`.
    static bool validateDatabase(const QByteArray& data, QStringList* issues);

    // Decrypts an `exportLibrary.db` (OneLibrary / SQLCipher 4) and checks its
    // schema and row counts with an independent SQLite connection.
    static bool validateDeviceLibrary(const QString& databasePath, QStringList* issues);
};

} // namespace device
} // namespace opendeck
