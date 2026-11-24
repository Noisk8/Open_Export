#pragma once

#include <QSortFilterProxyModel>
#include <QSet>
#include "ui/LibraryController.h"
#include "ui/PlaylistController.h"

namespace opendeck::ui {
// Keeps the full library intact for analysis while browsing playlist contents.
class TrackFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(qint64 playlistId READ playlistId WRITE setPlaylistId NOTIFY filterChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged)
    Q_PROPERTY(QList<qint64> selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    TrackFilterModel(LibraryController& library, PlaylistController& playlists)
        : m_playlists(playlists) {
        setSourceModel(&library);
        connect(&playlists, &PlaylistController::playlistsChanged, this, [this] { refresh(); });
        connect(this, &QAbstractItemModel::modelReset, this, &TrackFilterModel::countChanged);
        connect(this, &QAbstractItemModel::rowsInserted, this, &TrackFilterModel::countChanged);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &TrackFilterModel::countChanged);
        sort(0);
    }
    qint64 playlistId() const { return m_playlistId; }
    QString search() const { return m_search; }
    int count() const { return rowCount(); }
    void setPlaylistId(qint64 id) { m_playlistId = id; clearSelection(); refresh(); }
    QList<qint64> selectedIds() const {
        QList<qint64> result;
        for (int row = 0; row < rowCount(); ++row) {
            const auto id = index(row, 0).data(LibraryController::IdRole).toLongLong();
            if (m_selected.contains(id)) result.append(id);
        }
        return result;
    }
    Q_INVOKABLE void toggleSelection(qint64 id) {
        if (m_selected.contains(id)) m_selected.remove(id); else m_selected.insert(id);
        emit selectionChanged();
    }
    Q_INVOKABLE void clearSelection() { m_selected.clear(); emit selectionChanged(); }
    Q_INVOKABLE void selectVisible() {
        m_selected.clear();
        for (int row = 0; row < rowCount(); ++row) m_selected.insert(index(row, 0).data(LibraryController::IdRole).toLongLong());
        emit selectionChanged();
    }
    Q_INVOKABLE void sortBy(const QString& field, bool descending = false) {
        const QHash<QString, int> roles{{"title", LibraryController::TitleRole}, {"artist", LibraryController::ArtistRole},
            {"bpm", LibraryController::BpmRole}, {"key", LibraryController::KeyRole}, {"duration", LibraryController::DurationRole}};
        m_sortField = roles.value(field, 0);
        sort(0, descending ? Qt::DescendingOrder : Qt::AscendingOrder);
        invalidate();
    }
    void setSearch(const QString& value) { m_search = value.trimmed(); clearSelection(); refresh(); }
signals:
    void selectionChanged();
    void filterChanged();
    void countChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const auto idx = sourceModel()->index(row, 0, parent);
        const auto id = idx.data(LibraryController::IdRole).toLongLong();
        if (m_playlistId > 0 && !m_positions.contains(id)) return false;
        for (int role : {LibraryController::TitleRole, LibraryController::ArtistRole,
                        LibraryController::AlbumRole, LibraryController::GenreRole}) {
            if (idx.data(role).toString().contains(m_search, Qt::CaseInsensitive)) return true;
        }
        return false;
    }
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        if (m_sortField) {
            const QVariant a = left.data(m_sortField), b = right.data(m_sortField);
            if (m_sortField == LibraryController::BpmRole || m_sortField == LibraryController::DurationRole)
                return a.toDouble() < b.toDouble();
            return QString::localeAwareCompare(a.toString().toCaseFolded(), b.toString().toCaseFolded()) < 0;
        }
        if (m_playlistId > 0)
            return m_positions.value(left.data(LibraryController::IdRole).toLongLong())
                 < m_positions.value(right.data(LibraryController::IdRole).toLongLong());
        return left.row() < right.row();
    }
private:
    void refresh() {
        m_positions.clear();
        const auto ids = m_playlists.trackIds(m_playlistId);
        for (int i = 0; i < ids.size(); ++i) if (!m_positions.contains(ids[i])) m_positions.insert(ids[i], i);
        invalidate();
        emit filterChanged();
        emit countChanged();
        emit selectionChanged();
    }
    PlaylistController& m_playlists;
    QHash<qint64, int> m_positions;
    qint64 m_playlistId = 0;
    QString m_search;
    QSet<qint64> m_selected;
    int m_sortField = 0;
};
}
