#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "core/models/playlist.h"
#include "export/ExportManager.h"
#include "export/ExportManifest.h"
#include "export/ExportPublication.h"
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
    void cancelledCopyPreservesExistingAudio() {
        auto f = makeFixture();
        QVERIFY(f);
        auto library = build(*f->db);
        const auto item = library.tracks.first();
        QVERIFY(writeFakeAudio(item.track.filePath, 3 * 1024 * 1024));
        const QString destination = f->target + item.devicePath;
        QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
        QFile original(destination);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("previous audio");
        original.close();
        device::ExportOptions options;
        options.destination = f->target;
        options.writeArtwork = false;
        int polls = 0;
        device::ExportCallbacks callbacks;
        callbacks.cancelled = [&] {
            return QDir(f->target + "/.open-deck-export/stage/Contents").exists()
                    && ++polls >= 2;
        };
        QString error;
        const auto result = device::ExportManager::write(library, options, &error, callbacks);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(result.cancelled);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("previous audio"));
    }

    void publicationRollsBackAllFilesOnValidationFailure() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        const QString path = QStringLiteral("/Contents/track.mp3");
        const QString added = QStringLiteral("/Contents/new.mp3");
        QVERIFY(QDir().mkpath(root + "/Contents"));
        QVERIFY(writeFakeAudio(root + path, 123));
        device::ExportPublication publication(root);
        QVERIFY(QDir().mkpath(publication.stage() + "/Contents"));
        QVERIFY(writeFakeAudio(publication.stage() + path, 456));
        QVERIFY(writeFakeAudio(publication.stage() + added, 789));

        bool installed = false;
        QVERIFY_EXCEPTION_THROWN(publication.publish({}, [&] {
            installed = QFileInfo(root + path).size() == 456 && QFile::exists(root + added);
            throw std::runtime_error("simulated validation failure");
        }), std::runtime_error);
        QVERIFY(installed);
        QCOMPARE(QFileInfo(root + path).size(), 123);
        QVERIFY(!QFile::exists(root + added));
    }

    void recoversInterruptedPublicationBeforeWritingAgain() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path();
        const QString work = root + "/.open-deck-export";
        QVERIFY(QDir().mkpath(work + "/backup/Contents"));
        QVERIFY(QDir().mkpath(root + "/Contents"));
        const QString path = QStringLiteral("/Contents/track.mp3");
        QVERIFY(writeFakeAudio(work + "/owner", 0));
        QVERIFY(writeFakeAudio(work + "/backup" + path, 123));
        QVERIFY(writeFakeAudio(root + path, 456));
        QVERIFY(writeFakeAudio(root + "/Contents/new.mp3", 456));
        QFile journal(work + "/journal.json");
        QVERIFY(journal.open(QIODevice::WriteOnly));
        QVERIFY(journal.write(R"([{"path":"/Contents/track.mp3","previous":true,"install":true},
                                  {"path":"/Contents/new.mp3","previous":false,"install":true}])") > 0);
        journal.close();

        device::ExportPublication publication(root);
        QCOMPARE(QFileInfo(root + path).size(), 123);
        QVERIFY(!QFile::exists(root + "/Contents/new.mp3"));
        QVERIFY(!QFile::exists(journal.fileName()));
    }

    void repairsSameSizeChangesOnSourceAndDestination() {
        auto f = makeFixture();
        QVERIFY(f);
        QString error;
        QCOMPARE(exportTo(*f->db, f->target, false, &error).trackCount, 3);
        const auto item = build(*f->db).tracks.first();
        QFile source(item.track.filePath);
        QVERIFY(source.open(QIODevice::ReadWrite));
        QCOMPARE(source.write("x", 1), 1);
        source.close();
        auto result = exportTo(*f->db, f->target, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.skippedTracks, 2);

        QFile copy(f->target + item.devicePath);
        QVERIFY(copy.open(QIODevice::ReadWrite));
        QCOMPARE(copy.read(1), QByteArray("x"));
        QVERIFY(copy.seek(0));
        QCOMPARE(copy.write("y", 1), 1);
        copy.close();
        QFile::remove(f->target + item.analyzePath);
        result = exportTo(*f->db, f->target, true, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(result.skippedTracks, 2);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        QCOMPARE(copy.read(1), QByteArray("x"));
        QVERIFY(QFile::exists(f->target + item.analyzePath));
    }

    void pathsAreUniqueIgnoringCase() {
        auto f = makeFixture();
        QVERIFY(f);
        library::TrackRepository repository(*f->db);
        auto tracks = repository.allTracks();
        tracks[0].title = "Intro";
        tracks[1].title = "intro";
        tracks[2].title = "INTRO";
        for (const auto& track : tracks) QVERIFY(repository.updateTrack(track));
        QSet<QString> paths;
        for (const auto& item : build(*f->db).tracks) paths.insert(item.devicePath.toCaseFolded());
        QCOMPARE(paths.size(), 3);
    }

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
