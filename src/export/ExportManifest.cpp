#include "export/ExportManifest.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace opendeck {
namespace device {

namespace {

QByteArray digest(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

void addText(QCryptographicHash& hash, const QString& value) {
    hash.addData(value.toUtf8());
    hash.addData(QByteArrayView("\x1f", 1));
}

void addNumber(QCryptographicHash& hash, qint64 value) {
    hash.addData(QByteArray::number(value));
    hash.addData(QByteArrayView("\x1f", 1));
}

} // namespace

QString ExportManifest::pathIn(const QString& exportRoot) {
    return exportRoot + QStringLiteral("/PIONEER/rekordbox/") + QString::fromLatin1(kFileName);
}

QString ExportManifest::trackHash(const ExportTrack& item) {
    const core::Track& track = item.track;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addText(hash, item.devicePath);
    addText(hash, item.analyzePath);
    addText(hash, item.artworkPath);
    addText(hash, track.fileHash);
    addNumber(hash, item.fileSize);
    addNumber(hash, item.fileType);
    addNumber(hash, item.artworkId);
    addText(hash, track.title);
    addText(hash, track.artist);
    addText(hash, track.album);
    addText(hash, track.genre);
    addText(hash, track.label);
    addText(hash, track.comment);
    addText(hash, track.isrc);
    addText(hash, track.mixName);
    addText(hash, track.musicalKey);
    addNumber(hash, track.year);
    addNumber(hash, track.trackNumber);
    addNumber(hash, track.discNumber);
    addNumber(hash, track.rating);
    addNumber(hash, track.colorId);
    addNumber(hash, track.sampleRate);
    addNumber(hash, track.sampleDepth);
    addNumber(hash, track.bitrate);
    addNumber(hash, qRound(track.duration * 1000.0));
    addNumber(hash, qRound(track.bpm * 100.0));
    addNumber(hash, item.beats.size());
    for (const core::Beat& beat : item.beats) {
        addNumber(hash, beat.number);
        addNumber(hash, qRound(beat.time * 1000.0));
        addNumber(hash, qRound(beat.tempo * 100.0));
    }
    addNumber(hash, item.cues.size());
    for (const core::Cue& cue : item.cues) {
        addNumber(hash, static_cast<int>(cue.kind));
        addNumber(hash, cue.hotCueNumber);
        addNumber(hash, cue.colorId);
        addNumber(hash, qRound(cue.time * 1000.0));
        addNumber(hash, qRound(cue.loopTime * 1000.0));
        addText(hash, cue.name);
        addText(hash, cue.comment);
    }
    hash.addData(digest(item.peaks));
    return QString::fromLatin1(hash.result().toHex());
}

QString ExportManifest::libraryHash(const ExportLibrary& library) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const ExportPlaylist& playlist : library.playlists) {
        addNumber(hash, playlist.id);
        addNumber(hash, playlist.parentId);
        addNumber(hash, playlist.sortOrder);
        addNumber(hash, playlist.isFolder ? 1 : 0);
        addText(hash, playlist.name);
        for (quint32 trackId : playlist.trackIds) {
            addNumber(hash, trackId);
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

ExportManifest ExportManifest::load(const QString& exportRoot, QString* error) {
    ExportManifest manifest;
    const QString path = pathIn(exportRoot);
    QFile file(path);
    if (!file.exists()) {
        return manifest; // no previous export: a full write is needed
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return manifest;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        return manifest;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("generator")).toString()
                != QStringLiteral("open-deck-export")
            || root.value(QStringLiteral("version")).toInt() != kVersion) {
        return manifest;
    }
    manifest.valid = true;
    manifest.version = kVersion;
    manifest.createdDate = root.value(QStringLiteral("created")).toString();
    manifest.libraryDigest = root.value(QStringLiteral("playlists")).toString();
    const QJsonArray tracks = root.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue& value : tracks) {
        const QJsonObject entry = value.toObject();
        manifest.tracks.insert(entry.value(QStringLiteral("path")).toString(),
                entry.value(QStringLiteral("hash")).toString());
        const QString audioHash = entry.value(QStringLiteral("audioHash")).toString();
        if (audioHash.size() == 64)
            manifest.ownedAudio.insert(entry.value(QStringLiteral("path")).toString(), audioHash);
    }
    return manifest;
}

bool ExportManifest::save(const QString& exportRoot, QString* error) const {
    const QString path = pathIn(exportRoot);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) {
            *error = QStringLiteral("No se pudo crear la carpeta del manifiesto");
        }
        return false;
    }
    QJsonArray tracksArray;
    for (auto it = tracks.constBegin(); it != tracks.constEnd(); ++it) {
        QJsonObject entry;
        entry.insert(QStringLiteral("path"), it.key());
        entry.insert(QStringLiteral("hash"), it.value());
        if (ownedAudio.contains(it.key())) entry.insert(QStringLiteral("audioHash"), ownedAudio.value(it.key()));
        tracksArray.append(entry);
    }
    QJsonObject root;
    root.insert(QStringLiteral("generator"), QStringLiteral("open-deck-export"));
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("created"), createdDate);
    root.insert(QStringLiteral("playlists"), libraryDigest);
    root.insert(QStringLiteral("tracks"), tracksArray);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace device
} // namespace opendeck
