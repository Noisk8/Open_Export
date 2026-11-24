#pragma once

#include <QAbstractListModel>
#include <QAtomicInteger>
#include <QThread>
#include <QList>
#include <QUrl>

#include "core/models/track.h"

namespace opendeck {
namespace library {
class DatabaseManager;
}
namespace ui {

// LibraryController exposes the local track library to QML as a list model and
// implements import (drag & drop, folders). It is the thin boundary between the
// UI and the library/audio layers.
class LibraryController : public QAbstractListModel {
    Q_OBJECT
    // QML binds to this as a property; a Q_INVOKABLE method read without
    // parentheses evaluates to a JS function, not to the number.
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QString importStatus READ importStatus NOTIFY importingChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY trackCountChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY missingCountChanged)
  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        GenreRole,
        YearRole,
        DurationRole,
        BpmRole,
        KeyRole,
        AnalysisStatusRole,
        FilePathRole,
    };
    Q_ENUM(Roles)

    explicit LibraryController(library::DatabaseManager& db, QObject* parent = nullptr);

    ~LibraryController() override;
    bool importing() const { return m_importing; }
    QString importStatus() const { return m_importStatus; }
    Q_INVOKABLE void importAsync(const QList<QUrl>& urls, qint64 playlistId = 0);
    Q_INVOKABLE void cancelImport();

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Import entry points (called from QML). Returns the number of imported
    // tracks; skipped duplicates are not counted.
    Q_INVOKABLE int importFiles(const QList<QUrl>& urls);
    Q_INVOKABLE int importFolder(const QString& path);
    Q_INVOKABLE int trackCount() const;
    // Id of the track at a given row of the model, for playlist operations.
    Q_INVOKABLE qint64 trackIdAt(int row) const;
    // Reloads the model, for example after the analysis updated the metadata.
    Q_INVOKABLE void refresh();

    // Tracks whose audio file is no longer at the stored path.
    Q_INVOKABLE int missingCount() const;
    // Searches the mounted volumes for the missing files and repairs the paths.
    // Returns how many tracks were found again.
    Q_INVOKABLE int relocateMissing();
    Q_INVOKABLE QList<qint64> missingTrackIds();
    Q_INVOKABLE int removeMissing(const QList<qint64>& ids);

  signals:
    void importingChanged();
    void importProgress(int current, int total);
    void playlistTracksImported(qint64 playlistId, const QList<qint64>& ids);
    void importFinished(int imported, int skipped);
    void trackCountChanged();
    void missingCountChanged();
    void tracksRemoved();

  private:
    void reload();
    void collectAudioFiles(const QString& dir, QStringList& out);

    library::DatabaseManager& m_db;
    QList<core::Track> m_tracks;
    int m_missingCount = 0;
    QThread* m_importThread = nullptr;
    QAtomicInteger<bool> m_cancelImport{false};
    bool m_importing = false;
    QString m_importStatus;
};

} // namespace ui
} // namespace opendeck
