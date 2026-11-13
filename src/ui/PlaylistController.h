#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QVariant>

#include "core/models/playlist.h"
#include "export/ExportModel.h"

namespace opendeck {
namespace library {
class DatabaseManager;
}

namespace ui {

// PlaylistController exposes the playlists of the local library to QML and
// runs the export to a Pioneer media.
//
// It is the boundary between the UI and the library and export layers; all
// the heavy lifting happens in those layers, which can be tested without a
// GUI.
class PlaylistController : public QAbstractListModel {
    Q_OBJECT
    // Properties, not methods: QML bindings need values that reevaluate when
    // the model changes, and a Q_INVOKABLE read without parentheses is a JS
    // function object instead of the count.
    Q_PROPERTY(int playlistCount READ playlistCount NOTIFY playlistsChanged)
    Q_PROPERTY(int totalTrackCount READ totalTrackCount NOTIFY playlistsChanged)
  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TrackCountRole,
        IsFolderRole,
        ParentIdRole,
    };
    Q_ENUM(Roles)

    explicit PlaylistController(library::DatabaseManager& db, QObject* parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Playlist management.
    Q_INVOKABLE int playlistCount() const;
    // Total number of tracks across every playlist.
    Q_INVOKABLE int totalTrackCount() const;
    Q_INVOKABLE qint64 createPlaylist(const QString& name, qint64 parentId = 0);
    Q_INVOKABLE bool removePlaylist(qint64 playlistId);
    Q_INVOKABLE bool addTrack(qint64 playlistId, qint64 trackId);
    Q_INVOKABLE QList<qint64> trackIds(qint64 playlistId) const;

    // Writes an export of every playlist that holds tracks to the given
    // directory, and validates what it wrote. Returns an empty string on
    // success, or a description of the problem.
    // Builds the description of the export on the caller's thread. The result
    // is self contained (the analysis data is already loaded), so the actual
    // write can be handed to a worker thread.
    [[nodiscard]] device::ExportLibrary buildExportLibrary() const;

    // Synchronous export, kept for tests and headless callers. The UI uses
    // ExportController, which does the same work off the GUI thread.
    Q_INVOKABLE QString exportTo(const QVariant& destination);
    Q_INVOKABLE QString validate(const QVariant& destination);

    // Converts a path or a QUrl coming from QML into a local file path.
    [[nodiscard]] static QString localPath(const QVariant& value);

  signals:
    void playlistsChanged();
    void exportFinished(bool success, const QString& message);

  private:
    void reload();

    library::DatabaseManager& m_db;
    QList<core::Playlist> m_playlists;
};

} // namespace ui
} // namespace opendeck
