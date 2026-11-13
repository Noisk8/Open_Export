#include <QtTest>
#include <QByteArray>
#include <QHash>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QString>
#include <QTemporaryDir>
#include <QVariant>

#include <memory>

#include "core/models/track.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/CueController.h"

using namespace opendeck;

namespace {

int cueRowCount(library::DatabaseManager& db, qint64 trackId) {
    QSqlQuery q(db.database());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM track_cues WHERE track_id = :t"));
    q.bindValue(QStringLiteral(":t"), trackId);
    if (!q.exec() || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

int nullTextCount(library::DatabaseManager& db, qint64 trackId) {
    QSqlQuery q(db.database());
    q.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM track_cues WHERE track_id = :t "
            "AND (name IS NULL OR comment IS NULL)"));
    q.bindValue(QStringLiteral(":t"), trackId);
    if (!q.exec() || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

} // namespace

class TestCues : public QObject {
    Q_OBJECT

  private:
    struct Fixture {
        QTemporaryDir dir;
        std::unique_ptr<library::DatabaseManager> db;
        qint64 trackId = 0;
    };

    std::unique_ptr<Fixture> makeFixture() {
        auto f = std::make_unique<Fixture>();
        if (!f->dir.isValid()) {
            return nullptr;
        }
        f->db = std::make_unique<library::DatabaseManager>(
                f->dir.path() + QStringLiteral("/library.db"));
        QString error;
        if (!f->db->open(&error)) {
            return nullptr;
        }

        library::TrackRepository tracks(*f->db);
        core::Track track;
        track.title = QStringLiteral("Cue Track");
        track.filePath = f->dir.path() + QStringLiteral("/track.mp3");
        track.fileHash = QStringLiteral("cue-hash-1");
        track.duration = 300.0;
        f->trackId = tracks.insertTrack(track);
        if (f->trackId <= 0) {
            return nullptr;
        }
        return f;
    }

    static QVariant field(ui::CueController& controller, int row, int role) {
        return controller.data(controller.index(row, 0), role);
    }

    // Finds the row whose IdRole equals the given controller-local id.
    static int rowForId(ui::CueController& controller, qint64 id) {
        for (int row = 0; row < controller.rowCount(); ++row) {
            if (field(controller, row, ui::CueController::IdRole).toLongLong() == id) {
                return row;
            }
        }
        return -1;
    }

  private slots:
    void exposesCuesInTimeOrder() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);

        QCOMPARE(controller.property("trackId").toLongLong(), 0);
        QCOMPARE(controller.property("cueCount").toInt(), 0);
        QCOMPARE(controller.property("duration").toDouble(), 0.0);

        QSignalSpy trackSpy(&controller, &ui::CueController::trackIdChanged);
        QSignalSpy countSpy(&controller, &ui::CueController::cueCountChanged);
        controller.setTrack(f->trackId);
        QCOMPARE(controller.property("trackId").toLongLong(), f->trackId);
        QCOMPARE(controller.rowCount(), 0);
        QCOMPARE(trackSpy.count(), 1);
        QCOMPARE(countSpy.count(), 1);

        const int hot = static_cast<int>(core::CueKind::HotCue);
        const int memory = static_cast<int>(core::CueKind::MemoryCue);
        const int loop = static_cast<int>(core::CueKind::Loop);

        // Deliberately insert out of time order to exercise the sorting.
        const qint64 loopId = controller.addCue(loop, 20.0, 25.0, 0, 8,
                                                QStringLiteral("Build"), QString());
        const qint64 hotId = controller.addCue(hot, 10.0, 0.0, 1, 1,
                                               QStringLiteral("Intro"),
                                               QStringLiteral("start here"));
        const qint64 memoryId = controller.addCue(memory, 30.0, 0.0, 0, 0,
                                                  QString(), QString());
        QVERIFY(loopId > 0);
        QVERIFY(hotId > 0);
        QVERIFY(memoryId > 0);

        QCOMPARE(controller.property("cueCount").toInt(), 3);
        QCOMPARE(countSpy.count(), 4); // setTrack + three addCue calls
        QCOMPARE(cueRowCount(*f->db, f->trackId), 3);
        // Null QStrings must never reach SQLite as SQL NULL (NOT NULL columns).
        QCOMPARE(nullTextCount(*f->db, f->trackId), 0);

        // Sorted by time: hot (10), loop (20), memory (30).
        QCOMPARE(field(controller, 0, ui::CueController::IdRole).toLongLong(), hotId);
        QCOMPARE(field(controller, 0, ui::CueController::KindRole).toInt(), hot);
        QCOMPARE(field(controller, 0, ui::CueController::HotCueRole).toInt(), 1);
        QCOMPARE(field(controller, 0, ui::CueController::TimeRole).toDouble(), 10.0);
        QCOMPARE(field(controller, 0, ui::CueController::LoopTimeRole).toDouble(), 0.0);
        QCOMPARE(field(controller, 0, ui::CueController::ColorIdRole).toInt(), 1);
        QCOMPARE(field(controller, 0, ui::CueController::NameRole).toString(),
                 QStringLiteral("Intro"));
        QCOMPARE(field(controller, 0, ui::CueController::CommentRole).toString(),
                 QStringLiteral("start here"));
        QCOMPARE(field(controller, 0, ui::CueController::LabelRole).toString(),
                 QStringLiteral("Hot A"));

        QCOMPARE(field(controller, 1, ui::CueController::IdRole).toLongLong(), loopId);
        QCOMPARE(field(controller, 1, ui::CueController::KindRole).toInt(), loop);
        QCOMPARE(field(controller, 1, ui::CueController::HotCueRole).toInt(), 0);
        QCOMPARE(field(controller, 1, ui::CueController::TimeRole).toDouble(), 20.0);
        QCOMPARE(field(controller, 1, ui::CueController::LoopTimeRole).toDouble(), 25.0);
        QCOMPARE(field(controller, 1, ui::CueController::ColorIdRole).toInt(), 8);
        QCOMPARE(field(controller, 1, ui::CueController::LabelRole).toString(),
                 QStringLiteral("Loop"));

        QCOMPARE(field(controller, 2, ui::CueController::IdRole).toLongLong(), memoryId);
        QCOMPARE(field(controller, 2, ui::CueController::KindRole).toInt(), memory);
        QCOMPARE(field(controller, 2, ui::CueController::TimeRole).toDouble(), 30.0);
        QVERIFY(field(controller, 2, ui::CueController::NameRole).toString().isEmpty());
        QCOMPARE(field(controller, 2, ui::CueController::LabelRole).toString(),
                 QStringLiteral("Memory"));

        // "mm:ss.mmm" formatting.
        QCOMPARE(controller.timeLabel(0.0), QStringLiteral("00:00.000"));
        QCOMPARE(controller.timeLabel(65.5), QStringLiteral("01:05.500"));
        QCOMPARE(controller.timeLabel(125.25), QStringLiteral("02:05.250"));
    }

    void roleNamesAreQmlFriendly() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        const QHash<int, QByteArray> names = controller.roleNames();
        QCOMPARE(names.value(ui::CueController::IdRole), QByteArray("cueId"));
        QCOMPARE(names.value(ui::CueController::KindRole), QByteArray("kind"));
        QCOMPARE(names.value(ui::CueController::HotCueRole), QByteArray("hotCue"));
        QCOMPARE(names.value(ui::CueController::TimeRole), QByteArray("time"));
        QCOMPARE(names.value(ui::CueController::LoopTimeRole), QByteArray("loopTime"));
        QCOMPARE(names.value(ui::CueController::ColorIdRole), QByteArray("colorId"));
        QCOMPARE(names.value(ui::CueController::NameRole), QByteArray("name"));
        QCOMPARE(names.value(ui::CueController::CommentRole), QByteArray("comment"));
        QCOMPARE(names.value(ui::CueController::LabelRole), QByteArray("label"));
    }

    void hotCueLabelsRangeFromAToH() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        controller.setTrack(f->trackId);
        const int hot = static_cast<int>(core::CueKind::HotCue);
        for (int n = 1; n <= 8; ++n) {
            QVERIFY(controller.addCue(hot, static_cast<double>(n), 0.0, n, 0,
                                      QString(), QString()) > 0);
        }
        QCOMPARE(controller.rowCount(), 8);
        const QString letters = QStringLiteral("ABCDEFGH");
        for (int row = 0; row < 8; ++row) {
            QCOMPARE(field(controller, row, ui::CueController::LabelRole).toString(),
                     QStringLiteral("Hot %1").arg(letters.mid(row, 1)));
        }
    }

    void updateRemoveAndClear() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        controller.setTrack(f->trackId);

        const int hot = static_cast<int>(core::CueKind::HotCue);
        const int memory = static_cast<int>(core::CueKind::MemoryCue);
        const int loop = static_cast<int>(core::CueKind::Loop);

        const qint64 a = controller.addCue(hot, 10.0, 0.0, 2, 3,
                                           QStringLiteral("A"), QString());
        const qint64 b = controller.addCue(memory, 40.0, 0.0, 0, 0,
                                           QStringLiteral("B"), QString());
        QVERIFY(a > 0);
        QVERIFY(b > 0);
        QCOMPARE(controller.rowCount(), 2);

        // Update row 0 and check it keeps its id while being re-sorted.
        QVERIFY(controller.updateCue(0, loop, 15.0, 18.0, 0, 5,
                                     QStringLiteral("Looped"), QStringLiteral("edited")));
        QCOMPARE(controller.rowCount(), 2);
        const int row = rowForId(controller, a);
        QVERIFY(row >= 0);
        QCOMPARE(field(controller, row, ui::CueController::KindRole).toInt(), loop);
        QCOMPARE(field(controller, row, ui::CueController::TimeRole).toDouble(), 15.0);
        QCOMPARE(field(controller, row, ui::CueController::LoopTimeRole).toDouble(), 18.0);
        QCOMPARE(field(controller, row, ui::CueController::ColorIdRole).toInt(), 5);
        QCOMPARE(field(controller, row, ui::CueController::NameRole).toString(),
                 QStringLiteral("Looped"));
        QCOMPARE(field(controller, row, ui::CueController::CommentRole).toString(),
                 QStringLiteral("edited"));
        QCOMPARE(field(controller, row, ui::CueController::LabelRole).toString(),
                 QStringLiteral("Loop"));

        // Invalid updates are rejected and leave the model intact.
        QVERIFY(!controller.updateCue(0, loop, 15.0, 10.0, 0, 5, QString(), QString()));
        QVERIFY(!controller.updateCue(99, hot, 1.0, 0.0, 1, 0, QString(), QString()));
        QCOMPARE(controller.rowCount(), 2);

        const int bRow = rowForId(controller, b);
        QVERIFY(bRow >= 0);
        QVERIFY(controller.removeCue(bRow));
        QCOMPARE(controller.rowCount(), 1);
        QCOMPARE(cueRowCount(*f->db, f->trackId), 1);
        QVERIFY(!controller.removeCue(5));

        controller.clear();
        QCOMPARE(controller.rowCount(), 0);
        QCOMPARE(controller.property("cueCount").toInt(), 0);
        QCOMPARE(cueRowCount(*f->db, f->trackId), 0);
    }

    void changesPersistAcrossControllers() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController first(*f->db);
        first.setTrack(f->trackId);
        const int hot = static_cast<int>(core::CueKind::HotCue);
        const int loop = static_cast<int>(core::CueKind::Loop);
        QVERIFY(first.addCue(hot, 12.0, 0.0, 4, 2, QStringLiteral("Drop"),
                             QStringLiteral("cue")) > 0);
        QVERIFY(first.addCue(loop, 30.0, 34.0, 0, 6, QStringLiteral("Outro"),
                             QString()) > 0);
        QCOMPARE(cueRowCount(*f->db, f->trackId), 2);

        // A second controller over the same database must read those cues.
        ui::CueController second(*f->db);
        second.setTrack(f->trackId);
        QCOMPARE(second.property("trackId").toLongLong(), f->trackId);
        QCOMPARE(second.property("cueCount").toInt(), 2);
        QCOMPARE(field(second, 0, ui::CueController::KindRole).toInt(), hot);
        QCOMPARE(field(second, 0, ui::CueController::TimeRole).toDouble(), 12.0);
        QCOMPARE(field(second, 0, ui::CueController::HotCueRole).toInt(), 4);
        QCOMPARE(field(second, 0, ui::CueController::NameRole).toString(),
                 QStringLiteral("Drop"));
        QCOMPARE(field(second, 1, ui::CueController::KindRole).toInt(), loop);
        QCOMPARE(field(second, 1, ui::CueController::TimeRole).toDouble(), 30.0);
        QCOMPARE(field(second, 1, ui::CueController::LoopTimeRole).toDouble(), 34.0);

        // Clearing on one controller is visible to the other after a reload.
        first.clear();
        QCOMPARE(first.rowCount(), 0);
        second.setTrack(f->trackId);
        QCOMPARE(second.rowCount(), 0);
        QCOMPARE(cueRowCount(*f->db, f->trackId), 0);
    }

    void setTrackZeroEmptiesModel() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        controller.setTrack(f->trackId);
        QVERIFY(controller.addCue(static_cast<int>(core::CueKind::HotCue), 5.0, 0.0, 1, 0,
                                  QString(), QString()) > 0);
        QCOMPARE(controller.rowCount(), 1);

        QSignalSpy trackSpy(&controller, &ui::CueController::trackIdChanged);
        controller.setTrack(0);
        QCOMPARE(controller.rowCount(), 0);
        QCOMPARE(controller.property("trackId").toLongLong(), 0);
        QCOMPARE(trackSpy.count(), 1);
        // setTrack only reads: the database is left untouched.
        QCOMPARE(cueRowCount(*f->db, f->trackId), 1);
    }

    void rejectsInvalidParameters() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        controller.setTrack(f->trackId);

        const int hot = static_cast<int>(core::CueKind::HotCue);
        const int memory = static_cast<int>(core::CueKind::MemoryCue);
        const int loop = static_cast<int>(core::CueKind::Loop);

        QVERIFY(controller.addCue(hot, -1.0, 0.0, 1, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(loop, 20.0, 10.0, 0, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(hot, 1.0, 0.0, 0, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(hot, 1.0, 0.0, 9, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(memory, 1.0, 0.0, 1, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(loop, 1.0, 2.0, 3, 0, QString(), QString()) == 0);
        QVERIFY(controller.addCue(memory, 1.0, 0.0, 0, -1, QString(), QString()) == 0);
        QVERIFY(controller.addCue(memory, 1.0, 0.0, 0, 9, QString(), QString()) == 0);
        QVERIFY(controller.addCue(42, 1.0, 0.0, 0, 0, QString(), QString()) == 0);

        QCOMPARE(controller.rowCount(), 0);
        QCOMPARE(cueRowCount(*f->db, f->trackId), 0);

        // Boundary values are accepted.
        QVERIFY(controller.addCue(hot, 0.0, 0.0, 8, 8, QString(), QString()) > 0);
        QVERIFY(controller.addCue(loop, 5.0, 5.0, 0, 0, QString(), QString()) > 0);
        QVERIFY(controller.addCue(memory, 2.0, 0.0, 0, 0, QString(), QString()) > 0);
        QCOMPARE(controller.rowCount(), 3);
        QCOMPARE(field(controller, 0, ui::CueController::LabelRole).toString(),
                 QStringLiteral("Hot H"));
    }

    void durationProperty() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::CueController controller(*f->db);
        QSignalSpy spy(&controller, &ui::CueController::durationChanged);

        controller.setDuration(180.5);
        QCOMPARE(controller.property("duration").toDouble(), 180.5);
        QCOMPARE(spy.count(), 1);

        // The WRITE accessor is reachable through the metaobject too.
        QVERIFY(controller.setProperty("duration", 240.0));
        QCOMPARE(controller.duration(), 240.0);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestCues)
#include "test_cues.moc"
