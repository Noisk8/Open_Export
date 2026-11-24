#include "ui/PlaylistController.h"

#include <QDir>
#include <QSqlQuery>
#include <QSet>
#include <functional>
#include "library/Savepoint.h"
#include "library/TrackRepository.h"
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
    case DepthRole: {
        int depth = 0;
        qint64 parent = playlist.parentId;
        QSet<qint64> seen;
        while (parent && !seen.contains(parent)) {
            seen.insert(parent);
            ++depth;
            const auto it = std::find_if(m_playlists.cbegin(), m_playlists.cend(),
                [parent](const auto& p) { return p.id == parent; });
            parent = it == m_playlists.cend() ? 0 : it->parentId;
        }
        return depth;
    }
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
            {DepthRole, "depth"},
    };
}

int PlaylistController::playlistCount() const {
    return static_cast<int>(m_playlists.size());
}

int PlaylistController::totalTrackCount() const {
    QSet<qint64> ids;
    for (const auto& playlist : m_playlists)
        for (auto id : playlist.trackIds) ids.insert(id);
    return ids.size();
}

qint64 PlaylistController::createPlaylist(const QString& name, qint64 parentId, bool folder) {
    if (name.trimmed().isEmpty()) {
        fail(QStringLiteral("Escribe un nombre para la lista o carpeta"));
        return 0;
    }
    library::PlaylistRepository repository(m_db);
    core::Playlist playlist;
    playlist.name = name.trimmed();
    if (parentId && std::none_of(m_playlists.cbegin(), m_playlists.cend(),
            [parentId](const auto& p) { return p.id == parentId && p.isFolder; })) {
        fail(QStringLiteral("El destino debe ser una carpeta"));
        return 0;
    }
    playlist.parentId = parentId;
    playlist.isFolder = folder;
    for (const auto& p : m_playlists)
        if (p.parentId == parentId) playlist.sortOrder = qMax(playlist.sortOrder, p.sortOrder + 1);
    const qint64 id = repository.savePlaylist(playlist);
    if (id > 0) {
        reload(); // reload() emits playlistsChanged()
    }
    if (!id) fail(QStringLiteral("No se pudo guardar la lista o carpeta"));
    return id;
}

bool PlaylistController::removePlaylist(qint64 playlistId) {
    library::PlaylistRepository repository(m_db);
    if (std::none_of(m_playlists.cbegin(), m_playlists.cend(), [playlistId](const auto& p) { return p.id == playlistId; })
            || !repository.removePlaylist(playlistId)) {
        return fail(QStringLiteral("No se pudo eliminar la lista o carpeta"));
    }
    reload();
    return true;
}

bool PlaylistController::addTrack(qint64 playlistId, qint64 trackId) {
    return editTracks(playlistId, {trackId});
}

bool PlaylistController::renamePlaylist(qint64 playlistId, const QString& name) {
    if (name.trimmed().isEmpty()) return fail(QStringLiteral("El nombre no puede estar vacío"));
    QSqlQuery query(m_db.database());
    query.prepare(QStringLiteral("UPDATE playlists SET name = ? WHERE id = ?"));
    query.addBindValue(name.trimmed());
    query.addBindValue(playlistId);
    if (!query.exec() || query.numRowsAffected() == 0) return fail(QStringLiteral("No se pudo renombrar: lista inexistente o almacenamiento no disponible"));
    reload();
    return true;
}

bool PlaylistController::removeTrack(qint64 playlistId, qint64 trackId) {
    return editTracks(playlistId, {trackId}, true);
}

bool PlaylistController::fail(const QString& message) {
    m_lastError = message;
    emit errorChanged();
    return false;
}

bool PlaylistController::editTracks(qint64 playlistId, const QList<qint64>& ids, bool remove) {
    library::PlaylistRepository repository(m_db);
    for (auto playlist : m_playlists) {
        if (playlist.id != playlistId || playlist.isFolder) continue;
        for (auto id : ids) {
            if (remove) playlist.trackIds.removeAll(id);
            else if (!playlist.trackIds.contains(id)) playlist.trackIds.append(id);
        }
        if (!repository.savePlaylist(playlist)) return fail(QStringLiteral("No se pudieron guardar las pistas; no se aplicaron cambios"));
        reload();
        return true;
    }
    return fail(QStringLiteral("Selecciona una playlist, no una carpeta"));
}

bool PlaylistController::moveTrack(qint64 playlistId, qint64 trackId, qint64 beforeTrackId) {
    library::PlaylistRepository repository(m_db);
    for (auto playlist : m_playlists) {
        if (playlist.id != playlistId || playlist.isFolder) continue;
        if (trackId == beforeTrackId) return true;
        if (!playlist.trackIds.contains(trackId) || (beforeTrackId && !playlist.trackIds.contains(beforeTrackId)))
            return fail(QStringLiteral("La pista no pertenece a esta lista"));
        playlist.trackIds.removeAll(trackId);
        const int position = beforeTrackId ? playlist.trackIds.indexOf(beforeTrackId) : playlist.trackIds.size();
        playlist.trackIds.insert(position, trackId);
        if (!repository.savePlaylist(playlist)) return fail(QStringLiteral("No se pudo guardar el orden"));
        reload();
        return true;
    }
    return fail(QStringLiteral("Lista no encontrada"));
}

bool PlaylistController::movePlaylist(qint64 id, qint64 parentId, qint64 beforeId) {
    QHash<qint64, core::Playlist> nodes;
    for (const auto& p : m_playlists) nodes.insert(p.id, p);
    if (!nodes.contains(id) || (parentId && (!nodes.contains(parentId) || !nodes[parentId].isFolder)))
        return fail(QStringLiteral("Selecciona una carpeta de destino válida"));
    QSet<qint64> seen;
    for (auto ancestor = parentId; ancestor; ancestor = nodes.value(ancestor).parentId) {
        if (ancestor == id || seen.contains(ancestor)) return fail(QStringLiteral("Una carpeta no puede contenerse a sí misma"));
        seen.insert(ancestor);
    }
    if (beforeId == id) return true;
    if (beforeId && (!nodes.contains(beforeId) || nodes[beforeId].parentId != parentId))
        return fail(QStringLiteral("El destino no está en esa carpeta"));
    QList<qint64> siblings;
    for (const auto& p : m_playlists) if (p.parentId == parentId && p.id != id) siblings.append(p.id);
    siblings.insert(beforeId ? siblings.indexOf(beforeId) : siblings.size(), id);
    library::Savepoint transaction(m_db.database());
    if (!transaction.valid()) return fail(QStringLiteral("No se pudo iniciar el cambio"));
    QSqlQuery query(m_db.database());
    query.prepare(QStringLiteral("UPDATE playlists SET parent_id = ?, sort_order = ? WHERE id = ?"));
    for (int i = 0; i < siblings.size(); ++i) {
        query.bindValue(0, parentId ? QVariant(parentId) : QVariant());
        query.bindValue(1, i);
        query.bindValue(2, siblings[i]);
        if (!query.exec()) return fail(QStringLiteral("No se pudo mover la lista"));
    }
    if (!transaction.commit()) return fail(QStringLiteral("No se pudo guardar el movimiento"));
    reload();
    return true;
}

QVariantList PlaylistController::folders() const {
    QVariantList result{QVariantMap{{"id", 0}, {"name", QStringLiteral("Raíz")}}};
    for (int i = 0; i < m_playlists.size(); ++i) {
        const auto& p = m_playlists[i];
        if (p.isFolder) result.append(QVariantMap{{"id", p.id}, {"name", QString(data(index(i), DepthRole).toInt() * 2, ' ') + p.name}});
    }
    return result;
}

QList<core::Playlist> PlaylistController::exportPlaylists(const QList<qint64>& ids, bool all) const {
    if (all) return m_playlists;
    QSet<qint64> selected(ids.cbegin(), ids.cend());
    // Selecting a folder includes its descendants, then preserve all ancestors.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& p : m_playlists)
            if (p.parentId && selected.contains(p.parentId) && !selected.contains(p.id)) {
                selected.insert(p.id); changed = true;
            }
    }
    changed = true;
    while (changed) {
        changed = false;
        for (const auto& p : m_playlists)
            if (selected.contains(p.id) && p.parentId && !selected.contains(p.parentId)) {
                selected.insert(p.parentId); changed = true;
            }
    }
    QList<core::Playlist> result;
    for (const auto& p : m_playlists) if (selected.contains(p.id)) result.append(p);
    return result;
}

QList<qint64> PlaylistController::exportTrackIds(const QList<qint64>& ids, bool all) const {
    QSet<qint64> unique;
    for (const auto& playlist : exportPlaylists(ids, all))
        for (qint64 id : playlist.trackIds) unique.insert(id);
    return unique.values();
}

QVariantMap PlaylistController::exportSummary(const QList<qint64>& ids, bool all,
                                              bool useWavForFlac) const {
    QSet<qint64> unique;
    int count = 0;
    for (const auto& p : exportPlaylists(ids, all)) {
        if (!p.isFolder) ++count;
        for (auto id : p.trackIds) unique.insert(id);
    }
    library::TrackRepository repository(m_db);
    qint64 bytes = 0;
    int missing = 0;
    int needsWav = 0;
    for (const auto& t : repository.tracksByIds(unique.values())) {
        const bool requiresWav = useWavForFlac && t.filePath.endsWith(".flac", Qt::CaseInsensitive);
        const QFileInfo file(requiresWav ? repository.verifiedWav(t.fileHash).first : t.filePath);
        if (requiresWav && !file.isFile()) ++needsWav;
        if (!file.isFile()) ++missing;
        else bytes += file.size();
    }
    return {{"tracks", unique.size()}, {"playlists", count}, {"audioBytes", bytes}, {"missing", missing},
            {"needsWav", needsWav}};
}

QList<qint64> PlaylistController::trackIds(qint64 playlistId) const {
    for (const core::Playlist& playlist : m_playlists) {
        if (playlist.id == playlistId) {
            return playlist.trackIds;
        }
    }
    return {};
}

device::ExportLibrary PlaylistController::buildExportLibrary(const QList<qint64>& ids, bool all,
                                                            bool useWavForFlac, const QString& extensionFile) const {
    device::ExportManager manager(m_db);
    return manager.buildLibrary(exportPlaylists(ids, all), useWavForFlac, extensionFile);
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
    const auto nodes = repository.allPlaylists();
    m_playlists.clear();
    QSet<qint64> visited;
    std::function<void(qint64)> appendChildren = [&](qint64 parent) {
        for (const auto& p : nodes) if (p.parentId == parent && !visited.contains(p.id)) {
            visited.insert(p.id);
            m_playlists.append(p);
            appendChildren(p.id);
        }
    };
    appendChildren(0);
    for (const auto& p : nodes) if (!visited.contains(p.id)) m_playlists.append(p);
    m_lastError.clear();
    emit errorChanged();
    endResetModel();
    emit playlistsChanged();
}

} // namespace ui
} // namespace opendeck
