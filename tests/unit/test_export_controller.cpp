#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "export/ExportValidator.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"
#include "ui/ExportController.h"
#include "ui/PlaylistController.h"

using namespace opendeck;

namespace {

// Writes a large but uninteresting file so the copy takes long enough for the
// test to observe the GUI thread while the worker is running.
bool writeFakeAudio(const QString& path, int megabytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray block(1024 * 1024, 0);
    for (int i = 0; i < megabytes; ++i) {
        if (file.write(block) != block.size()) {
            return false;
        }
    }
    return true;
}

} // namespace

class TestExportController : public QObject {
    Q_OBJECT

  private:
    struct Fixture {
        QTemporaryDir dir;
        std::unique_ptr<library::DatabaseManager> db;
        std::unique_ptr<ui::PlaylistController> playlists;
        QString target;
    };

    std::unique_ptr<Fixture> makeFixture(int tracks, int megabytesEach) {
        auto f = std::make_unique<Fixture>();
        if (!f->dir.isValid()) {
            return nullptr;
        }
        f->db = std::make_unique<library::DatabaseManager>(f->dir.path()
                + QStringLiteral("/library.db"));
        QString error;
        if (!f->db->open(&error)) {
            return nullptr;
        }

        library::TrackRepository repository(*f->db);
        QList<qint64> ids;
        for (int i = 0; i < tracks; ++i) {
            const QString path = f->dir.path()
                    + QStringLiteral("/track%1.wav").arg(i);
            if (!writeFakeAudio(path, megabytesEach)) {
                return nullptr;
            }
            core::Track track;
            track.title = QStringLiteral("Track %1").arg(i);
            track.artist = QStringLiteral("Artist");
            track.filePath = path;
            track.fileHash = QStringLiteral("hash-%1").arg(i);
            const qint64 id = repository.insertTrack(track);
            if (id <= 0) {
                return nullptr;
            }
            ids.append(id);
        }

        library::PlaylistRepository playlists(*f->db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main");
        playlist.trackIds = ids;
        if (playlists.savePlaylist(playlist) <= 0) {
            return nullptr;
        }

        f->playlists = std::make_unique<ui::PlaylistController>(*f->db);
        f->target = f->dir.path() + QStringLiteral("/usb");
        if (!QDir().mkpath(f->target)) {
            return nullptr;
        }
        return f;
    }

  private slots:
    void exportRunsWithoutBlockingTheGuiThread() {
        auto f = makeFixture(2, 24); // 48 MB to copy
        QVERIFY(f != nullptr);

        ui::ExportController exporter(*f->playlists);
        QSignalSpy finishedSpy(&exporter, &ui::ExportController::finished);
        QSignalSpy progressSpy(&exporter, &ui::ExportController::progressChanged);

        // A timer on the GUI thread must keep firing while the export runs.
        // If the export were synchronous this would never tick.
        int ticks = 0;
        QTimer ticker;
        ticker.setInterval(10);
        QObject::connect(&ticker, &QTimer::timeout, this, [&ticks]() { ++ticks; });
        ticker.start();

        exporter.exportTo(f->target);
        QVERIFY(exporter.running()); // returns immediately, work is elsewhere
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 60000);
        ticker.stop();

        QVERIFY(!exporter.running());
        QCOMPARE(exporter.progress(), 100);
        QVERIFY2(ticks > 0, "the GUI thread was blocked during the export");
        QVERIFY(progressSpy.count() > 0);

        const QList<QVariant> args = finishedSpy.takeFirst();
        QVERIFY2(args.at(0).toBool(), qPrintable(args.at(1).toString()));
        QStringList issues;
        QVERIFY2(device::ExportValidator::validateExport(f->target, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));
    }

    void cancelStopsTheExport() {
        auto f = makeFixture(3, 48); // 144 MB
        QVERIFY(f != nullptr);

        ui::ExportController exporter(*f->playlists);
        QSignalSpy finishedSpy(&exporter, &ui::ExportController::finished);
        exporter.exportTo(f->target);
        QVERIFY(exporter.running());
        exporter.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0 || !exporter.running(), 10000);
        QVERIFY(!exporter.running());
    }

    void rejectsAnInvalidDestination() {
        auto f = makeFixture(1, 1);
        QVERIFY(f != nullptr);
        ui::ExportController exporter(*f->playlists);
        QSignalSpy finishedSpy(&exporter, &ui::ExportController::finished);
        exporter.exportTo(f->target + QStringLiteral("/missing"));
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(!exporter.running());
        QVERIFY(!finishedSpy.takeFirst().at(0).toBool());
    }
};

QTEST_MAIN(TestExportController)
#include "test_export_controller.moc"
