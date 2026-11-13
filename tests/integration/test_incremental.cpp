#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "core/models/playlist.h"
#include "export/ExportManager.h"
#include "export/ExportManifest.h"
#include "export/ExportValidator.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"

using namespace opendeck;

namespace {

bool writeFakeAudio(const QString& path, int bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(QByteArray(bytes, 0)) == bytes;
}

} // namespace

class TestIncremental : public QObject {
    Q_OBJECT

  private:
    struct Fixture {
        QTemporaryDir dir;
        std::unique_ptr<library::DatabaseManager> db;
        QString target;
    };

    std::unique_ptr<Fixture> makeFixture() {
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
        library::PlaylistRepository playlists(*f->db);
        QList<qint64> ids;
        for (int i = 1; i <= 3; ++i) {
            const QString path = f->dir.path() + QStringLiteral("/t%1.mp3").arg(i);
            if (!writeFakeAudio(path, 4096 * i)) {
                return nullptr;
            }
            core::Track track;
            track.title = QStringLiteral("Track %1").arg(i);
            track.artist = QStringLiteral("Artist");
            track.filePath = path;
            track.fileHash = QStringLiteral("hash-%1").arg(i);
            track.bpm = 120.0 + i;
            const qint64 id = repository.insertTrack(track);
            if (id <= 0) {
                return nullptr;
            }
            ids.append(id);
        }
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main");
        playlist.trackIds = ids;
        if (playlists.savePlaylist(playlist) <= 0) {
            return nullptr;
        }
        f->target = f->dir.path() + QStringLiteral("/usb");
        if (!QDir().mkpath(f->target)) {
            return nullptr;
        }
        return f;
    }

    device::ExportLibrary build(library::DatabaseManager& db) {
        library::PlaylistRepository repository(db);
        device::ExportManager manager(db);
        return manager.buildLibrary(repository.allPlaylists());
    }

    device::ExportReport exportTo(library::DatabaseManager& db, const QString& target,
                                  bool incremental, QString* error) {
        device::ExportManager manager(db);
        device::ExportOptions options;
        options.destination = target;
        options.incremental = incremental;
        options.writeArtwork = false;
        return manager.write(build(db), options, error);
    }

  private slots:
    void secondExportReusesUnchangedFiles() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        QString error;

        const device::ExportReport first = exportTo(*f->db, f->target, false, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(first.trackCount, 3);
        QCOMPARE(first.skippedTracks, 0);
        QCOMPARE(first.analysisCount, 3);
        QVERIFY(QFile::exists(device::ExportManifest::pathIn(f->target)));
        QVERIFY(QFile::exists(f->target + QStringLiteral(
                "/PIONEER/rekordbox/exportLibrary.db")));

        const device::ExportReport second = exportTo(*f->db, f->target, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(second.trackCount, 3);
        QCOMPARE(second.skippedTracks, 3);
        QCOMPARE(second.analysisCount, 0);
        QCOMPARE(second.removedTracks, 0);

        QStringList issues;
        QVERIFY2(device::ExportValidator::validateExport(f->target, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));
    }

    void aChangedTrackIsReexported() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        QString error;
        QVERIFY(exportTo(*f->db, f->target, false, &error).trackCount == 3);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        library::TrackRepository repository(*f->db);
        const QList<core::Track> tracks = repository.allTracks();
        QCOMPARE(tracks.size(), 3);
        core::Track changed = tracks.last();
        changed.bpm = 140.0;
        QVERIFY(repository.updateTrack(changed));

        const device::ExportReport report = exportTo(*f->db, f->target, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(report.skippedTracks, 2);
        QCOMPARE(report.analysisCount, 1);
    }

    void removedTracksDeleteTheirFiles() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        QString error;
        QVERIFY(exportTo(*f->db, f->target, false, &error).trackCount == 3);

        library::PlaylistRepository playlists(*f->db);
        QList<core::Playlist> all = playlists.allPlaylists();
        QCOMPARE(all.size(), 1);
        QVERIFY(!all.first().trackIds.isEmpty());
        const device::ExportLibrary before = build(*f->db);
        QCOMPARE(before.tracks.size(), 3);
        const QString removedPath = f->target + before.tracks.last().devicePath;
        all[0].trackIds.removeLast();
        QVERIFY(playlists.savePlaylist(all.first()) > 0);

        const device::ExportReport report = exportTo(*f->db, f->target, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(report.trackCount, 2);
        QCOMPARE(report.removedTracks, 1);
        QVERIFY(!QFile::exists(removedPath));

        const device::ExportManifest manifest =
                device::ExportManifest::load(f->target, &error);
        QVERIFY(manifest.valid);
        QCOMPARE(manifest.tracks.size(), 2);
    }

    void manifestDetectsContentChanges() {
        device::ExportLibrary library = makeLibraryForHash();
        const QString before = device::ExportManifest::trackHash(library.tracks.first());
        QVERIFY(!before.isEmpty());
        library.tracks[0].track.bpm += 1.0;
        QVERIFY(device::ExportManifest::trackHash(library.tracks.first()) != before);
        library.tracks[0].track.bpm -= 1.0;
        QCOMPARE(device::ExportManifest::trackHash(library.tracks.first()), before);
        library.tracks[0].peaks.append(char(1));
        QVERIFY(device::ExportManifest::trackHash(library.tracks.first()) != before);
        QVERIFY(device::ExportManifest::libraryHash(library)
                != device::ExportManifest::libraryHash(device::ExportLibrary()));
    }

  private:
    static device::ExportLibrary makeLibraryForHash() {
        device::ExportLibrary library;
        device::ExportTrack item;
        item.deviceId = 1;
        item.track.title = QStringLiteral("Track");
        item.track.bpm = 128.0;
        item.devicePath = QStringLiteral("/Contents/a.mp3");
        item.peaks = QByteArray(16, 0);
        library.tracks.append(item);
        device::ExportPlaylist playlist;
        playlist.id = 1;
        playlist.name = QStringLiteral("Main");
        playlist.trackIds = {1};
        library.playlists.append(playlist);
        return library;
    }
};

QTEST_MAIN(TestIncremental)
#include "test_incremental.moc"
