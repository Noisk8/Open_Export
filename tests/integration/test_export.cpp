#include <QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>

#include "core/models/playlist.h"
#include "audio/AudioAnalysisService.h"
#include "export/ExportManager.h"
#include "export/ExportValidator.h"
#include "export/onelibrary/DeviceLibraryWriter.h"
#include "export/devicesql/DeviceSqlReader.h"
#include "library/AudioImportService.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"

using namespace opendeck;

namespace {

quint32 bigU32(const QByteArray& data, int offset) {
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
            | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

QByteArray anlzSection(const QByteArray& file, const char* fourcc) {
    int offset = static_cast<int>(bigU32(file, 4));
    const quint32 end = bigU32(file, 8);
    while (offset + 12 <= static_cast<int>(end)) {
        const quint32 length = bigU32(file, offset + 8);
        if (length < 12) {
            break;
        }
        if (file.mid(offset, 4) == QByteArray(fourcc, 4)) {
            return file.mid(offset + 12, static_cast<int>(length) - 12);
        }
        offset += static_cast<int>(length);
    }
    return {};
}

} // namespace

// Integration test: generates a real MP3 with ffmpeg, imports it, builds a
// playlist and exports everything to a folder. Then it checks that the result
// is the layout that a Pioneer player expects.
//
// It skips when ffmpeg is unavailable so the suite still runs on machines
// without it.
class TestExport : public QObject {
    Q_OBJECT

  private slots:
    void exportsAPlaylistThatAPlayerCanRead() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // --- A real audio file ----------------------------------------------
        const QString audioPath = dir.path() + QStringLiteral("/song.mp3");
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=3"),
                        QStringLiteral("-metadata"), QStringLiteral("title=Song One"),
                        QStringLiteral("-metadata"), QStringLiteral("artist=The Artist"),
                        QStringLiteral("-metadata"), QStringLiteral("album=An Album"),
                        QStringLiteral("-metadata"), QStringLiteral("genre=House"),
                        QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), audioPath});
        if (!ffmpeg.waitForFinished(30000)) {
            QSKIP("ffmpeg unavailable; skipping the export integration test");
        }
        QVERIFY2(QFile::exists(audioPath), "ffmpeg did not produce the audio file");

        // --- The local library ----------------------------------------------
        const QString dbPath = dir.path() + QStringLiteral("/library.db");
        library::DatabaseManager db(dbPath);
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        library::TrackRepository tracks(db);
        core::Track track = library::AudioImportService::readMetadata(audioPath);
        track.bpm = 128.0;
        track.musicalKey = QStringLiteral("8A");
        const qint64 trackId = tracks.insertTrack(track);
        QVERIFY(trackId > 0);

        library::PlaylistRepository playlists(db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main Room");
        playlist.trackIds = {trackId};
        const qint64 playlistId = playlists.savePlaylist(playlist);
        QVERIFY(playlistId > 0);

        // --- The export ------------------------------------------------------
        const QString target = dir.path() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(target));

        device::ExportManager manager(db);
        const QList<core::Playlist> allPlaylists = playlists.allPlaylistsWithEntries();
        QCOMPARE(allPlaylists.size(), 1);
        const device::ExportLibrary library = manager.buildLibrary(allPlaylists);
        QCOMPARE(library.tracks.size(), 1);
        QCOMPARE(library.playlists.size(), 1);

        device::ExportOptions options;
        options.destination = target;
        const device::ExportReport report = manager.write(library, options, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(report.trackCount, 1);
        QCOMPARE(report.playlistCount, 1);
        QCOMPARE(report.analysisCount, 1);

        // --- The layout ------------------------------------------------------
        QVERIFY(QFile::exists(target + QStringLiteral("/PIONEER/rekordbox/export.pdb")));
        QVERIFY(QFileInfo(target + QStringLiteral("/Contents")).isDir());

        // --- The database ----------------------------------------------------
        QFile database(target + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
        QVERIFY(database.open(QIODevice::ReadOnly));
        const QByteArray data = database.readAll();
        database.close();

        QStringList issues;
        QVERIFY2(device::ExportValidator::validateDatabase(data, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));
        QVERIFY2(device::ExportValidator::validateExport(target, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));

        const device::DeviceSqlReader reader(data);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.rows(0).size(), 1);
        QCOMPARE(reader.rows(7).size(), 1);
        QCOMPARE(reader.rows(8).size(), 1);
        QCOMPARE(device::DeviceSqlReader::readString(reader.rows(7).first(), 20),
                QStringLiteral("Main Room"));

        // --- The analysis ----------------------------------------------------
        const QString analyzePath = device::DeviceSqlReader::readSlotString(
                reader.rows(0).first(), 0x5e + 2 * 14);
        QVERIFY(!analyzePath.isEmpty());
        QVERIFY(QFile::exists(target + analyzePath));

        auto mismatched = library;
        mismatched.playlists[0].name = QStringLiteral("Different set");
        QVERIFY2(device::onelibrary::DeviceLibraryWriter::write(
                dir.filePath("different.db"), mismatched, QStringLiteral("2026-01-01"), &error),
                qPrintable(error));
        const QString oneLibrary = target + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db");
        QVERIFY(QFile::remove(oneLibrary));
        QVERIFY(QFile::rename(dir.filePath("different.db"), oneLibrary));
        issues.clear();
        QVERIFY(!device::ExportValidator::validateExport(target, &issues));
        QVERIFY(issues.join(' ').contains(QStringLiteral("sets distintos")));
    }

    void analysisReachesTheAnlzFiles() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // A WAV with a clear 120 BPM pulse train, so the detector has a
        // deterministic answer and the beat grid is non-empty.
        const QString audioPath = dir.path() + QStringLiteral("/pulse.wav");
        constexpr int kRate = 44100;
        const int frames = kRate * 8;
        QByteArray pcm(frames * 2, 0);
        for (int i = 0; i < frames; ++i) {
            const double time = static_cast<double>(i) / kRate;
            const double phase = std::fmod(time, 0.5);
            const double envelope = phase < 0.06 ? std::exp(-phase * 45.0) : 0.0;
            const auto value = static_cast<qint16>(
                    std::sin(2.0 * M_PI * 1000.0 * time) * envelope * 22000.0);
            std::memcpy(pcm.data() + i * 2, &value, sizeof(value));
        }
        QByteArray wav;
        auto u16 = [&wav](quint16 v) {
            wav.append(char(v & 0xff)); wav.append(char((v >> 8) & 0xff));
        };
        auto u32 = [&wav](quint32 v) {
            for (int i = 0; i < 4; ++i) wav.append(char((v >> (8 * i)) & 0xff));
        };
        wav.append("RIFF", 4); u32(36 + pcm.size()); wav.append("WAVE", 4);
        wav.append("fmt ", 4); u32(16); u16(1); u16(1); u32(kRate);
        u32(kRate * 2); u16(2); u16(16);
        wav.append("data", 4); u32(pcm.size()); wav.append(pcm);
        QFile audio(audioPath);
        QVERIFY(audio.open(QIODevice::WriteOnly));
        QCOMPARE(audio.write(wav), wav.size());
        audio.close();

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        library::TrackRepository tracks(db);
        core::Track track = library::AudioImportService::readMetadata(audioPath);
        track.title = QStringLiteral("Pulse");
        const qint64 trackId = tracks.insertTrack(track);
        QVERIFY(trackId > 0);

        // This is exactly what AnalysisController persists on the GUI thread.
        const audio::AnalysisResult result = audio::AudioAnalysisService::analyze(audioPath);
        QVERIFY2(result.ok, qPrintable(result.error));
        track.id = trackId;
        track.bpm = result.bpm;
        track.musicalKey = result.musicalKey;
        track.duration = result.duration;
        track.analysisStatus = core::AnalysisStatus::Analyzed;
        QVERIFY(tracks.updateTrack(track));
        QVERIFY(tracks.replaceBeats(trackId, result.beats));
        QVERIFY(tracks.storeWaveform(trackId, 0, result.peaks));

        library::PlaylistRepository playlists(db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Analysed");
        playlist.trackIds = {trackId};
        QVERIFY(playlists.savePlaylist(playlist) > 0);

        const QString target = dir.path() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(target));
        device::ExportManager manager(db);
        device::ExportOptions options;
        options.destination = target;
        QVERIFY2(manager.write(manager.buildLibrary(playlists.allPlaylists()), options, &error)
                         .trackCount >= 0, qPrintable(error));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QFile database(target + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
        QVERIFY(database.open(QIODevice::ReadOnly));
        const device::DeviceSqlReader reader(database.readAll());
        database.close();

        // The BPM and the key now live in the track row.
        const QList<QByteArray> rows = reader.rows(0);
        QCOMPARE(rows.size(), 1);
        auto littleU32 = [](const QByteArray& row, int offset) {
            return static_cast<quint32>(static_cast<quint8>(row.at(offset)))
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 1))) << 8)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 2))) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 3))) << 24);
        };
        QCOMPARE(littleU32(rows.first(), 0x38), static_cast<quint32>(qRound(result.bpm * 100)));

        const QString analyzePath =
                device::DeviceSqlReader::readSlotString(rows.first(), 0x5e + 2 * 14);
        QVERIFY(QFile::exists(target + analyzePath));

        QFile dat(target + analyzePath);
        QVERIFY(dat.open(QIODevice::ReadOnly));
        const QByteArray datBytes = dat.readAll();
        dat.close();

        const QByteArray grid = anlzSection(datBytes, "PQTZ");
        QVERIFY(!grid.isEmpty());
        QVERIFY(bigU32(grid, 8) > 0); // the beat grid has beats
        const QByteArray preview = anlzSection(datBytes, "PWAV");
        QVERIFY(!preview.isEmpty());
        bool hasPeaks = false;
        for (int i = 8; i < preview.size(); ++i) {
            if (preview.at(i) != 0) {
                hasPeaks = true;
                break;
            }
        }
        QVERIFY(hasPeaks);
    }

    void reportsAnUnusableDestination() {
        QTemporaryDir dir;        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        device::ExportManager manager(db);
        device::ExportOptions options;
        options.destination = dir.path() + QStringLiteral("/does-not-exist");
        const device::ExportReport report = manager.write({}, options, &error);
        QVERIFY(!error.isEmpty());
        QCOMPARE(report.trackCount, 0);
    }

    void keepsTheFolderHierarchy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString audioPath = dir.path() + QStringLiteral("/song.mp3");
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=1"),
                        QStringLiteral("-metadata"), QStringLiteral("title=Song"),
                        QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), audioPath});
        if (!ffmpeg.waitForFinished(30000)) {
            QSKIP("ffmpeg unavailable; skipping the export integration test");
        }

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        library::TrackRepository tracks(db);
        const qint64 trackId = tracks.insertTrack(
                library::AudioImportService::readMetadata(audioPath));
        QVERIFY(trackId > 0);

        library::PlaylistRepository playlists(db);
        // A folder without tracks of its own, with a playlist inside it.
        core::Playlist folder;
        folder.name = QStringLiteral("Warm Up");
        folder.isFolder = true;
        const qint64 folderId = playlists.savePlaylist(folder);
        QVERIFY(folderId > 0);

        core::Playlist nested;
        nested.name = QStringLiteral("Deep");
        nested.parentId = folderId;
        nested.trackIds = {trackId};
        const qint64 nestedId = playlists.savePlaylist(nested);
        QVERIFY(nestedId > 0);

        const QString target = dir.path() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(target));

        device::ExportManager manager(db);
        const device::ExportLibrary library = manager.buildLibrary(playlists.allPlaylists());
        QCOMPARE(library.playlists.size(), 2);

        device::ExportOptions options;
        options.destination = target;
        QVERIFY2(manager.write(library, options, &error).trackCount >= 0, qPrintable(error));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QFile database(target + QStringLiteral("/PIONEER/rekordbox/export.pdb"));
        QVERIFY(database.open(QIODevice::ReadOnly));
        const QByteArray data = database.readAll();
        database.close();

        QStringList issues;
        QVERIFY2(device::ExportValidator::validateDatabase(data, &issues),
                qPrintable(issues.join(QStringLiteral("; "))));

        const device::DeviceSqlReader reader(data);
        const QList<QByteArray> nodes = reader.rows(7);
        QCOMPARE(nodes.size(), 2);
        // The folder keeps its flag and the nested playlist points at it.
        auto u32 = [](const QByteArray& row, int offset) {
            return static_cast<quint32>(static_cast<quint8>(row.at(offset)))
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 1))) << 8)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 2))) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 3))) << 24);
        };
        const bool folderIsFirst = u32(nodes.at(0), 0x10) == 1;
        const int folderIndex = folderIsFirst ? 0 : 1;
        const int nestedIndex = folderIsFirst ? 1 : 0;
        QCOMPARE(u32(nodes.at(folderIndex), 0x10), 1u); // it is a folder
        QCOMPARE(u32(nodes.at(nestedIndex), 0x00), u32(nodes.at(folderIndex), 0x0c));
    }
};

QTEST_MAIN(TestExport)
#include "test_export.moc"
