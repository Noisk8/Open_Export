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
#include <QtEndian>
#include <algorithm>

#include "export/anlz/AnlzWriter.h"
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

using Rows = QList<QStringList>;

bool compareLibrary(QSqlDatabase db, const DeviceSqlReader& reader) {
    const auto matches = [&](const QString& sql, Rows expected) {
        QSqlQuery query(db);
        if (!query.exec(sql)) return false;
        Rows actual;
        const qsizetype columns = expected.isEmpty() ? 0 : expected.first().size();
        while (query.next()) {
            QStringList row;
            for (int i = 0; i < columns; ++i)
                row.append(query.value(i).toString());
            actual.append(row);
        }
        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());
        return expected == actual;
    };
    const auto number = [](const QByteArray& row, int offset) {
        return QString::number(readU32(row, offset));
    };
    Rows tracks;
    for (const auto& row : reader.rows(0)) {
        tracks.append({number(row, kTrackIdOffset),
            DeviceSqlReader::readSlotString(row, kTrackStringSlots + 2 * kTrackFilePathSlot),
            DeviceSqlReader::readSlotString(row, kTrackStringSlots + 2 * kTrackAnalyzePathSlot)});
    }
    Rows playlists;
    for (const auto& row : reader.rows(7)) {
        playlists.append({number(row, kPlaylistIdOffset), number(row, kPlaylistParentOffset),
            number(row, 8), number(row, kPlaylistIsFolderOffset),
            DeviceSqlReader::readString(row, 20)});
    }
    Rows entries;
    for (const auto& row : reader.rows(8)) {
        entries.append({number(row, kEntryPlaylistIdOffset), number(row, kEntryTrackIdOffset),
            number(row, kEntryIndexOffset)});
    }
    return matches(QStringLiteral("SELECT content_id, path, analysisDataFilePath FROM content"), tracks)
            && matches(QStringLiteral("SELECT playlist_id, coalesce(playlist_id_parent,0), "
                                      "sequenceNo, attribute, name FROM playlist"), playlists)
            && matches(QStringLiteral("SELECT playlist_id, content_id, sequenceNo FROM playlist_content"),
                       entries);
}

bool validateDeviceLibrary(const QString& databasePath, QStringList* issues,
                           const DeviceSqlReader* legacy);

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

bool ExportValidator::validateExport(const QString& exportRoot, QStringList* issues,
                                     const QString& mediaFallback, bool expectAudio,
                                     bool expectAnalysis) {
    QFile database(exportRoot + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
    if (!database.open(QIODevice::ReadOnly)) {
        addIssue(issues, QStringLiteral("No se encontró %1").arg(database.fileName()));
        return false;
    }
    const QByteArray data = database.readAll();
    bool ok = validateDatabase(data, issues);

    const QString dlpPath = exportRoot + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db");
    const DeviceSqlReader reader(data);
    if (!reader.isValid()) return false;
    if (QFileInfo::exists(dlpPath))
        ok = opendeck::device::validateDeviceLibrary(dlpPath, issues, &reader) && ok;

    const auto media = [&](const QString& path) {
        if (path != QDir::cleanPath(path) || !path.startsWith('/')) return QString();
        const QString staged = exportRoot + path;
        return !mediaFallback.isEmpty() && !QFileInfo::exists(staged)
                ? mediaFallback + path : staged;
    };

    QSet<QString> audioPaths;
    QSet<QString> analysisPaths;
    for (const QByteArray& row : reader.rows(0)) {
        const QString audioPath = DeviceSqlReader::readSlotString(
                row, kTrackStringSlots + 2 * kTrackFilePathSlot);
        const QString key = audioPath.toCaseFolded();
        if (expectAudio && (!audioPath.startsWith("/Contents/") || audioPaths.contains(key)
                || !QFileInfo(media(audioPath)).isFile() || QFileInfo(media(audioPath)).size() == 0)) {
            addIssue(issues, QStringLiteral("Audio ausente, vacío o duplicado: %1").arg(audioPath));
            ok = false;
        }
        audioPaths.insert(key);
        if (!expectAnalysis) continue;
        const QString analyzePath = DeviceSqlReader::readSlotString(
                row, kTrackStringSlots + 2 * kTrackAnalyzePathSlot);
        if (analyzePath != AnlzWriter::analysisPath(audioPath) || analysisPaths.contains(analyzePath)) {
            addIssue(issues, QStringLiteral("Ruta de análisis inválida: %1").arg(analyzePath));
            ok = false;
            continue;
        }
        analysisPaths.insert(analyzePath);
        for (const QString& suffix : {QStringLiteral(".DAT"), QStringLiteral(".EXT"),
                                      QStringLiteral(".2EX")}) {
            const QString path = QFileInfo(analyzePath).path() + "/ANLZ0000" + suffix;
            QFile file(media(path));
            bool valid = file.open(QIODevice::ReadOnly);
            const QByteArray header = valid ? file.read(12) : QByteArray();
            const auto sizeAt = [](const QByteArray& bytes, int offset) {
                return qFromBigEndian<quint32>(bytes.constData() + offset);
            };
            valid = header.size() == 12 && header.startsWith("PMAI")
                    && sizeAt(header, 8) == file.size() && sizeAt(header, 4) >= 12
                    && sizeAt(header, 4) <= file.size() && file.seek(sizeAt(header, 4));
            bool pathMatched = false;
            while (valid && !file.atEnd()) {
                const auto section = file.read(12);
                const qint64 start = file.pos() - section.size();
                valid = section.size() == 12 && sizeAt(section, 4) >= 12
                        && sizeAt(section, 8) >= sizeAt(section, 4)
                        && sizeAt(section, 8) <= file.size() - start;
                if (valid && section.startsWith("PPTH")) {
                    QByteArray expected(2 * (audioPath.size() + 1), char(0));
                    for (qsizetype i = 0; i < audioPath.size(); ++i)
                        qToBigEndian<quint16>(audioPath.at(i).unicode(), expected.data() + 2 * i);
                    const QByteArray length = file.read(4);
                    valid = !pathMatched && length.size() == 4 && sizeAt(length, 0) == expected.size()
                            && sizeAt(section, 8) >= 16 + expected.size()
                            && file.read(expected.size()) == expected;
                    pathMatched = valid;
                }
                if (valid) valid = file.seek(start + sizeAt(section, 8));
            }
            if (!valid || !pathMatched) {
                addIssue(issues, QStringLiteral("Análisis ausente o inválido: %1").arg(path));
                ok = false;
            }
        }
    }
    return ok;
}

bool ExportValidator::validateDeviceLibrary(const QString& databasePath, QStringList* issues) {
    return opendeck::device::validateDeviceLibrary(databasePath, issues, nullptr);
}

namespace {

bool validateDeviceLibrary(const QString& databasePath, QStringList* issues,
                           const DeviceSqlReader* legacy) {
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
            if (!query.exec(QStringLiteral("PRAGMA integrity_check")) || !query.next()
                    || query.value(0).toString() != QStringLiteral("ok")) {
                addIssue(issues, QStringLiteral("exportLibrary.db: integrity_check falló"));
                ok = false;
            }
            const QStringList referenceChecks{
                QStringLiteral("SELECT 1 FROM playlist_content pc LEFT JOIN playlist p "
                               "ON p.playlist_id=pc.playlist_id LEFT JOIN content c "
                               "ON c.content_id=pc.content_id WHERE p.playlist_id IS NULL "
                               "OR c.content_id IS NULL LIMIT 1"),
                QStringLiteral("SELECT 1 FROM cue LEFT JOIN content USING(content_id) "
                               "WHERE content.content_id IS NULL LIMIT 1"),
                QStringLiteral("SELECT 1 FROM playlist p LEFT JOIN playlist parent "
                               "ON p.playlist_id_parent=parent.playlist_id "
                               "WHERE p.playlist_id_parent > 0 AND parent.playlist_id IS NULL LIMIT 1")};
            for (const QString& check : referenceChecks) {
                if (!query.exec(check) || query.next()) {
                    addIssue(issues, QStringLiteral("exportLibrary.db: referencias inválidas"));
                    ok = false;
                }
            }
            if (legacy && !compareLibrary(db, *legacy)) {
                addIssue(issues, QStringLiteral("Device Library y OneLibrary contienen sets distintos"));
                ok = false;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return ok;
}

} // namespace

} // namespace device
} // namespace opendeck
