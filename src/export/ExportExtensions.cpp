#include "export/ExportExtensions.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>
#include <stdexcept>

#include "export/devicesql/DeviceSqlDatabase.h"
#include "export/devicesql/DeviceSqlReader.h"
#include "export/devicesql/DeviceSqlRow.h"

namespace opendeck::device {
namespace {

constexpr quint32 kTags = 3;
constexpr quint32 kTagTracks = 4;
constexpr int kExtensionTables = 9;

[[noreturn]] void invalid(const QString& message) {
    throw std::runtime_error(message.toStdString());
}

QString nameOf(const QJsonValue& value) {
    const QString name = value.toString().trimmed();
    if (name.isEmpty() || name.size() > 80 || name.contains(QChar(0)))
        invalid(QStringLiteral("Cada nombre de etiqueta, categoría o banco debe tener de 1 a 80 caracteres"));
    return name;
}

QJsonArray arrayOf(const QJsonObject& object, const QString& key, bool optional = false) {
    const auto value = object.value(key);
    if (optional && value.isUndefined()) return {};
    if (!value.isArray()) invalid(QStringLiteral("Se esperaba una lista JSON en «%1»").arg(key));
    return value.toArray();
}

} // namespace

ExtensionSettings ExtensionSettings::read(const QString& path, const QList<core::Track>& tracks) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        invalid(QStringLiteral("No se pudo leer el archivo de extensión JSON (máximo 1 MiB)"));
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) invalid(QStringLiteral("Configuración JSON inválida: %1").arg(parseError.errorString()));
    const auto root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        if (it.key() != "myTags" && it.key() != "hotCueBanks")
            invalid(QStringLiteral("Opción de extensión desconocida: %1").arg(it.key()));
    }

    QHash<QString, qint64> ids;
    for (const auto& track : tracks) ids.insert(QDir::cleanPath(track.filePath), track.id);
    const auto trackId = [&](const QJsonValue& value) {
        const QString path = value.toString();
        const auto id = ids.value(QDir::cleanPath(path));
        if (!QDir::isAbsolutePath(path) || id <= 0)
            invalid(QStringLiteral("La pista debe estar importada y tener una ruta absoluta: %1").arg(path));
        return id;
    };

    ExtensionSettings settings;
    QSet<QPair<QString, QString>> tagNames;
    for (const auto& value : arrayOf(root, "myTags", true)) {
        const auto object = value.toObject();
        Tag tag{nameOf(object.value("category")), nameOf(object.value("name")), {}};
        const auto key = qMakePair(tag.category, tag.name);
        if (tagNames.contains(key)) invalid(QStringLiteral("Etiqueta repetida: %1").arg(tag.name));
        tagNames.insert(key);
        QSet<qint64> seen;
        for (const auto& track : arrayOf(object, "tracks")) {
            const auto id = trackId(track);
            if (!seen.contains(id)) tag.trackIds.append(id);
            seen.insert(id);
        }
        if (tag.trackIds.isEmpty()) invalid(QStringLiteral("Etiqueta sin pistas: %1").arg(tag.name));
        settings.tags.append(tag);
    }

    QSet<QString> bankNames;
    for (const auto& value : arrayOf(root, "hotCueBanks", true)) {
        const auto object = value.toObject();
        Bank bank{nameOf(object.value("name")), {}};
        if (bankNames.contains(bank.name)) invalid(QStringLiteral("Banco repetido: %1").arg(bank.name));
        bankNames.insert(bank.name);
        QSet<int> positions;
        for (const auto& item : arrayOf(object, "slots")) {
            const auto slot = item.toObject();
            const int position = slot.value("slot").toInt();
            const int cue = slot.value("cue").toInt();
            const int loop = slot.value("loop").toInt();
            const bool hasCue = slot.contains("cue");
            const bool hasLoop = slot.contains("loop");
            if (position < 1 || position > 8 || positions.contains(position) || hasCue == hasLoop
                    || (hasCue && (cue < 1 || cue > 8)) || (hasLoop && loop < 1))
                invalid(QStringLiteral("Posición, hot cue o loop inválido en el banco «%1»").arg(bank.name));
            positions.insert(position);
            bank.slotsByPosition.append({position, trackId(slot.value("track")), cue, loop});
        }
        if (bank.slotsByPosition.isEmpty()) invalid(QStringLiteral("Banco sin posiciones: %1").arg(bank.name));
        settings.banks.append(bank);
    }
    if (settings.tags.isEmpty() && settings.banks.isEmpty())
        invalid(QStringLiteral("El archivo debe definir al menos una etiqueta o un banco"));
    return settings;
}

QByteArray buildExportExt(const QList<ExportTag>& tags) {
    DeviceSqlDatabase database;
    for (int type = 0; type < kExtensionTables; ++type) database.createTable(type);
    QHash<QString, QPair<quint32, quint32>> categories;
    quint32 nextTagId = 0x10000;
    DeviceSqlDataPage tagPage(kTags);
    const auto append = [&](const QString& name, quint32 parent, quint32 position, quint32 id) {
        if (name.isEmpty() || name.size() > 80)
            invalid(QStringLiteral("Nombre inválido para exportExt.pdb"));
        DeviceSqlRow row(31);
        row.setU16(0, 0x0680);
        row.setU32(12, parent);
        row.setU32(16, position);
        row.setU32(20, id);
        row.setU8(27, parent == 0 ? 1 : 0);
        row.setU8(28, 3);
        const auto nameOffset = row.appendString(DeviceSqlString::text(name));
        const auto empty = row.appendString(DeviceSqlString::text(QString()));
        row.setU8(29, nameOffset);
        row.setU8(30, empty);
        if (!tagPage.canFit(row.bytes())) tagPage = DeviceSqlDataPage(kTags);
        row.setU16(2, tagPage.rowCount() * 0x20);
        tagPage.appendRow(row.bytes());
        database.appendRow(kTags, row.bytes());
    };
    for (const auto& tag : tags) {
        auto category = categories.find(tag.category);
        if (category == categories.end()) {
            const quint32 position = categories.size();
            const quint32 id = position + 1;
            append(tag.category, 0, position, id);
            category = categories.insert(tag.category, {id, 0});
        }
        const quint32 id = nextTagId++;
        append(tag.name, category->first, category->second++, id);
        for (quint32 track : tag.trackIds) {
            DeviceSqlRow row(16);
            row.setU32(4, track);
            row.setU32(8, id);
            row.setU32(12, 3);
            database.appendRow(kTagTracks, row.bytes());
        }
    }
    return database.serialize();
}

bool validateExportExt(const QByteArray& bytes, const ExportLibrary& expected) {
    const DeviceSqlReader reader(bytes);
    if (!reader.isValid() || reader.tableCount() != kExtensionTables) return false;
    for (int i = 0; i < kExtensionTables; ++i)
        if (reader.tableType(i) != static_cast<quint32>(i)) return false;

    QHash<quint32, QString> categories;
    QHash<quint32, QPair<quint32, QString>> tags;
    for (const auto& row : reader.rows(kTags)) {
        if (row.size() < 32 || qFromLittleEndian<quint16>(row.constData()) != 0x0680) return false;
        const auto id = qFromLittleEndian<quint32>(row.constData() + 20);
        const auto parent = qFromLittleEndian<quint32>(row.constData() + 12);
        const auto name = DeviceSqlReader::readString(row, static_cast<quint8>(row.at(29)));
        if (!id || name.isEmpty() || categories.contains(id) || tags.contains(id)
                || (row.at(27) != 0) != (parent == 0)) return false;
        if (parent == 0) categories.insert(id, name);
        else tags.insert(id, {parent, name});
    }

    QHash<QPair<QString, QString>, QSet<quint32>> wanted;
    QSet<QString> wantedCategories;
    for (const auto& tag : expected.tags) {
        wantedCategories.insert(tag.category);
        wanted.insert({tag.category, tag.name}, QSet<quint32>(tag.trackIds.begin(), tag.trackIds.end()));
    }
    const auto categoryNames = categories.values();
    if (categories.size() != wantedCategories.size()
            || QSet<QString>(categoryNames.begin(), categoryNames.end()) != wantedCategories
            || tags.size() != wanted.size()) return false;

    QSet<quint32> tracks;
    for (const auto& track : expected.tracks) tracks.insert(track.deviceId);
    for (const auto& row : reader.rows(kTagTracks)) {
        if (row.size() < 16) return false;
        const auto track = qFromLittleEndian<quint32>(row.constData() + 4);
        const auto tagId = qFromLittleEndian<quint32>(row.constData() + 8);
        if (!tracks.contains(track) || !tags.contains(tagId)) return false;
        const auto tag = tags.value(tagId);
        const auto key = qMakePair(categories.value(tag.first), tag.second);
        if (!wanted.contains(key) || !wanted[key].remove(track)) return false;
    }
    for (const auto& remaining : wanted)
        if (!remaining.isEmpty()) return false;
    return true;
}

} // namespace opendeck::device
