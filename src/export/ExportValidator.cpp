#include "export/ExportValidator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "export/devicesql/DeviceSqlReader.h"
#include "export/onelibrary/SqlCipherWriter.h"

namespace opendeck {
namespace device {

namespace {

// Offsets of the track row, see DeviceSqlWriter.
constexpr int kTrackIdOffset = 0x48;
constexpr int kTrackStringSlots = 0x5e;
constexpr int kTrackFilePathSlot = 20;
constexpr int kTrackAnalyzePathSlot = 14;
constexpr int kTrackTitleSlot = 17;

constexpr int kPlaylistParentOffset = 0x00;
constexpr int kPlaylistIdOffset = 0x0c;
constexpr int kPlaylistIsFolderOffset = 0x10;

constexpr int kEntryIndexOffset = 0x00;
constexpr int kEntryTrackIdOffset = 0x04;
constexpr int kEntryPlaylistIdOffset = 0x08;

quint32 readU32(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

void addIssue(QStringList* issues, const QString& message) {
    if (issues != nullptr) {
        issues->append(message);
    }
}

} // namespace

bool ExportValidator::validateDatabase(const QByteArray& data, QStringList* issues) {
    const DeviceSqlReader reader(data);
    if (!reader.isValid()) {
        addIssue(issues, QStringLiteral("export.pdb: %1").arg(reader.errorString()));
        return false;
    }

    bool ok = true;

    // --- The table directory ------------------------------------------------
    if (reader.tableCount() != 20) {
        addIssue(issues, QStringLiteral("export.pdb: se esperaban 20 tablas y hay %1")
                                 .arg(reader.tableCount()));
        ok = false;
    }
    for (int i = 0; i < reader.tableCount(); ++i) {
        const quint32 type = reader.tableType(i);
        if (type != static_cast<quint32>(i)) {
            addIssue(issues, QStringLiteral("export.pdb: la tabla %1 declara el tipo %2")
                                     .arg(i)
                                     .arg(type));
            ok = false;
        }
    }

    // --- The tracks ---------------------------------------------------------
    const QList<QByteArray> tracks = reader.rows(0);
    QSet<quint32> trackIds;
    for (const QByteArray& row : tracks) {
        const quint32 id = readU32(row, kTrackIdOffset);
        if (id == 0) {
            addIssue(issues, QStringLiteral("export.pdb: hay una pista sin identificador"));
            ok = false;
            continue;
        }
        if (trackIds.contains(id)) {
            addIssue(issues, QStringLiteral("export.pdb: el identificador %1 está repetido").arg(id));
            ok = false;
        }
        trackIds.insert(id);

        const QString title = DeviceSqlReader::readSlotString(row, kTrackStringSlots
                                                              + 2 * kTrackTitleSlot);
        if (title.isEmpty()) {
            addIssue(issues, QStringLiteral("export.pdb: la pista %1 no tiene título").arg(id));
            ok = false;
        }
        const QString path = DeviceSqlReader::readSlotString(row, kTrackStringSlots
                                                             + 2 * kTrackFilePathSlot);
        if (!path.startsWith(QStringLiteral("/Contents/"))) {
            addIssue(issues, QStringLiteral("export.pdb: la pista %1 tiene una ruta inválida \"%2\"")
                                     .arg(id)
                                     .arg(path));
            ok = false;
        }
    }

    // --- The playlist tree --------------------------------------------------
    const QList<QByteArray> nodes = reader.rows(7);
    QSet<quint32> nodeIds;
    for (const QByteArray& row : nodes) {
        nodeIds.insert(readU32(row, kPlaylistIdOffset));
    }
    for (const QByteArray& row : nodes) {
        const quint32 id = readU32(row, kPlaylistIdOffset);
        const quint32 parent = readU32(row, kPlaylistParentOffset);
        if (parent != 0 && !nodeIds.contains(parent)) {
            addIssue(issues, QStringLiteral("export.pdb: la lista %1 apunta a un padre inexistente (%2)")
                                     .arg(id)
                                     .arg(parent));
            ok = false;
        }
        const QString name = DeviceSqlReader::readString(row, 20);
        if (name.isEmpty()) {
            addIssue(issues, QStringLiteral("export.pdb: hay una lista sin nombre"));
            ok = false;
        }
        Q_UNUSED(kPlaylistIsFolderOffset)
    }

    // --- The playlist entries ------------------------------------------------
    const QList<QByteArray> entries = reader.rows(8);
    QHash<quint32, quint32> expectedIndex;
    for (const QByteArray& row : entries) {
        const quint32 index = readU32(row, kEntryIndexOffset);
        const quint32 trackId = readU32(row, kEntryTrackIdOffset);
        const quint32 playlistId = readU32(row, kEntryPlaylistIdOffset);
        if (!trackIds.contains(trackId)) {
            addIssue(issues, QStringLiteral("export.pdb: la entrada %1 apunta a la pista inexistente %2")
                                     .arg(index)
                                     .arg(trackId));
            ok = false;
        }
        if (!nodeIds.contains(playlistId)) {
            addIssue(issues, QStringLiteral("export.pdb: la entrada %1 apunta a la lista inexistente %2")
                                     .arg(index)
                                     .arg(playlistId));
            ok = false;
        }
        const quint32 expected = expectedIndex.value(playlistId, 0) + 1;
        if (index != expected) {
            addIssue(issues, QStringLiteral("export.pdb: la entrada %1 de la lista %2 debería ser la %3")
                                     .arg(index)
                                     .arg(playlistId)
                                     .arg(expected));
            ok = false;
        }
        expectedIndex[playlistId] = index;
    }

    return ok;
}

bool ExportValidator::validateExport(const QString& exportRoot, QStringList* issues) {
    bool ok = true;

    const QString databasePath = exportRoot + QStringLiteral("/PIONEER/rekordbox/export.pdb");
    QFile database(databasePath);
    if (!database.open(QIODevice::ReadOnly)) {
        addIssue(issues, QStringLiteral("No se encontró %1").arg(databasePath));
        return false;
    }
    const QByteArray data = database.readAll();
    database.close();

    ok = validateDatabase(data, issues) && ok;

    // OneLibrary is optional, but when present it must be readable.
    const QString dlpPath = exportRoot + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db");
    if (QFileInfo::exists(dlpPath)) {
        ok = validateDeviceLibrary(dlpPath, issues) && ok;
    }

    // The analysis files of every track have to be where the database says.
    const DeviceSqlReader reader(data);
    if (reader.isValid()) {
        for (const QByteArray& row : reader.rows(0)) {
            const QString analyzePath =
                    DeviceSqlReader::readSlotString(row, kTrackStringSlots
                                                           + 2 * kTrackAnalyzePathSlot);
            if (analyzePath.isEmpty()) {
                continue;
            }
            const QString path = exportRoot + analyzePath;
            if (!QFileInfo::exists(path)) {
                addIssue(issues, QStringLiteral("Falta el fichero de análisis %1").arg(analyzePath));
                ok = false;
            }
            const QString audioPath =
                    DeviceSqlReader::readSlotString(row, kTrackStringSlots
                                                           + 2 * kTrackFilePathSlot);
            if (!audioPath.isEmpty() && !QFileInfo::exists(exportRoot + audioPath)) {
                addIssue(issues, QStringLiteral("Falta el audio %1").arg(audioPath));
                ok = false;
            }
        }
    }

    return ok;
}

bool ExportValidator::validateDeviceLibrary(const QString& databasePath, QStringList* issues) {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        addIssue(issues, QStringLiteral("No se pudo crear un directorio temporal"));
        return false;
    }
    const QString plainPath = dir.path() + QStringLiteral("/exportLibrary.db");
    QString error;
    if (!onelibrary::SqlCipherWriter::decryptFile(databasePath, plainPath, &error)) {
        addIssue(issues, QStringLiteral("exportLibrary.db: %1").arg(error));
        return false;
    }

    bool ok = true;
    const QString connection =
            QStringLiteral("ode_validate_dlp_%1")
                    .arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(plainPath);
        if (!db.open()) {
            addIssue(issues, QStringLiteral("exportLibrary.db: %1")
                                     .arg(db.lastError().text()));
            ok = false;
        } else {
            QSqlQuery query(db);
            const QStringList tables = {
                    QStringLiteral("content"), QStringLiteral("playlist"),
                    QStringLiteral("playlist_content"), QStringLiteral("cue"),
                    QStringLiteral("menuItem"), QStringLiteral("category"),
                    QStringLiteral("sort"), QStringLiteral("property")};
            for (const QString& table : tables) {
                if (!query.exec(QStringLiteral("SELECT count(*) FROM %1").arg(table))) {
                    addIssue(issues, QStringLiteral("exportLibrary.db: falta la tabla %1")
                                             .arg(table));
                    ok = false;
                }
            }
            if (query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next()
                    && query.value(0).toString() != QStringLiteral("ok")) {
                addIssue(issues, QStringLiteral("exportLibrary.db: integrity_check falló"));
                ok = false;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

} // namespace device
} // namespace opendeck
