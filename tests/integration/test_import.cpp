#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>

#include "library/AudioImportService.h"

using namespace opendeck;

// Integration test: generates a real MP3 with ffmpeg, then verifies that the
// importer reads its tags and computes a persistent hash. Skips when ffmpeg is
// unavailable so the suite still runs on machines without it.
class TestImport : public QObject {
    Q_OBJECT

  private slots:
    void readsMetadataFromGeneratedMp3() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/test.mp3");

        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                {QStringLiteral("-y"),
                        QStringLiteral("-f"), QStringLiteral("lavfi"),
                        QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=1"),
                        QStringLiteral("-metadata"), QStringLiteral("title=Test Song"),
                        QStringLiteral("-metadata"), QStringLiteral("artist=Test Artist"),
                        QStringLiteral("-metadata"), QStringLiteral("album=Test Album"),
                        QStringLiteral("-c:a"), QStringLiteral("libmp3lame"), path});
        if (!ffmpeg.waitForFinished(20000)) {
            QSKIP("ffmpeg unavailable; skipping import integration test");
        }

        const core::Track t = library::AudioImportService::readMetadata(path);
        QCOMPARE(t.title, QStringLiteral("Test Song"));
        QCOMPARE(t.artist, QStringLiteral("Test Artist"));
        QCOMPARE(t.album, QStringLiteral("Test Album"));
        QVERIFY(t.duration > 0.5);
        QVERIFY(t.sampleRate > 0);
        QVERIFY(!t.fileHash.isEmpty());
        QVERIFY(library::AudioImportService::isSupportedExtension(path));
    }
};

QTEST_MAIN(TestImport)
#include "test_import.moc"
