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

#include <taglib/tpropertymap.h>
#include <taglib/flacfile.h>
#include <taglib/wavfile.h>

#include "ui/ConversionController.h"

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
