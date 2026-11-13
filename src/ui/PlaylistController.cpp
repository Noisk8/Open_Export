#include "ui/PlaylistController.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVariant>

#include "export/ExportManager.h"
#include "export/ExportValidator.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"

namespace opendeck {
namespace ui {

QString PlaylistController::localPath(const QVariant& value) {
    QString path;
    if (value.metaType() == QMetaType::fromType<QUrl>()) {
        const QUrl url = value.toUrl();
        path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    } else {
        path = value.toString();
        if (path.startsWith(QStringLiteral("file:"))) {
            path = QUrl(path).toLocalFile();
        }
    }
    return path;
}

PlaylistController::PlaylistController(library::DatabaseManager& db, QObject* parent)
        : QAbstractListModel(parent), m_db(db) {
    reload();
}

int PlaylistController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_playlists.size());
}

QVariant PlaylistController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_playlists.size()) {
        return {};
    }
    const core::Playlist& playlist = m_playlists.at(index.row());
    switch (role) {
    case IdRole: return playlist.id;
    case NameRole: return playlist.name;
    case TrackCountRole: return static_cast<int>(playlist.trackIds.size());
    case IsFolderRole: return playlist.isFolder;
    case ParentIdRole: return playlist.parentId;
    default: return {};
    }
}

QHash<int, QByteArray> PlaylistController::roleNames() const {
    return {
            {IdRole, "playlistId"},
            {NameRole, "name"},
            {TrackCountRole, "trackCount"},
            {IsFolderRole, "isFolder"},
            {ParentIdRole, "parentId"},
    };
}

int PlaylistController::playlistCount() const {
    return static_cast<int>(m_playlists.size());
}

int PlaylistController::totalTrackCount() const {
    int total = 0;
    for (const core::Playlist& playlist : m_playlists) {
        total += static_cast<int>(playlist.trackIds.size());
    }
    return total;
}

qint64 PlaylistController::createPlaylist(const QString& name, qint64 parentId) {
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    library::PlaylistRepository repository(m_db);
    core::Playlist playlist;
    playlist.name = name.trimmed();
    playlist.parentId = parentId;
    playlist.sortOrder = playlistCount();
    const qint64 id = repository.savePlaylist(playlist);
    if (id > 0) {
        reload(); // reload() emits playlistsChanged()
    }
    return id;
}

bool PlaylistController::removePlaylist(qint64 playlistId) {
    library::PlaylistRepository repository(m_db);
    if (!repository.removePlaylist(playlistId)) {
        return false;
    }
    reload();
    return true;
}

bool PlaylistController::addTrack(qint64 playlistId, qint64 trackId) {
    library::PlaylistRepository repository(m_db);
    const QList<core::Playlist> playlists = repository.allPlaylists();
    int position = 0;
    for (const core::Playlist& playlist : playlists) {
        if (playlist.id == playlistId) {
            position = static_cast<int>(playlist.trackIds.size());
        }
    }
    if (!repository.addTrack(playlistId, trackId, position)) {
        return false;
    }
    reload();
    return true;
}

QList<qint64> PlaylistController::trackIds(qint64 playlistId) const {
    for (const core::Playlist& playlist : m_playlists) {
        if (playlist.id == playlistId) {
            return playlist.trackIds;
        }
    }
    return {};
}

device::ExportLibrary PlaylistController::buildExportLibrary() const {
    library::PlaylistRepository repository(m_db);
    device::ExportManager manager(m_db);
    return manager.buildLibrary(repository.allPlaylists());
}

QString PlaylistController::exportTo(const QVariant& destination) {
    const QString path = PlaylistController::localPath(destination);
    const QFileInfo target(path);
    if (!target.isDir()) {
        const QString message = QStringLiteral("%1 no es una carpeta").arg(path);
        emit exportFinished(false, message);
        return message;
    }

    const device::ExportLibrary library = buildExportLibrary();
    if (library.tracks.isEmpty()) {
        const QString message = QStringLiteral("No hay pistas para exportar");
        emit exportFinished(false, message);
        return message;
    }

    device::ExportManager manager(m_db);
    device::ExportOptions options;
    options.destination = target.absoluteFilePath();

    QString error;
    const device::ExportReport report = manager.write(library, options, &error);
    if (!error.isEmpty()) {
        emit exportFinished(false, error);
        return error;
    }

    QStringList issues;
    const bool valid = device::ExportValidator::validateExport(options.destination, &issues);
    if (!valid) {
        const QString message = issues.isEmpty()
                ? QStringLiteral("La exportación tiene problemas")
                : issues.join(QStringLiteral("\n"));
        emit exportFinished(false, message);
        return message;
    }

    const QString message =
            QStringLiteral("Exportadas %1 pistas, %2 listas y %3 análisis")
                    .arg(report.trackCount)
                    .arg(report.playlistCount)
                    .arg(report.analysisCount);
    emit exportFinished(true, message);
    return QString();
}

QString PlaylistController::validate(const QVariant& destination) {
    QStringList issues;
    if (!device::ExportValidator::validateExport(PlaylistController::localPath(destination), &issues)) {
        return issues.join(QStringLiteral("\n"));
    }
    return QString();
}

void PlaylistController::reload() {
    beginResetModel();
    library::PlaylistRepository repository(m_db);
    m_playlists = repository.allPlaylists();
    endResetModel();
    emit playlistsChanged();
}

} // namespace ui
} // namespace opendeck
