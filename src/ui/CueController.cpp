#include "ui/CueController.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"

namespace opendeck {
namespace ui {

namespace {

// A default constructed QString is null and the SQLite driver binds it as SQL
// NULL, which the NOT NULL columns reject. Normalise to an empty, non-null
// string, mirroring the helper in TrackRepository.cpp.
QString text(const QString& value) {
    return value.isNull() ? QString::fromUtf8("") : value;
}

} // namespace

CueController::CueController(library::DatabaseManager& db, QObject* parent)
        : QAbstractListModel(parent), m_db(db) {
}

int CueController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant CueController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry& entry = m_entries.at(index.row());
    const core::Cue& cue = entry.cue;
    switch (role) {
    case IdRole: return entry.id;
    case KindRole: return static_cast<int>(cue.kind);
    case HotCueRole: return cue.hotCueNumber;
    case TimeRole: return cue.time;
    case LoopTimeRole: return cue.loopTime;
    case ColorIdRole: return cue.colorId;
    case NameRole: return cue.name;
    case CommentRole: return cue.comment;
    case LabelRole: return labelFor(cue);
    default: return {};
    }
}

QHash<int, QByteArray> CueController::roleNames() const {
    return {
            {IdRole, "cueId"},
            {KindRole, "kind"},
            {HotCueRole, "hotCue"},
            {TimeRole, "time"},
            {LoopTimeRole, "loopTime"},
            {ColorIdRole, "colorId"},
            {NameRole, "name"},
            {CommentRole, "comment"},
            {LabelRole, "label"},
    };
}

qint64 CueController::trackId() const {
    return m_trackId;
}

double CueController::duration() const {
    return m_duration;
}

void CueController::setDuration(double duration) {
    if (m_duration == duration) {
        return;
    }
    m_duration = duration;
    emit durationChanged();
}

void CueController::setTrack(qint64 trackId) {
    const bool changed = (m_trackId != trackId);
    m_trackId = trackId;

    QList<core::Cue> cues;
    if (trackId > 0) {
        library::TrackRepository repository(m_db);
        cues = repository.cues(trackId);
    }

    QList<Entry> entries;
    entries.reserve(cues.size());
    for (const core::Cue& cue : cues) {
        core::Cue normalized = cue;
        normalized.name = text(normalized.name);
        normalized.comment = text(normalized.comment);
        entries.append(Entry{m_nextId++, std::move(normalized)});
    }
    sortEntries(entries);

    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();

    if (changed) {
        emit trackIdChanged();
    }
    emit cueCountChanged();
}

qint64 CueController::addCue(int kind, double time, double loopTime, int hotCueNumber,
                             int colorId, const QString& name, const QString& comment) {
    if (!isValid(kind, time, loopTime, hotCueNumber, colorId)) {
        return 0;
    }
    const qint64 id = m_nextId;
    QList<Entry> next = m_entries;
    next.append(
            Entry{id, makeCue(kind, time, loopTime, hotCueNumber, colorId, name, comment)});
    sortEntries(next);
    if (!commit(std::move(next))) {
        return 0;
    }
    ++m_nextId;
    return id;
}

bool CueController::updateCue(int row, int kind, double time, double loopTime,
                              int hotCueNumber, int colorId, const QString& name,
                              const QString& comment) {
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }
    if (!isValid(kind, time, loopTime, hotCueNumber, colorId)) {
        return false;
    }
    QList<Entry> next = m_entries;
    // Keep the controller-local id of the edited cue.
    next[row].cue = makeCue(kind, time, loopTime, hotCueNumber, colorId, name, comment);
    sortEntries(next);
    return commit(std::move(next));
}

bool CueController::removeCue(int row) {
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }
    QList<Entry> next = m_entries;
    next.removeAt(row);
    return commit(std::move(next));
}

void CueController::clear() {
    commit(QList<Entry>{});
}

QString CueController::timeLabel(double seconds) const {
    const double clamped = (std::isfinite(seconds) && seconds > 0.0) ? seconds : 0.0;
    const qint64 totalMs = static_cast<qint64>(clamped * 1000.0 + 0.5);
    const qint64 minutes = totalMs / 60000;
    const qint64 secs = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    const auto zeroPad = [](qint64 value, int width) {
        return QString::number(value).rightJustified(width, QLatin1Char('0'));
    };
    return QStringLiteral("%1:%2.%3")
            .arg(zeroPad(minutes, 2))
            .arg(zeroPad(secs, 2))
            .arg(zeroPad(ms, 3));
}

QString CueController::labelFor(const core::Cue& cue) const {
    switch (cue.kind) {
    case core::CueKind::HotCue:
        if (cue.hotCueNumber >= 1 && cue.hotCueNumber <= 8) {
            return QStringLiteral("Hot %1").arg(
                    QStringLiteral("ABCDEFGH").mid(cue.hotCueNumber - 1, 1));
        }
        return QStringLiteral("Hot");
    case core::CueKind::MemoryCue:
        return QStringLiteral("Memory");
    case core::CueKind::Loop:
        return QStringLiteral("Loop");
    }
    return QStringLiteral("Cue");
}

bool CueController::isValid(int kind, double time, double loopTime,
                            int hotCueNumber, int colorId) const {
    const core::CueKind cueKind = static_cast<core::CueKind>(kind);
    switch (cueKind) {
    case core::CueKind::HotCue:
    case core::CueKind::MemoryCue:
    case core::CueKind::Loop:
        break;
    default:
        return false;
    }

    if (!std::isfinite(time) || time < 0.0) {
        return false;
    }
    if (cueKind == core::CueKind::Loop && (!std::isfinite(loopTime) || loopTime < time)) {
        return false;
    }
    if (colorId < 0 || colorId > 8) {
        return false;
    }
    if (cueKind == core::CueKind::HotCue) {
        if (hotCueNumber < 1 || hotCueNumber > 8) {
            return false;
        }
    } else if (hotCueNumber != 0) {
        return false;
    }
    return true;
}

core::Cue CueController::makeCue(int kind, double time, double loopTime,
                                 int hotCueNumber, int colorId,
                                 const QString& name, const QString& comment) const {
    core::Cue cue;
    cue.kind = static_cast<core::CueKind>(kind);
    cue.hotCueNumber = hotCueNumber;
    cue.time = time;
    cue.loopTime = loopTime;
    cue.colorId = colorId;
    cue.name = text(name);
    cue.comment = text(comment);
    return cue;
}

void CueController::sortEntries(QList<Entry>& entries) {
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry& a, const Entry& b) { return a.cue.time < b.cue.time; });
}

bool CueController::persist(const QList<Entry>& entries) {
    if (m_trackId <= 0) {
        // No track selected: keep the model in memory only, there is nothing
        // to write for a non-existent track id.
        return true;
    }
    QList<core::Cue> cues;
    cues.reserve(entries.size());
    for (const Entry& entry : entries) {
        cues.append(entry.cue);
    }
    library::TrackRepository repository(m_db);
    return repository.replaceCues(m_trackId, cues);
}

bool CueController::commit(QList<Entry> entries) {
    if (!persist(entries)) {
        return false;
    }
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit cueCountChanged();
    return true;
}

} // namespace ui
} // namespace opendeck
