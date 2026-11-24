#include "ui/LibraryController.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QUrl>
#include <QSet>
#include <QTimer>

#include "library/AudioImportService.h"
#include "library/DatabaseManager.h"
#include "library/LibraryRelocator.h"
#include "library/TrackRepository.h"

namespace opendeck {
namespace ui {

LibraryController::LibraryController(library::DatabaseManager& db, QObject* parent)
        : QAbstractListModel(parent), m_db(db) {
    reload();
    auto* timer = new QTimer(this);
    timer->setInterval(5000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (m_missingCount > 0 && !m_importing) missingTrackIds();
    });
    timer->start();
    connect(this, &LibraryController::importProgress, this, [this](int current, int total) {
        m_importStatus = QStringLiteral("Importando %1/%2…").arg(current).arg(total);
        emit importingChanged();
    });
}

LibraryController::~LibraryController() {
    m_cancelImport.storeRelaxed(true);
    if (m_importThread) m_importThread->wait();
}

void LibraryController::cancelImport() { m_cancelImport.storeRelaxed(true); }

void LibraryController::importAsync(const QList<QUrl>& urls) {
    if (m_importing || urls.isEmpty()) return;
    delete m_importThread;
    m_cancelImport.storeRelaxed(false);
    m_importing = true;
    m_importStatus = QStringLiteral("Buscando archivos…");
    emit importingChanged();
    const QString dbPath = m_db.databasePath();
    m_importThread = QThread::create([this, urls, dbPath] {
        QStringList files;
        for (const auto& url : urls) {
            if (m_cancelImport.loadRelaxed()) break;
            if (!url.isLocalFile()) continue;
            const QString path = url.toLocalFile();
            if (QFileInfo(path).isDir()) {
                QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
                while (it.hasNext() && !m_cancelImport.loadRelaxed()) {
                    const QString file = it.next();
                    if (library::AudioImportService::isSupportedExtension(file)) files.append(file);
                }
            } else if (library::AudioImportService::isSupportedExtension(path)) files.append(path);
        }
        files.removeDuplicates();
        library::DatabaseManager workerDb(dbPath);
        QString error;
        int imported = 0, skipped = 0, failed = 0;
        if (workerDb.open(&error)) {
            library::TrackRepository repository(workerDb);
            for (int i = 0; i < files.size() && !m_cancelImport.loadRelaxed(); ++i) {
                emit importProgress(i + 1, files.size());
                const auto track = library::AudioImportService::readMetadata(files[i], &m_cancelImport);
                if (m_cancelImport.loadRelaxed()) break;
                if (track.fileHash.isEmpty()) { ++failed; continue; }
                if (repository.existsByHash(track.fileHash) || repository.existsByPath(track.filePath)) ++skipped;
                else if (repository.insertTrack(track) > 0) ++imported;
                else ++failed;
            }
        }
        const bool cancelled = m_cancelImport.loadRelaxed();
        QMetaObject::invokeMethod(this, [this, imported, skipped, failed, error, cancelled] {
            m_importStatus = error.isEmpty()
                ? QStringLiteral("%1%2 importadas · %3 duplicadas · %4 errores")
                    .arg(cancelled ? QStringLiteral("Cancelado · ") : QString()).arg(imported).arg(skipped).arg(failed)
                : QStringLiteral("Error importando: %1").arg(error);
            // The thread has no more work touching the library at this point.
            m_importThread->wait();
            m_importing = false;
            reload();
            emit importingChanged();
            emit importFinished(imported, skipped);
        }, Qt::QueuedConnection);
    });
    m_importThread->setParent(this);
    m_importThread->start();
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
        if (track.fileHash.isEmpty()) {
            ++skipped;
            continue;
        }
        const bool duplicate = repository.existsByHash(track.fileHash) ||
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
    missingTrackIds();
}

QList<qint64> LibraryController::missingTrackIds() {
    QList<qint64> ids;
    for (const core::Track& track : m_tracks) {
        if (!QFileInfo(track.filePath).isFile()) ids.append(track.id);
    }
    const int count = ids.size();
    if (count != m_missingCount) {
        m_missingCount = count;
        emit missingCountChanged();
    }
    return ids;
}

int LibraryController::removeMissing(const QList<qint64>& ids) {
    if (m_importing) return -1;
    const QSet<qint64> confirmed(ids.begin(), ids.end());
    auto missing = missingTrackIds();
    missing.removeIf([&](qint64 id) { return !confirmed.contains(id); });
    if (missing.isEmpty()) return 0;

    library::TrackRepository repository(m_db);
    if (!repository.removeTracks(missing)) return -1;
    reload();
    emit tracksRemoved();
    return missing.size();
}

int LibraryController::missingCount() const {
    return m_missingCount;
}

int LibraryController::relocateMissing() {
    if (m_importing) return 0;
    QString error;
    const library::RelocationReport report = library::LibraryRelocator::relocate(m_db, &error);
    reload();
    return report.relocated;
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
