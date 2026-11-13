#include "export/ExportManager.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <stdexcept>

#include "export/ExportManifest.h"
#include "export/anlz/AnlzWriter.h"
#include "export/devicesql/DeviceSqlWriter.h"
#include "export/onelibrary/DeviceLibraryWriter.h"
#include "library/ArtworkExtractor.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"

namespace opendeck {
namespace device {

namespace {

// Rekordbox truncates the name of the audio files to 43 characters.
constexpr int kMaxFileStem = 43;

// Removes the characters that a FAT32 volume cannot hold.
QString sanitize(const QString& value, const QString& fallback) {
    QString result;
    result.reserve(value.size());
    for (const QChar c : value) {
        if (c == QChar('/') || c == QChar('\\') || c == QChar(':') || c == QChar('*')
                || c == QChar('?') || c == QChar('"') || c == QChar('<') || c == QChar('>')
                || c == QChar('|') || c == QChar(0)) {
            result.append(QChar('_'));
        } else {
            result.append(c);
        }
    }
    result = result.trimmed();
    if (result.isEmpty()) {
        return fallback;
    }
    return result;
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error(file.errorString().toStdString());
    }
    if (file.write(data) != data.size() || !file.commit()) {
        throw std::runtime_error(file.errorString().toStdString());
    }
}

// Resizes an image and writes it as a JPEG.
bool writeArtworkFile(const QString& path, const QByteArray& data) {
    QImage image;
    if (!image.loadFromData(data)) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage small = image.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage large = image.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!small.save(path, "JPG", 85)) {
        return false;
    }
    QFileInfo info(path);
    const QString largePath = info.absolutePath() + QChar('/') + info.completeBaseName()
            + QStringLiteral("_m.jpg");
    large.save(largePath, "JPG", 90);
    return true;
}

} // namespace

int ExportManager::fileTypeFor(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("mp3")) {
        return 1;
    }
    if (suffix == QStringLiteral("m4a")) {
        return 4;
    }
    if (suffix == QStringLiteral("flac")) {
        return 5;
    }
    if (suffix == QStringLiteral("wav")) {
        return 11;
    }
    if (suffix == QStringLiteral("aif") || suffix == QStringLiteral("aiff")) {
        return 12;
    }
    return 0;
}

QString ExportManager::audioPath(const core::Track& track) {
    const QFileInfo info(track.filePath);
    const QString artist = sanitize(track.artist, QStringLiteral("Unknown Artist"));
    const QString album = sanitize(track.album, QStringLiteral("Unknown Album"));
    const QString extension = info.suffix().toLower();

    QString stem = sanitize(track.title.isEmpty() ? info.completeBaseName() : track.title,
            QStringLiteral("Track"));
    if (stem.size() > kMaxFileStem) {
        stem.truncate(kMaxFileStem);
    }
    const QString fileName = extension.isEmpty() ? stem : stem + QChar('.') + extension;
    return QStringLiteral("/Contents/%1/%2/%3").arg(artist, album, fileName);
}

ExportLibrary ExportManager::buildLibrary(const QList<core::Playlist>& playlists) const {
    Q_ASSERT(m_db != nullptr);
    library::TrackRepository repository(*m_db);

    // Collect the tracks of the playlists, keeping the order of the first
    // appearance.
    QList<qint64> trackIds;
    QSet<qint64> seen;
    for (const core::Playlist& playlist : playlists) {
        for (qint64 id : playlist.trackIds) {
            if (!seen.contains(id)) {
                seen.insert(id);
                trackIds.append(id);
            }
        }
    }

    const QList<core::Track> tracks = repository.tracksByIds(trackIds);

    ExportLibrary library;
    QHash<qint64, quint32> deviceIds;
    QSet<QString> usedPaths;

    for (const core::Track& track : tracks) {
        ExportTrack item;
        item.track = track;
        item.deviceId = static_cast<quint32>(library.tracks.size() + 1);
        deviceIds.insert(track.id, item.deviceId);

        QString path = audioPath(track);
        // Two tracks may end up with the same name; keep them apart.
        int suffix = 2;
        while (usedPaths.contains(path)) {
            const QFileInfo info(path);
            const QString extension = info.suffix();
            const QString stem = extension.isEmpty() ? info.completeBaseName()
                                                    : info.completeBaseName();
            path = QStringLiteral("/Contents/%1/%2/%3 (%4).%5")
                    .arg(info.path().section(QStringLiteral("/"), 2, 2),
                            info.path().section(QStringLiteral("/"), 3, 3), stem)
                    .arg(suffix++)
                    .arg(extension);
        }
        usedPaths.insert(path);

        item.devicePath = path;
        item.fileName = path.section(QChar('/'), -1);
        item.fileType = fileTypeFor(track.filePath);
        item.analyzePath = AnlzWriter::analysisPath(path);

        const QFileInfo fileInfo(track.filePath);
        item.fileSize = fileInfo.exists() ? fileInfo.size() : 0;

        // Resolve the analysis here, on the GUI thread: the export worker must
        // be able to run without ever touching the SQLite connection.
        item.beats = repository.beats(track.id);
        item.cues = repository.cues(track.id);
        item.peaks = repository.waveform(track.id, 0);

        library.tracks.append(item);
    }

    // Build the playlist tree, keeping the folders and their order.
    QHash<qint64, quint32> nodeIds;
    quint32 nextId = 1;
    for (const core::Playlist& playlist : playlists) {
        nodeIds.insert(playlist.id, nextId++);
    }

    for (const core::Playlist& playlist : playlists) {
        ExportPlaylist node;
        node.id = nodeIds.value(playlist.id, 0);
        node.name = playlist.name;
        node.parentId = playlist.parentId == 0 ? 0 : nodeIds.value(playlist.parentId, 0);
        node.sortOrder = static_cast<quint32>(playlist.sortOrder);
        node.isFolder = playlist.isFolder;
        for (qint64 id : playlist.trackIds) {
            const quint32 deviceId = deviceIds.value(id, 0);
            if (deviceId != 0) {
                node.trackIds.append(deviceId);
            }
        }
        library.playlists.append(node);
    }

    return library;
}

ExportReport ExportManager::write(const ExportLibrary& library, const ExportOptions& options,
                                  QString* error, const ExportCallbacks& callbacks) {
    ExportReport report;
    if (error) {
        error->clear();
    }

    try {
        const QFileInfo target(options.destination);
        if (options.destination.trimmed().isEmpty() || !target.isDir()) {
            throw std::runtime_error("El destino no es un directorio válido");
        }
        if (library.tracks.isEmpty()) {
            throw std::runtime_error("No hay pistas para exportar");
        }

        const QString root = target.absoluteFilePath();
        const QString createdDate = QDate::currentDate().toString(Qt::ISODate);

        // --- Only export tracks whose audio is actually reachable ------------
        // A track whose source file disappeared must NOT be written into the
        // databases: that would produce a USB with rows pointing at missing
        // audio and a validation failure with one error per track.
        QList<ExportTrack> exportable;
        QSet<quint32> exportableIds;
        for (const ExportTrack& item : library.tracks) {
            const QFileInfo source(item.track.filePath);
            if (!source.isFile() || !source.isReadable()) {
                ++report.missingTracks;
                report.warnings.append(
                        QStringLiteral("Archivo no encontrado: %1").arg(item.track.filePath));
                continue;
            }
            exportable.append(item);
            exportableIds.insert(item.deviceId);
        }
        if (exportable.isEmpty()) {
            throw std::runtime_error(
                    "Ninguna pista tiene un archivo de audio accesible. Conecta el "
                    "dispositivo con la música o vuelve a importar las pistas.");
        }

        // A previous export lets the incremental mode reuse unchanged files.
        const ExportManifest previous =
                options.incremental ? ExportManifest::load(root, error) : ExportManifest();
        ExportManifest next;
        next.valid = true;
        next.version = ExportManifest::kVersion;
        next.createdDate = createdDate;

        // --- Artwork ids are assigned up front -------------------------------
        // They must not depend on which tracks the incremental mode skips, or
        // the rows of export.pdb / exportLibrary.db would point at the wrong
        // image on the next run.
        QHash<qint64, QByteArray> artworkBytes;
        QHash<qint64, quint32> artworkIds;
        QHash<qint64, QString> artworkPaths;
        QHash<QByteArray, quint32> seenArtwork;
        quint32 nextArtworkId = 1;
        if (options.writeArtwork) {
            for (const ExportTrack& item : exportable) {
                const QList<library::EmbeddedArtwork> artwork =
                        library::readEmbeddedArtwork(item.track.filePath);
                for (const library::EmbeddedArtwork& image : artwork) {
                    if (!image.mimeType.contains(QStringLiteral("jpeg"))
                            && !image.mimeType.contains(QStringLiteral("jpg"))) {
                        continue;
                    }
                    const QByteArray key =
                            QCryptographicHash::hash(image.data, QCryptographicHash::Sha256);
                    quint32 id = 0;
                    const auto it = seenArtwork.constFind(key);
                    if (it != seenArtwork.constEnd()) {
                        id = it.value();
                    } else {
                        id = nextArtworkId++;
                        seenArtwork.insert(key, id);
                    }
                    artworkIds.insert(item.track.id, id);
                    artworkBytes.insert(item.track.id, image.data);
                    const QString folder = QStringLiteral("/PIONEER/Artwork/%1")
                            .arg((id - 1) / 20 + 1, 5, 10, QChar('0'));
                    artworkPaths.insert(item.track.id,
                            folder + QStringLiteral("/a%1.jpg").arg(id));
                    break;
                }
            }
        }

        // --- Copy the audio and write the analysis --------------------------
        for (int index = 0; index < exportable.size(); ++index) {
            const ExportTrack& item = exportable.at(index);
            const core::Track& track = item.track;

            if (callbacks.cancelled && callbacks.cancelled()) {
                report.cancelled = true;
                return report;
            }
            if (callbacks.progress) {
                callbacks.progress(index, static_cast<int>(exportable.size()),
                        item.fileName);
            }

            const QString trackDigest = ExportManifest::trackHash(item);
            next.tracks.insert(item.devicePath, trackDigest);
            const QString audioTarget = root + item.devicePath;
            const QString anlzTarget = root + item.analyzePath;
            // Exporting onto the same volume that already holds Contents/ means
            // source and destination can be the very same file: never delete it.
            const bool sameFile = QFileInfo(track.filePath).absoluteFilePath()
                    == QFileInfo(audioTarget).absoluteFilePath();
            const bool reusable = previous.valid
                    && previous.tracks.value(item.devicePath) == trackDigest
                    && (!options.copyAudio || QFile::exists(audioTarget))
                    && (!options.writeAnalysis || QFile::exists(anlzTarget));
            if (reusable) {
                ++report.skippedTracks;
                continue;
            }

            if (options.copyAudio && !sameFile) {
                QDir().mkpath(QFileInfo(audioTarget).absolutePath());
                // An export may be written over a previous one.
                QFile::remove(audioTarget);
                if (!QFile::copy(track.filePath, audioTarget)) {
                    throw std::runtime_error("No se pudo copiar " + track.filePath.toStdString());
                }
            }

            if (options.writeAnalysis) {
                AnlzData data;
                data.devicePath = item.devicePath;
                data.duration = track.duration;
                data.beats = item.beats;
                data.cues = item.cues;
                data.peaks = item.peaks;

                const QString directory = root + AnlzWriter::analysisDirectory(item.devicePath);
                QDir().mkpath(directory);
                writeFile(directory + QStringLiteral("/ANLZ0000.DAT"),
                        AnlzWriter::buildDat(data));
                writeFile(directory + QStringLiteral("/ANLZ0000.EXT"),
                        AnlzWriter::buildExt(data));
                writeFile(directory + QStringLiteral("/ANLZ0000.2EX"),
                        AnlzWriter::build2Ex(data));
                ++report.analysisCount;
            }

            if (artworkBytes.contains(track.id)) {
                const QString path = root + artworkPaths.value(track.id);
                if (writeArtworkFile(path, artworkBytes.value(track.id))) {
                    ++report.artworkCount;
                }
            }
        }

        // --- Remove the files of tracks that are no longer exported -----------
        if (previous.valid) {
            for (auto it = previous.tracks.constBegin(); it != previous.tracks.constEnd(); ++it) {
                if (next.tracks.contains(it.key())) {
                    continue;
                }
                QFile::remove(root + it.key());
                const QString directory = root + AnlzWriter::analysisDirectory(it.key());
                QFile::remove(directory + QStringLiteral("/ANLZ0000.DAT"));
                QFile::remove(directory + QStringLiteral("/ANLZ0000.EXT"));
                QFile::remove(directory + QStringLiteral("/ANLZ0000.2EX"));
                ++report.removedTracks;
            }
        }

        // --- Both databases --------------------------------------------------
        // Build them from the tracks that were really written, and drop the
        // playlist entries that pointed at missing files. Otherwise a USB with
        // unreadable sources would still end up with a consistent database.
        ExportLibrary prepared;
        prepared.tracks = exportable;
        for (ExportTrack& item : prepared.tracks) {
            item.artworkId = artworkIds.value(item.track.id, 0);
            item.artworkPath = artworkPaths.value(item.track.id, QString());
        }
        for (const ExportPlaylist& playlist : library.playlists) {
            ExportPlaylist filtered = playlist;
            filtered.trackIds.clear();
            for (quint32 trackId : playlist.trackIds) {
                if (exportableIds.contains(trackId)) {
                    filtered.trackIds.append(trackId);
                }
            }
            prepared.playlists.append(filtered);
        }
        next.libraryDigest = ExportManifest::libraryHash(prepared);

        DeviceSqlWriter writer;
        writer.setLibrary(prepared);
        writer.setCreationDate(createdDate);
        writeFile(root + QStringLiteral("/PIONEER/rekordbox/export.pdb"), writer.build());

        if (options.writeDeviceLibrary) {
            QString dlpError;
            if (!onelibrary::DeviceLibraryWriter::write(
                        root + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db"),
                        prepared, createdDate, &dlpError)) {
                throw std::runtime_error("Device Library Plus: "
                        + dlpError.toStdString());
            }
        }

        // Save the manifest even for a full export: the next one can reuse it.
        QString manifestError;
        if (!next.save(root, &manifestError)) {
            report.warnings.append(manifestError);
        }

        report.trackCount = static_cast<int>(prepared.tracks.size());
        report.playlistCount = static_cast<int>(prepared.playlists.size());
        return report;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return report;
    }
}

} // namespace device
} // namespace opendeck
