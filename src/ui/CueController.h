#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include "core/models/track.h"

namespace opendeck {
namespace library {
class DatabaseManager;
}
namespace ui {

// CueController exposes the cues (hot cues, memory cues and loops) of a single
// track to QML as a list model and keeps them in sync with the local library.
//
// The cues table has no stable per-row id that survives a save: replaceCues()
// deletes and reinserts every row and cues() does not return the autoincrement
// id. The controller therefore assigns a small, controller-local id to each cue
// (exposed through IdRole and returned by addCue). Editing operations take a
// row instead, which is what a QML delegate naturally has available.
class CueController : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(qint64 trackId READ trackId NOTIFY trackIdChanged)
    Q_PROPERTY(int cueCount READ rowCount NOTIFY cueCountChanged)
    Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY durationChanged)
  public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        KindRole,
        HotCueRole,
        TimeRole,
        LoopTimeRole,
        ColorIdRole,
        NameRole,
        CommentRole,
        LabelRole,
    };
    Q_ENUM(Roles)

    explicit CueController(library::DatabaseManager& db, QObject* parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Q_PROPERTY accessors
    qint64 trackId() const;
    double duration() const;
    void setDuration(double duration);

    // Loads the cues of a track into the model. A non-positive id empties the
    // model without touching the database.
    Q_INVOKABLE void setTrack(qint64 trackId);

    // Adds a cue, persists it and returns its controller-local id; returns 0
    // when the parameters are invalid or the write fails.
    Q_INVOKABLE qint64 addCue(int kind, double time, double loopTime, int hotCueNumber,
                              int colorId, const QString& name, const QString& comment);
    // Replaces the cue at a row; false for an invalid row or invalid parameters.
    Q_INVOKABLE bool updateCue(int row, int kind, double time, double loopTime,
                               int hotCueNumber, int colorId, const QString& name,
                               const QString& comment);
    Q_INVOKABLE bool removeCue(int row);
    Q_INVOKABLE void clear();

    // Human readable "mm:ss.mmm" label for a position in seconds.
    Q_INVOKABLE QString timeLabel(double seconds) const;

  signals:
    void trackIdChanged();
    void cueCountChanged();
    void durationChanged();

  private:
    struct Entry {
        qint64 id = 0;
        core::Cue cue;
    };

    static void sortEntries(QList<Entry>& entries);
    bool isValid(int kind, double time, double loopTime, int hotCueNumber,
                 int colorId) const;
    core::Cue makeCue(int kind, double time, double loopTime, int hotCueNumber,
                      int colorId, const QString& name, const QString& comment) const;
    QString labelFor(const core::Cue& cue) const;
    bool persist(const QList<Entry>& entries);
    bool commit(QList<Entry> entries);

    library::DatabaseManager& m_db;
    qint64 m_trackId = 0;
    double m_duration = 0.0;
    qint64 m_nextId = 1;
    QList<Entry> m_entries;
};

} // namespace ui
} // namespace opendeck
