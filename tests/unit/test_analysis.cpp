#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>

#include <cmath>
#include <cstring>

#include "audio/AudioAnalysisService.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/AnalysisController.h"

using namespace opendeck;

namespace {

// Writes a mono 16 bit WAV made of short 1 kHz bursts at a known tempo. The
// onset envelope of this signal has a very clear periodicity, so it is a good
// fixture for the tempo and beat grid detectors.
bool writePulseWave(const QString& path, double bpm, double seconds) {
    constexpr int kRate = 44100;
    const int total = static_cast<int>(kRate * seconds);
    const double period = 60.0 / bpm;

    QByteArray pcm(total * 2, 0);
    for (int i = 0; i < total; ++i) {
        const double time = static_cast<double>(i) / kRate;
        const double phase = std::fmod(time, period);
        const double envelope = phase < 0.06 ? std::exp(-phase * 45.0) : 0.0;
        const double sample =
                std::sin(2.0 * M_PI * 1000.0 * time) * envelope * 22000.0;
        const auto value = static_cast<qint16>(
                qBound(-32768.0, sample, 32767.0));
        std::memcpy(pcm.data() + i * 2, &value, sizeof(value));
    }

    QByteArray wav;
    const quint32 dataSize = static_cast<quint32>(pcm.size());
    const quint32 riffSize = 36 + dataSize;
    const quint16 channels = 1, bits = 16, format = 1, blockAlign = 2;
    const quint32 rate = kRate, byteRate = kRate * 2;
    auto appendU16 = [&wav](quint16 value) {
        wav.append(char(value & 0xff));
        wav.append(char((value >> 8) & 0xff));
    };
    auto appendU32 = [&wav](quint32 value) {
        for (int i = 0; i < 4; ++i) {
            wav.append(char((value >> (8 * i)) & 0xff));
        }
    };
    wav.append("RIFF", 4);
    appendU32(riffSize);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    appendU32(16);
    appendU16(format);
    appendU16(channels);
    appendU32(rate);
    appendU32(byteRate);
    appendU16(blockAlign);
    appendU16(bits);
    wav.append("data", 4);
    appendU32(dataSize);
    wav.append(pcm);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(wav) == wav.size();
}

} // namespace

class TestAnalysis : public QObject {
    Q_OBJECT

  private slots:
    void ffmpegIsPresent() {
        QVERIFY(audio::AudioAnalysisService::ffmpegAvailable());
    }

    void detectsTempoAndBuildsBeatGrid() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/120bpm.wav");
        QVERIFY(writePulseWave(path, 120.0, 12.0));

        const audio::AnalysisResult result = audio::AudioAnalysisService::analyze(path);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY2(std::abs(result.bpm - 120.0) < 3.0,
                qPrintable(QStringLiteral("BPM detectado: %1").arg(result.bpm)));
        QVERIFY(result.beats.size() >= 20);
        // Beat numbers cycle 1..4 and the tempo is carried on every beat.
        QCOMPARE(result.beats.first().number, 1);
        QCOMPARE(result.beats.at(1).number, 2);
        QVERIFY(std::abs(result.beats.at(1).time - result.beats.first().time - 0.5) < 0.01);
        for (const core::Beat& beat : result.beats) {
            QVERIFY(std::abs(beat.tempo - result.bpm) < 0.001);
        }
    }

    void waveformHasTheRightResolution() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/wave.wav");
        QVERIFY(writePulseWave(path, 128.0, 4.0));

        const audio::AnalysisResult result = audio::AudioAnalysisService::analyze(path);
        QVERIFY(result.ok);
        const int expected = static_cast<int>(std::ceil(
                result.duration * audio::AudioAnalysisService::kWaveformColumnsPerSecond));
        QCOMPARE(result.peaks.size(), expected);

        bool hasPeaks = false;
        for (const char value : result.peaks) {
            if (static_cast<quint8>(value) > 0) {
                hasPeaks = true;
                break;
            }
        }
        QVERIFY(hasPeaks);
    }

    void keyIsInCamelotNotation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/key.wav");
        QVERIFY(writePulseWave(path, 100.0, 3.0));

        const audio::AnalysisResult result = audio::AudioAnalysisService::analyze(path);
        QVERIFY(result.ok);
        QVERIFY(!result.musicalKey.isEmpty());
        QVERIFY(result.musicalKey.endsWith(QStringLiteral("A"))
                || result.musicalKey.endsWith(QStringLiteral("B")));
    }

    void reportsTheFailureOfAnInvalidFile() {
        const audio::AnalysisResult result =
                audio::AudioAnalysisService::analyze(QStringLiteral("/does/not/exist.wav"));
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }

    void controllerPersistsResultsFromAWorkerThread() {
        if (!audio::AudioAnalysisService::ffmpegAvailable()) {
            QSKIP("ffmpeg unavailable");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/pulse.wav");
        QVERIFY(writePulseWave(path, 120.0, 10.0));

        library::DatabaseManager db(dir.path() + QStringLiteral("/library.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        library::TrackRepository repository(db);
        core::Track track;
        track.title = QStringLiteral("Pulse");
        track.filePath = path;
        track.fileHash = QStringLiteral("pulse-hash");
        const qint64 id = repository.insertTrack(track);
        QVERIFY(id > 0);

        ui::AnalysisController controller(db);
        QCOMPARE(controller.pendingCount(), 1);
        QVERIFY(!controller.running());

        QSignalSpy spy(&controller, &ui::AnalysisController::finished);
        controller.analyzeAll();
        QVERIFY(controller.running());
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() > 0, 30000);
        QVERIFY(!controller.running());
        QCOMPARE(controller.pendingCount(), 0);

        const QList<core::Track> tracks = repository.allTracks();
        QCOMPARE(tracks.size(), 1);
        QVERIFY(tracks.first().bpm > 0.0);
        QVERIFY(!tracks.first().musicalKey.isEmpty());
        QCOMPARE(tracks.first().analysisStatus, core::AnalysisStatus::Analyzed);
        QVERIFY(repository.beats(id).size() > 10);
        QVERIFY(!repository.waveform(id, 0).isEmpty());
    }
};

QTEST_MAIN(TestAnalysis)
#include "test_analysis.moc"
