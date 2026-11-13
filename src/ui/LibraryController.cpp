#include "ui/LibraryController.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "library/AudioImportService.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"

namespace opendeck {
namespace ui {

LibraryController::LibraryController(library::DatabaseManager& db, QObject* parent)
        : QAbstractListModel(parent), m_db(db) {
    reload();
}

int LibraryController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_tracks.size());
}

QVariant LibraryController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size()) {
        return {};
    }
    const core::Track& t = m_tracks.at(index.row());
    switch (role) {
    case IdRole: return t.id;
    case TitleRole: return t.title;
    case ArtistRole: return t.artist;
    case AlbumRole: return t.album;
    case GenreRole: return t.genre;
    case YearRole: return t.year;
    case DurationRole: return t.duration;
    case BpmRole: return t.bpm;
    case KeyRole: return t.musicalKey;
    case AnalysisStatusRole: return static_cast<int>(t.analysisStatus);
    case FilePathRole: return t.filePath;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryController::roleNames() const {
    return {
            {IdRole, "trackId"},
            {TitleRole, "title"},
            {ArtistRole, "artist"},
            {AlbumRole, "album"},
            {GenreRole, "genre"},
            {YearRole, "year"},
            {DurationRole, "duration"},
            {BpmRole, "bpm"},
            {KeyRole, "key"},
            {AnalysisStatusRole, "analysisStatus"},
            {FilePathRole, "filePath"},
    };
}

int LibraryController::importFiles(const QList<QUrl>& urls) {
    // Flatten the drop payload (files and folders) into a list of audio paths.
    QStringList files;
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            collectAudioFiles(path, files);
        } else if (library::AudioImportService::isSupportedExtension(path)) {
            files.append(path);
        }
    }

    library::TrackRepository repository(m_db);
    int imported = 0;
    int skipped = 0;
    const int total = files.size();

    for (int i = 0; i < files.size(); ++i) {
        emit importProgress(i + 1, total);
        const QString& path = files.at(i);

        core::Track track = library::AudioImportService::readMetadata(path);

        // Deduplicate by content hash (persistent identity) or, failing that,
        // by path.
        const bool duplicate =
                (!track.fileHash.isEmpty() && repository.existsByHash(track.fileHash)) ||
                repository.existsByPath(track.filePath);
        if (duplicate) {
            ++skipped;
            continue;
        }

        if (repository.insertTrack(track) != 0) {
            ++imported;
        } else {
            ++skipped;
        }
    }

    reload();
    emit importFinished(imported, skipped);
    return imported;
}

int LibraryController::importFolder(const QString& path) {
    return importFiles({QUrl::fromLocalFile(path)});
}

int LibraryController::trackCount() const {
    return static_cast<int>(m_tracks.size());
}

qint64 LibraryController::trackIdAt(int row) const {
    if (row < 0 || row >= m_tracks.size()) {
        return 0;
    }
    return m_tracks.at(row).id;
}

void LibraryController::reload() {
    beginResetModel();
    library::TrackRepository repository(m_db);
    m_tracks = repository.allTracks();
    endResetModel();
    emit trackCountChanged();
}

void LibraryController::refresh() {
    reload();
}

void LibraryController::collectAudioFiles(const QString& dir, QStringList& out) {
    QDir directory(dir);
    const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            collectAudioFiles(entry.absoluteFilePath(), out);
        } else if (library::AudioImportService::isSupportedExtension(entry.absoluteFilePath())) {
            out.append(entry.absoluteFilePath());
        }
    }
}

} // namespace ui
} // namespace opendeck
