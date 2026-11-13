#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "core/models/playlist.h"
#include "export/ExportManager.h"
#include "export/ExportValidator.h"
#include "export/devicesql/DeviceSqlReader.h"
#include "library/DatabaseManager.h"
#include "library/LibraryRelocator.h"
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

core::Track makeTrack(const QString& path, const QString& hash) {
    core::Track track;
    track.title = QFileInfo(path).completeBaseName();
    track.artist = QStringLiteral("Artist");
    track.filePath = path;
    track.fileHash = hash;
    return track;
}

} // namespace

class TestMissingSources : public QObject {
    Q_OBJECT

  private slots:
    void missingTracksAreExcludedAndTheExportStaysValid() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString good = dir.path() + QStringLiteral("/good.mp3");
        const QString dead = dir.path() + QStringLiteral("/gone.mp3");
        QVERIFY(writeFakeAudio(good, 2048));

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        library::TrackRepository repository(db);
        const qint64 goodId = repository.insertTrack(makeTrack(good, QStringLiteral("h1")));
        const qint64 deadId = repository.insertTrack(makeTrack(dead, QStringLiteral("h2")));
        QVERIFY(goodId > 0 && deadId > 0);

        library::PlaylistRepository playlists(db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main");
        playlist.trackIds = {goodId, deadId};
        QVERIFY(playlists.savePlaylist(playlist) > 0);

        device::ExportManager manager(db);
        const device::ExportLibrary library =
                manager.buildLibrary(playlists.allPlaylists());
        QCOMPARE(library.tracks.size(), 2);

        const QString target = dir.path() + QStringLiteral("/usb");
        QVERIFY(QDir().mkpath(target));
        device::ExportOptions options;
        options.destination = target;
        options.writeArtwork = false;
        const device::ExportReport report = manager.write(library, options, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(report.trackCount, 1);
        QCOMPARE(report.missingTracks, 1);
        QCOMPARE(report.analysisCount, 1);

        QStringList issues;
        QVERIFY2(device::ExportValidator::validateExport(target, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));

        // The dead track must not appear in the database or in the playlist.
        QFile database(target + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
        QVERIFY(database.open(QIODevice::ReadOnly));
        const device::DeviceSqlReader reader(database.readAll());
        database.close();
        QCOMPARE(reader.rows(0).size(), 1);
        QCOMPARE(reader.rows(8).size(), 1);
    }

    void relocatorFindsAFileUnderANewMountPoint() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldRoot = dir.path() + QStringLiteral("/oldlabel");
        const QString newRoot = dir.path() + QStringLiteral("/newlabel");
        QVERIFY(QDir().mkpath(oldRoot + QStringLiteral("/music")));
        QVERIFY(QDir().mkpath(newRoot + QStringLiteral("/music")));
        const QString real = newRoot + QStringLiteral("/music/song.mp3");
        QVERIFY(writeFakeAudio(real, 4096));

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        library::TrackRepository repository(db);
        const QString deadPath = oldRoot + QStringLiteral("/music/song.mp3");
        const qint64 id = repository.insertTrack(makeTrack(deadPath, QStringLiteral("h")));
        QVERIFY(id > 0);

        const library::RelocationReport report =
                library::LibraryRelocator::relocate(db, &error, {newRoot});
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(report.checked, 1);
        QCOMPARE(report.relocated, 1);

        const QList<core::Track> tracks = repository.allTracks();
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks.first().filePath, real);
    }

    void relocatorReportsWhenNothingIsFound() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        library::TrackRepository repository(db);
        QVERIFY(repository.insertTrack(
                makeTrack(dir.path() + QStringLiteral("/missing.mp3"),
                        QStringLiteral("h"))) > 0);

        const library::RelocationReport report =
                library::LibraryRelocator::relocate(db, &error, {dir.path()});
        QCOMPARE(report.checked, 1);
        QCOMPARE(report.relocated, 0);
        QVERIFY(report.summary().contains(QStringLiteral("no encontrado")));
    }
};

QTEST_MAIN(TestMissingSources)
#include "test_missing_sources.moc"
