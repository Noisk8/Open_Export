#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QThread>
#include <QTimer>

#include <taglib/tpropertymap.h>
#include <taglib/flacfile.h>
#include <taglib/wavfile.h>

#include "ui/ConversionController.h"
#include "export/ExportManager.h"
#include "library/AudioImportService.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"

using namespace opendeck;

namespace {

bool makeFlac(const QString& path) {
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
            {QStringLiteral("-y"),
                    QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-f"), QStringLiteral("lavfi"),
                    QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=2"),
                    QStringLiteral("-c:a"), QStringLiteral("flac"),
                    QStringLiteral("-sample_fmt"), QStringLiteral("s16"),
                    QStringLiteral("-metadata"), QStringLiteral("title=Test Title"),
                    QStringLiteral("-metadata"), QStringLiteral("artist=Test Artist"),
                    QStringLiteral("-metadata"), QStringLiteral("album=Test Album"),
                    QStringLiteral("-metadata"), QStringLiteral("genre=House"),
                    QStringLiteral("-metadata"), QStringLiteral("date=2024"),
                    QStringLiteral("-metadata"), QStringLiteral("comment=A comment"),
                    QStringLiteral("-metadata"), QStringLiteral("ISRC=ESABC1234567"),
                    path});
    if (!ffmpeg.waitForFinished(30000)) {
        return false;
    }
    return ffmpeg.exitStatus() == QProcess::NormalExit && ffmpeg.exitCode() == 0
            && QFileInfo::exists(path);
}

} // namespace

class TestConversion : public QObject {
    Q_OBJECT

  private slots:
    void convertsFlacToWavWithoutLossAndKeepsMetadata() {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
            QSKIP("ffmpeg unavailable");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString flac = dir.path() + QStringLiteral("/source.flac");
        QVERIFY(makeFlac(flac));

        ui::ConversionController converter;
        converter.setOutputDirectory(dir.path());
        converter.addFiles({QUrl::fromLocalFile(flac)});
        QCOMPARE(converter.rowCount(), 1);
        QVERIFY(!converter.running());

        QSignalSpy verified(&converter, &ui::ConversionController::wavVerified);
        QSignalSpy spy(&converter, &ui::ConversionController::finished);
        converter.start();
        QVERIFY(converter.running());
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 60000);
        QVERIFY(!converter.running());

        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 1); // succeeded
        QCOMPARE(args.at(1).toInt(), 0); // failed

        const QString wav = dir.path() + QStringLiteral("/source.wav");
        QVERIFY(QFileInfo::exists(wav));
        QVERIFY(QFileInfo(wav).size() > QFileInfo(flac).size());

        // The controller reports a bit-exact conversion.
        const QModelIndex index = converter.index(0);
        QCOMPARE(converter.data(index, ui::ConversionController::StatusRole).toInt(), 2);
        QVERIFY(converter.data(index, ui::ConversionController::LosslessRole).toBool());
        QCOMPARE(converter.data(index, ui::ConversionController::SampleRateRole).toInt(), 44100);
        QCOMPARE(converter.data(index, ui::ConversionController::ChannelsRole).toInt(), 1);
        QCOMPARE(converter.data(index, ui::ConversionController::BitDepthRole).toInt(), 16);
        QVERIFY(converter.data(index, ui::ConversionController::TagCountRole).toInt() >= 5);

        // Read the tags back from the WAV with TagLib.
        const QByteArray wavPath = wav.toLocal8Bit();
        TagLib::RIFF::WAV::File file(wavPath.constData());
        QVERIFY(file.isValid());
        const TagLib::PropertyMap properties = file.properties();
        QVERIFY(properties.contains("TITLE"));
        QCOMPARE(QString::fromUtf8(properties["TITLE"].front().toCString(true)),
                QStringLiteral("Test Title"));
        QVERIFY(properties.contains("ARTIST"));
        QCOMPARE(QString::fromUtf8(properties["ARTIST"].front().toCString(true)),
                QStringLiteral("Test Artist"));
        QVERIFY(properties.contains("ALBUM"));
        QVERIFY(properties.contains("GENRE"));
        QVERIFY(properties.contains("DATE") || properties.contains("YEAR"));
        QVERIFY(properties.contains("ISRC"));
        const auto importedWav = library::AudioImportService::readMetadata(wav);
        QVERIFY(!importedWav.isrc.isEmpty());

        library::DatabaseManager db(dir.filePath("library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        library::TrackRepository tracks(db);
        const qint64 trackId = tracks.insertTrack(library::AudioImportService::readMetadata(flac));
        QVERIFY(trackId > 0);
        core::Cue cue;
        cue.hotCueNumber = 1;
        cue.time = 0.1;
        QVERIFY(tracks.replaceCues(trackId, {cue}));
        QCOMPARE(verified.count(), 1);
        const auto association = verified.first();
        QCOMPARE(association[1].toString(), wav);
        QVERIFY(tracks.registerVerifiedWav(association[0].toString(), wav, association[2].toString()));
        core::Playlist playlist;
        playlist.id = 1;
        playlist.name = "Set";
        playlist.trackIds = {trackId};
        device::ExportManager exporter(db);
        const auto set = exporter.buildLibrary({playlist}, true);
        QCOMPARE(set.tracks.first().track.id, trackId);
        QCOMPARE(set.tracks.first().track.filePath, wav);
        QCOMPARE(set.tracks.first().track.isrc, importedWav.isrc);
        QCOMPARE(set.tracks.first().cues.first().time, cue.time);
        QCOMPARE(set.playlists.first().trackIds, QList<quint32>{1});
        device::ExportOptions options;
        options.destination = dir.filePath("usb");
        options.writeDeviceLibrary = false;
        options.xdjRxProfile = true;
        QVERIFY(QDir().mkpath(options.destination));
        QCOMPARE(exporter.write(set, options, &error).trackCount, 1);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QFile changedWav(wav);
        QVERIFY(changedWav.open(QIODevice::Append));
        QCOMPARE(changedWav.write("x", 1), 1);
        changedWav.close();
        QCOMPARE(exporter.write(set, options, &error).trackCount, 0);
        QVERIFY(error.contains("WAV verificado"));
        QVERIFY(QFile::exists(options.destination + set.tracks.first().devicePath));

    }

    void preservesExistingWavAndResolvesBatchCollisions() {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + "/a"));
        QVERIFY(QDir().mkpath(dir.path() + "/b"));
        const QString a = dir.path() + "/a/song.flac";
        const QString b = dir.path() + "/b/song.flac";
        QVERIFY(makeFlac(a));
        QVERIFY(QFile::copy(a, b));
        QFile existing(dir.path() + "/song.wav");
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("original wav bytes");
        existing.close();
        ui::ConversionController converter;
        converter.setOutputDirectory(dir.path());
        converter.addFiles({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)});
        const QString targetA = converter.data(converter.index(0), ui::ConversionController::TargetRole).toString();
        const QString targetB = converter.data(converter.index(1), ui::ConversionController::TargetRole).toString();
        QVERIFY(targetA != targetB);
        QVERIFY(targetA != existing.fileName());
        QSignalSpy finished(&converter, &ui::ConversionController::finished);
        converter.start();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
        QCOMPARE(finished.first()[0].toInt(), 2);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("original wav bytes"));
        QVERIFY(QFileInfo::exists(targetA));
        QVERIFY(QFileInfo::exists(targetB));
        QCOMPARE(QDir(dir.path()).entryList({".ode-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void lateCollisionDoesNotOverwriteOrLeaveTemporary() {
        QTemporaryDir dir;
        const QString source = dir.path() + "/song.flac";
        const QString target = dir.path() + "/song.wav";
        QVERIFY(makeFlac(source));
        QFile existing(target);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("keep me");
        existing.close();
        ui::ConversionWorker worker;
        worker.setJobs({{source, target}});
        QSignalSpy done(&worker, &ui::ConversionWorker::itemFinished);
        worker.run();
        QCOMPARE(done.size(), 1);
        QVERIFY(!qvariant_cast<ui::ConversionOutcome>(done.first()[1]).ok);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("keep me"));
        QCOMPARE(QDir(dir.path()).entryList({".ode-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void cancellationStopsAnActiveProcess() {
        QTemporaryDir dir;
        const QString source = dir.path() + "/song.flac";
        QVERIFY(makeFlac(source));
        QFile fake(dir.path() + "/ffmpeg");
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\nexec /bin/sleep 60\n");
        fake.close();
        QVERIFY(fake.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", dir.path().toLocal8Bit());
        ui::ConversionController converter;
        converter.addFiles({QUrl::fromLocalFile(source)});
        QSignalSpy done(&converter, &ui::ConversionController::finished);
        converter.start();
        QTimer::singleShot(150, &converter, &ui::ConversionController::cancel);
        const bool stopped = done.wait(3000);
        qputenv("PATH", oldPath);
        QVERIFY(stopped);
        QVERIFY(!converter.running());
        QVERIFY(!QFileInfo::exists(dir.path() + "/song.wav"));
        QCOMPARE(QDir(dir.path()).entryList({".ode-*"}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void rejectsNonFlacFiles() {
        ui::ConversionController converter;
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/notaudio.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("hello");
        file.close();
        converter.addFiles({QUrl::fromLocalFile(path)});
        QCOMPARE(converter.rowCount(), 0);
    }

    void deduplicatesAndRemoves() {
        QTemporaryDir dir;
        const QString a = dir.path() + QStringLiteral("/a.flac");
        const QString b = dir.path() + QStringLiteral("/b.flac");
        QVERIFY(makeFlac(a));
        QVERIFY(makeFlac(b));
        ui::ConversionController converter;
        converter.addFiles({QUrl::fromLocalFile(a), QUrl::fromLocalFile(a),
                QUrl::fromLocalFile(b)});
        QCOMPARE(converter.rowCount(), 2);
        converter.removeAt(0);
        QCOMPARE(converter.rowCount(), 1);
        converter.clear();
        QCOMPARE(converter.rowCount(), 0);
    }
};

QTEST_MAIN(TestConversion)
#include "test_conversion.moc"
