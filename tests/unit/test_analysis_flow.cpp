#include <QtTest>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QModelIndex>
#include <QTemporaryDir>
#include <QUrl>

#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/AnalysisController.h"
#include "ui/LibraryController.h"

using namespace opendeck;

namespace {

// A short, real WAV so the analysis worker has something to decode.
bool makeWav(const QString& path) {
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
            {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-f"), QStringLiteral("lavfi"),
                    QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=3"),
                    QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path});
    if (!ffmpeg.waitForFinished(30000)) {
        return false;
    }
    return ffmpeg.exitStatus() == QProcess::NormalExit && ffmpeg.exitCode() == 0
            && QFile::exists(path);
}

} // namespace

// Regression test for the import -> analyse -> export flow. The analysis
// controller must notice tracks that were imported AFTER the application
// started, otherwise the "Analyse library" button stays disabled forever.
class TestAnalysisFlow : public QObject {
    Q_OBJECT

  private slots:
    void newImportsBecomePending() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/new.wav");
        QVERIFY(makeWav(wav));

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        ui::LibraryController library(db);
        ui::AnalysisController analysis(db);
        analysis.bindToLibrary(library);
        QCOMPARE(library.trackCount(), 0);
        QCOMPARE(analysis.pendingCount(), 0);

        QSignalSpy spy(&analysis, &ui::AnalysisController::pendingCountChanged);
        const int imported = library.importFiles({QUrl::fromLocalFile(wav)});
        QCOMPARE(imported, 1);
        QCOMPARE(library.trackCount(), 1);
        // This is the bug: without a connection to the library, pendingCount
        // stays at 0 and QML never enables the analyse button.
        QCOMPARE(analysis.pendingCount(), 1);
        QVERIFY(spy.count() > 0);
        QVERIFY(analysis.statusText().contains(QStringLiteral("pendiente")));
    }

    void analysisRollbackKeepsPendingState() {
        QTemporaryDir dir;
        library::DatabaseManager db(dir.filePath("library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        library::TrackRepository tracks(db);
        core::Track track;
        track.fileHash = "test-hash";
        track.filePath = dir.filePath("audio.wav");
        track.id = tracks.insertTrack(track);
        QVERIFY(track.id > 0);
        QVERIFY(tracks.replaceBeats(track.id, {{1, 0.1, 120.0}}));
        QSqlQuery query(db.database());
        QVERIFY(query.exec("CREATE TRIGGER fail_waveform BEFORE INSERT ON track_waveform "
                           "BEGIN SELECT RAISE(ABORT, 'simulated failure'); END"));
        track.analysisStatus = core::AnalysisStatus::Analyzed;
        track.bpm = 120;
        QVERIFY(!tracks.saveAnalysis(track, {{1, 0.2, 120.0}}, QByteArray("peaks")));
        const auto saved = tracks.tracksByIds({track.id}).first();
        QCOMPARE(saved.analysisStatus, core::AnalysisStatus::NotAnalyzed);
        QCOMPARE(saved.bpm, 0);
        QCOMPARE(tracks.beats(track.id).first().time, 0.1);
    }

    void analysingNewImportsClearsThemAndUpdatesTheLibrary() {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
            QSKIP("ffmpeg unavailable");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString wav = dir.path() + QStringLiteral("/new.wav");
        QVERIFY(makeWav(wav));

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        ui::LibraryController library(db);
        ui::AnalysisController analysis(db);
        analysis.bindToLibrary(library);

        QCOMPARE(library.importFiles({QUrl::fromLocalFile(wav)}), 1);
        QCOMPARE(analysis.pendingCount(), 1);

        QSignalSpy finishedSpy(&analysis, &ui::AnalysisController::finished);
        library::TrackRepository tracks(db);
        core::Track other;
        other.fileHash = "other-track";
        other.filePath = dir.filePath("missing.wav");
        QVERIFY(tracks.insertTrack(other) > 0);
        analysis.refresh();
        QCOMPARE(analysis.pendingCount(), 2);
        const qint64 selected = tracks.unanalyzedTracks().first().id;
        analysis.analyzeTracks({selected});
        QVERIFY(analysis.running());
        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 60000);
        QVERIFY(!analysis.running());
        QCOMPARE(analysis.pendingCount(), 1);
        QCOMPARE(finishedSpy.first().at(1).toInt(), 0);

        // The library model must show the newly computed BPM and status.
        const int bpmRole = ui::LibraryController::BpmRole;
        const int statusRole = ui::LibraryController::AnalysisStatusRole;
        library.refresh();
        for (int row = 0; row < library.rowCount(); ++row) {
            const auto index = library.index(row);
            if (library.data(index, ui::LibraryController::IdRole).toLongLong() != selected) continue;
            QVERIFY(library.data(index, bpmRole).toDouble() > 0.0);
            QCOMPARE(library.data(index, statusRole).toInt(), 2);
            return;
        }
        QFAIL("Selected track missing from library");
    }
};

QTEST_MAIN(TestAnalysisFlow)
#include "test_analysis_flow.moc"
