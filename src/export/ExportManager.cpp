#include "export/ExportManager.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDate>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QImage>
#include <QSet>
#include <QStringList>

#include <stdexcept>

#include "export/ExportManifest.h"
#include "export/ExportPublication.h"
#include "export/ExportValidator.h"
#include "export/anlz/AnlzWriter.h"
#include "export/devicesql/DeviceSqlWriter.h"
#include "export/onelibrary/DeviceLibraryWriter.h"
#include "library/ArtworkExtractor.h"
#include "library/AudioImportService.h"
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
    for (const QChar c : value.normalized(QString::NormalizationForm_C)) {
        if (c == QChar('/') || c == QChar('\\') || c == QChar(':') || c == QChar('*')
                || c == QChar('?') || c == QChar('"') || c == QChar('<') || c == QChar('>')
                || c == QChar('|') || c.unicode() < 32) {
            result.append(QChar('_'));
        } else {
            result.append(c);
        }
    }
    result = result.trimmed();
    while (result.endsWith('.') || result.endsWith(' ')) result.chop(1);
    result = result.left(100);
    if (!result.isEmpty() && result.back().isHighSurrogate()) result.chop(1);
    if (result.isEmpty()) {
        return fallback;
    }
    return result;
}

void checkRxAudio(const QString& path, int type, const core::Track& audio) {
    const bool pcm = type == 11 || type == 12;
    const bool rate = audio.sampleRate == 44100 || audio.sampleRate == 48000;
    if ((type != 1 && type != 4 && !pcm) || audio.channels < 1 || audio.channels > 2
            || (pcm && (!rate || (audio.sampleDepth != 16 && audio.sampleDepth != 24)))
            || (type == 11 && !library::AudioImportService::isIntegerPcmWav(path))) {
        throw std::runtime_error(QStringLiteral("Audio fuera del perfil XDJ-RX: %1").arg(path).toStdString());
    }
}

QString fileDigest(const QString& path, const ExportCallbacks& callbacks) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        if (callbacks.cancelled && callbacks.cancelled()) return {};
        const auto chunk = file.read(1024 * 1024);
        if (file.error() != QFile::NoError) return {};
        hash.addData(chunk);
    }
    return QString::fromLatin1(hash.result().toHex());
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
    QImage small = image.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage large = image.scaled(240, 240, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray smallBytes;
    QByteArray largeBytes;
    QBuffer smallBuffer(&smallBytes);
    QBuffer largeBuffer(&largeBytes);
    if (!smallBuffer.open(QIODevice::WriteOnly) || !largeBuffer.open(QIODevice::WriteOnly)
            || !small.save(&smallBuffer, "JPG", 85) || !large.save(&largeBuffer, "JPG", 90)) return false;
    writeFile(path, smallBytes);
    writeFile(path.chopped(4) + QStringLiteral("_m.jpg"), largeBytes);
    return true;
}

} // namespace

int ExportManager::fileTypeFor(const QString& filePath) {
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("mp3")) {
        return 1;
    }
    if (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("aac")) {
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

ExportLibrary ExportManager::buildLibrary(const QList<core::Playlist>& playlists,
                                          bool useWavForFlac) const {
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
    QSet<QString> usedAnalysisPaths;

    for (const core::Track& track : tracks) {
        ExportTrack item;
        item.track = track;
        if (useWavForFlac && track.filePath.endsWith(".flac", Qt::CaseInsensitive)) {
            const auto [path, hash] = repository.verifiedWav(track.fileHash);
            item.track.filePath = path;
            item.verifiedWavHash = hash;
            if (item.track.title.isEmpty())
                item.track.title = QFileInfo(track.filePath).completeBaseName();
        }
        item.deviceId = static_cast<quint32>(library.tracks.size() + 1);
        deviceIds.insert(track.id, item.deviceId);

        QString path = audioPath(item.track);
        // Two tracks may end up with the same name; keep them apart.
        int suffix = 2;
        const QFileInfo info(path);
        while (usedPaths.contains(path.toCaseFolded())
                || usedAnalysisPaths.contains(AnlzWriter::analysisPath(path))) {
            const QString extension = info.suffix();
            const QString stem = info.completeBaseName();
            path = QStringLiteral("/Contents/%1/%2/%3 (%4).%5")
                    .arg(info.path().section(QStringLiteral("/"), 2, 2),
                            info.path().section(QStringLiteral("/"), 3, 3), stem)
                    .arg(suffix++)
                    .arg(extension);
        }
        usedPaths.insert(path.toCaseFolded());

        item.devicePath = path;
        item.fileName = path.section(QChar('/'), -1);
        item.fileType = fileTypeFor(item.track.filePath);
        item.analyzePath = AnlzWriter::analysisPath(path);
        usedAnalysisPaths.insert(item.analyzePath);

        const QFileInfo fileInfo(item.track.filePath);
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

        const QString root = target.canonicalFilePath();
        const QStorageInfo volume(root);
        const QByteArray fs = volume.fileSystemType().toLower();
        const bool fat = fs == "vfat" || fs == "msdos" || fs == "fat" || fs == "fat32";
        if (options.xdjRxProfile && root == volume.rootPath() && !fat)
            throw std::runtime_error("El perfil XDJ-RX requiere un USB FAT o FAT32");
        ExportPublication publication(root);
        const QString createdDate = QDate::currentDate().toString(Qt::ISODate);

        // --- Only export tracks whose audio is actually reachable ------------
        // A track whose source file disappeared must NOT be written into the
        // databases: that would produce a USB with rows pointing at missing
        // audio and a validation failure with one error per track.
        QList<ExportTrack> exportable;
        QSet<quint32> exportableIds;
        for (const ExportTrack& item : library.tracks) {
            if (item.track.filePath.isEmpty())
                throw std::runtime_error(QStringLiteral("Falta convertir a WAV: %1")
                        .arg(item.track.title).toStdString());
            const QFileInfo source(item.track.filePath);
            if (!source.isFile() || !source.isReadable()) {
                if (!item.verifiedWavHash.isEmpty())
                    throw std::runtime_error(QStringLiteral("WAV verificado no disponible: %1")
                            .arg(item.track.filePath).toStdString());
                ++report.missingTracks;
                report.warnings.append(
                        QStringLiteral("Archivo no encontrado: %1").arg(item.track.filePath));
                continue;
            }
            if (item.fileType == 0)
                throw std::runtime_error(QStringLiteral("Formato sin soporte de exportación: %1")
                        .arg(item.track.filePath).toStdString());
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
                options.incremental ? ExportManifest::load(root, nullptr) : ExportManifest();
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
            ExportTrack& item = exportable[index];
            core::Track& track = item.track;

            if (callbacks.cancelled && callbacks.cancelled()) {
                report.cancelled = true;
                return report;
            }
            if (callbacks.progress) {
                callbacks.progress(index, static_cast<int>(exportable.size()),
                        item.fileName);
            }

            const QFileInfo sourceInfo(track.filePath);
            item.fileSize = sourceInfo.size();
            const QString audioTarget = publication.destination(item.devicePath);
            const bool sameFile = sourceInfo.canonicalFilePath()
                    == QFileInfo(audioTarget).canonicalFilePath();
            if (options.incremental || options.copyAudio || !item.verifiedWavHash.isEmpty()) {
                track.fileHash = fileDigest(track.filePath, callbacks);
                if (track.fileHash.isEmpty()) {
                    if (callbacks.cancelled && callbacks.cancelled()) {
                        report.cancelled = true;
                        return report;
                    }
                    throw std::runtime_error("No se pudo verificar el audio original");
                }
            }
            const auto audio = library::AudioImportService::readMetadata(track.filePath, nullptr, false);
            if (track.isrc.isEmpty()) track.isrc = audio.isrc;
            if (track.comment.isEmpty()) track.comment = audio.comment;
            if (track.label.isEmpty()) track.label = audio.label;
            if (track.composer.isEmpty()) track.composer = audio.composer;
            if (track.mixName.isEmpty()) track.mixName = audio.mixName;
            if (track.trackNumber == 0) track.trackNumber = audio.trackNumber;
            if (track.discNumber == 0) track.discNumber = audio.discNumber;
            if (!item.verifiedWavHash.isEmpty() || options.xdjRxProfile) {
                if (!item.verifiedWavHash.isEmpty()
                        && (audio.sampleRate <= 0 || track.fileHash != item.verifiedWavHash))
                    throw std::runtime_error("El WAV verificado cambió; vuelve a convertir el FLAC");
                if (options.xdjRxProfile) checkRxAudio(track.filePath, item.fileType, audio);
                track.sampleDepth = audio.sampleDepth;
                track.sampleRate = audio.sampleRate;
                track.channels = audio.channels;
                track.bitrate = audio.bitrate;
            }
            const QString trackDigest = ExportManifest::trackHash(item);
            if (options.copyAudio) next.tracks.insert(item.devicePath, trackDigest);

            const bool reusable = previous.valid
                    && previous.tracks.value(item.devicePath) == trackDigest
                    && (!options.copyAudio || fileDigest(audioTarget, callbacks) == track.fileHash);
            if (options.copyAudio && !sameFile
                    && (!reusable || previous.ownedAudio.contains(item.devicePath)))
                next.ownedAudio.insert(item.devicePath, track.fileHash);
            bool skipped = reusable;
            if (options.copyAudio && !sameFile && !reusable) {
                if (fat && item.fileSize > 0xffffffffLL)
                    throw std::runtime_error("Un archivo supera el límite de 4 GiB de FAT32");
                const QStorageInfo current(root);
                if (current.bytesAvailable() >= 0 && item.fileSize > current.bytesAvailable())
                    throw std::runtime_error("Espacio insuficiente para preparar la exportación");
                const QString stagedAudio = publication.stage() + item.devicePath;
                QDir().mkpath(QFileInfo(stagedAudio).absolutePath());
                QFile source(track.filePath);
                QSaveFile target(stagedAudio);
                QCryptographicHash copied(QCryptographicHash::Sha256);
                if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
                    throw std::runtime_error("No se pudo abrir el audio para copiar");
                while (!source.atEnd()) {
                    if (callbacks.cancelled && callbacks.cancelled()) {
                        report.cancelled = true;
                        return report;
                    }
                    const QByteArray chunk = source.read(1024 * 1024);
                    copied.addData(chunk);
                    if (source.error() != QFile::NoError || target.write(chunk) != chunk.size())
                        throw std::runtime_error("Error copiando audio");
                }
                if (QString::fromLatin1(copied.result().toHex()) != track.fileHash)
                    throw std::runtime_error("El audio original cambió durante la copia");
                if (!target.commit()) throw std::runtime_error("No se pudo guardar el audio");
            }

            if (options.writeAnalysis) {
                AnlzData data;
                data.devicePath = item.devicePath;
                data.duration = track.duration;
                data.beats = item.beats;
                data.cues = item.cues;
                data.peaks = item.peaks;

                const QString directory = AnlzWriter::analysisDirectory(item.devicePath);
                const QList<QPair<QString, QByteArray>> files{
                    {"/ANLZ0000.DAT", AnlzWriter::buildDat(data)},
                    {"/ANLZ0000.EXT", AnlzWriter::buildExt(data)},
                    {"/ANLZ0000.2EX", AnlzWriter::build2Ex(data)}};
                bool analysisWritten = false;
                for (const auto& [name, bytes] : files) {
                    QFile existing(publication.destination(directory + name));
                    if (reusable && existing.open(QIODevice::ReadOnly)
                            && existing.size() == bytes.size() && existing.readAll() == bytes) continue;
                    writeFile(publication.stage() + directory + name, bytes);
                    analysisWritten = true;
                }
                if (analysisWritten) {
                    ++report.analysisCount;
                    skipped = false;
                }
            }

            if (skipped) ++report.skippedTracks;
            if (artworkBytes.contains(track.id)) {
                const QString path = publication.stage() + artworkPaths.value(track.id);
                if (writeArtworkFile(path, artworkBytes.value(track.id))) {
                    ++report.artworkCount;
                } else {
                    artworkIds.remove(track.id);
                    artworkPaths.remove(track.id);
                    report.warnings.append(QStringLiteral("No se pudo escribir la carátula de %1").arg(track.title));
                }
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
        writeFile(publication.stage() + QStringLiteral("/PIONEER/rekordbox/export.pdb"), writer.build());

        if (options.writeDeviceLibrary) {
            QString dlpError;
            if (!onelibrary::DeviceLibraryWriter::write(
                        publication.stage() + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db"),
                        prepared, createdDate, &dlpError)) {
                throw std::runtime_error("Device Library Plus: "
                        + dlpError.toStdString());
            }
        }

        const QString stage = publication.stage();
        QString manifestError;
        if (!next.save(stage, &manifestError))
            throw std::runtime_error(manifestError.toStdString());

        QStringList issues;
        if (!ExportValidator::validateExport(stage, &issues, root, options.copyAudio,
                    options.writeAnalysis))
            throw std::runtime_error(issues.join(QStringLiteral(" · ")).toStdString());
        if (callbacks.cancelled && callbacks.cancelled()) {
            report.cancelled = true;
            return report;
        }

        QStringList removePaths;
        if (!options.writeDeviceLibrary)
            removePaths.append(QStringLiteral("/PIONEER/rekordbox/exportLibrary.db"));
        publication.publish(removePaths, [&] {
            issues.clear();
            if (!ExportValidator::validateExport(root, &issues, {}, options.copyAudio,
                        options.writeAnalysis))
                throw std::runtime_error(issues.join(QStringLiteral(" · ")).toStdString());
        });

        QSet<QString> retained;
        for (auto it = next.tracks.cbegin(); it != next.tracks.cend(); ++it)
            retained.insert(it.key().toCaseFolded());
        QSet<QString> retainedAnalysis;
        for (const auto& item : exportable) retainedAnalysis.insert(item.analyzePath);
        QSet<QString> sourcePaths;
        for (const auto& item : exportable) {
            const QString path = QFileInfo(item.track.filePath).canonicalFilePath();
            if (path.startsWith(root + '/')) sourcePaths.insert(path.mid(root.size()).toCaseFolded());
        }
        // --- Remove the files of tracks that are no longer exported -----------
        if (previous.valid) {
            for (auto it = previous.ownedAudio.cbegin(); it != previous.ownedAudio.cend(); ++it) {
                if (!options.copyAudio || retained.contains(it.key().toCaseFolded())
                        || sourcePaths.contains(it.key().toCaseFolded())) {
                    continue;
                }
                if (!it.key().startsWith("/Contents/") || QDir::cleanPath(it.key()) != it.key()) continue;
                try {
                    const QString path = publication.destination(it.key());
                    if (fileDigest(path, {}) != it.value() || !QFile::remove(path)) continue;
                    ++report.removedTracks;
                    if (!retainedAnalysis.contains(AnlzWriter::analysisPath(it.key()))) {
                        const QString directory = AnlzWriter::analysisDirectory(it.key());
                        for (const QString& name : {QStringLiteral("/ANLZ0000.DAT"),
                                                    QStringLiteral("/ANLZ0000.EXT"),
                                                    QStringLiteral("/ANLZ0000.2EX")})
                            QFile::remove(publication.destination(directory + name));
                    }
                } catch (const std::exception& e) {
                    report.warnings.append(QString::fromUtf8(e.what()));
                    break;
                }
            }
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
