#include "audio/AudioAnalysisService.h"

#include <QByteArray>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <vector>

namespace opendeck {
namespace audio {

namespace {

constexpr int kFftSize = 4096;
constexpr int kHopSize = 512; // ~23.2 ms at 22050 Hz
constexpr int kChromaSeconds = 120; // the key is estimated from the first two minutes
constexpr int kChromaHop = kFftSize * 2; // non-overlapping windows keep the FFT cost low

// Krumhansl-Schmuckler key profiles, starting at C.
constexpr double kMajorProfile[12] = {
        6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr double kMinorProfile[12] = {
        6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

// Camelot wheel. Index 0 is C, 1 is C#, … 11 is B.
constexpr const char* kMajorCamelot[12] = {
        "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B"};
constexpr const char* kMinorCamelot[12] = {
        "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A"};

// In-place iterative radix-2 FFT.
void fft(std::vector<std::complex<float>>& data) {
    const int n = static_cast<int>(data.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * M_PI / static_cast<double>(len);
        const std::complex<float> step(static_cast<float>(std::cos(angle)),
                static_cast<float>(std::sin(angle)));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0F, 0.0F);
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<float> u = data[i + k];
                const std::complex<float> v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= step;
            }
        }
    }
}

// Decodes the file to mono 32 bit float PCM with ffmpeg.
std::vector<float> decode(const QString& filePath, QString* error) {
    const QString program =
            QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (program.isEmpty()) {
        *error = QStringLiteral("No se encontró ffmpeg en el PATH");
        return {};
    }

    QProcess process;
    process.start(program,
            {QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-nostdin"),
                    QStringLiteral("-i"), filePath,
                    QStringLiteral("-map"), QStringLiteral("0:a:0"),
                    QStringLiteral("-vn"),
                    QStringLiteral("-ac"), QStringLiteral("1"),
                    QStringLiteral("-ar"), QString::number(AudioAnalysisService::kSampleRate),
                    QStringLiteral("-f"), QStringLiteral("f32le"),
                    QStringLiteral("-")});
    if (!process.waitForFinished(180000)) {
        *error = QStringLiteral("ffmpeg tardó demasiado o no se pudo ejecutar");
        process.kill();
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QStringLiteral("ffmpeg no pudo decodificar el archivo: %1")
                         .arg(QString::fromUtf8(process.readAllStandardError()).trimmed());
        return {};
    }

    const QByteArray raw = process.readAllStandardOutput();
    if (raw.size() < static_cast<int>(sizeof(float)) * 1024) {
        *error = QStringLiteral("El audio decodificado es demasiado corto");
        return {};
    }
    std::vector<float> samples(raw.size() / static_cast<int>(sizeof(float)));
    std::memcpy(samples.data(), raw.constData(), samples.size() * sizeof(float));
    return samples;
}

// Positive spectral flux of the RMS energy: a cheap onset envelope.
std::vector<double> onsetEnvelope(const std::vector<float>& samples) {
    const int frames = static_cast<int>(samples.size()) / kHopSize;
    std::vector<double> envelope(static_cast<size_t>(std::max(0, frames)), 0.0);
    double previous = 0.0;

    for (int frame = 0; frame < frames; ++frame) {
        double sum = 0.0;
        const int start = frame * kHopSize;
        for (int i = 0; i < kHopSize; ++i) {
            const double value = samples[static_cast<size_t>(start + i)];
            sum += value * value;
        }
        const double rms = std::sqrt(sum / kHopSize);
        envelope[static_cast<size_t>(frame)] = std::max(0.0, rms - previous);
        previous = rms;
    }

    // Normalise so that autocorrelations of different tracks are comparable.
    double mean = 0.0;
    for (const double value : envelope) {
        mean += value;
    }
    mean /= std::max<size_t>(1, envelope.size());
    double variance = 0.0;
    for (double& value : envelope) {
        value -= mean;
        variance += value * value;
    }
    const double deviation = std::sqrt(variance / std::max<size_t>(1, envelope.size()));
    if (deviation > 1e-12) {
        for (double& value : envelope) {
            value /= deviation;
        }
    }
    return envelope;
}

struct Tempo {
    double bpm = 0.0;
    int periodFrames = 0;
    int offsetFrames = 0;
};

// Tempo from the autocorrelation of the onset envelope, reinforced with the
// first harmonics so that half/double time is less likely to win.
Tempo detectTempo(const std::vector<double>& envelope) {
    Tempo result;
    const double framesPerSecond =
            static_cast<double>(AudioAnalysisService::kSampleRate) / kHopSize;
    const int minLag = static_cast<int>(std::round(framesPerSecond * 60.0 / 200.0));
    const int maxLag = static_cast<int>(std::round(framesPerSecond * 60.0 / 60.0));
    const int size = static_cast<int>(envelope.size());
    if (size < minLag * 2) {
        return result;
    }

    // Short tracks cannot supply four harmonics of the slowest tempo; use as
    // many lags as the signal actually has instead of discarding the track.
    const int maxCorrelation = std::min(maxLag * 4, size - 1);
    std::vector<double> correlation(static_cast<size_t>(maxCorrelation + 1), 0.0);
    for (int lag = 1; lag <= maxCorrelation; ++lag) {
        double sum = 0.0;
        for (int i = 0; i + lag < size; ++i) {
            sum += envelope[static_cast<size_t>(i)] * envelope[static_cast<size_t>(i + lag)];
        }
        correlation[static_cast<size_t>(lag)] = sum / (size - lag);
    }

    int bestLag = minLag;
    double bestScore = -1e30;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double score = correlation[static_cast<size_t>(lag)];
        for (int harmonic = 2; harmonic <= 4; ++harmonic) {
            const int index = lag * harmonic;
            if (index <= maxCorrelation) {
                score += correlation[static_cast<size_t>(index)] / harmonic;
            }
        }
        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }

    double bpm = framesPerSecond * 60.0 / bestLag;
    // Gentle octave correction towards the range of club music.
    if (bpm < 80.0) {
        bpm *= 2.0;
        bestLag = std::max(1, bestLag / 2);
    } else if (bpm > 190.0) {
        bpm /= 2.0;
        bestLag = std::min(maxLag * 4, bestLag * 2);
    }

    // Find the phase that lines up with the strongest onsets.
    int bestOffset = 0;
    double bestPhase = -1e30;
    for (int offset = 0; offset < bestLag; ++offset) {
        double sum = 0.0;
        for (int beat = offset; beat < size; beat += bestLag) {
            sum += envelope[static_cast<size_t>(beat)];
        }
        if (sum > bestPhase) {
            bestPhase = sum;
            bestOffset = offset;
        }
    }

    result.bpm = bpm;
    result.periodFrames = bestLag;
    result.offsetFrames = bestOffset;
    return result;
}

// Builds the beat grid of the whole track from the estimated tempo and phase.
QList<core::Beat> buildBeatGrid(const Tempo& tempo, double duration) {
    QList<core::Beat> beats;
    if (tempo.bpm <= 0.0 || tempo.periodFrames <= 0) {
        return beats;
    }
    const double period = 60.0 / tempo.bpm;
    const double first =
            static_cast<double>(tempo.offsetFrames) * kHopSize / AudioAnalysisService::kSampleRate;
    int index = 0;
    for (double time = first; time <= duration; time += period, ++index) {
        core::Beat beat;
        beat.number = (index % 4) + 1;
        beat.time = time;
        beat.tempo = tempo.bpm;
        beats.append(beat);
    }
    return beats;
}

// Peak envelope, 150 columns per second, scaled so that the loudest column
// reaches 255. The ANLZ writer resamples it for the previews.
QByteArray buildWaveform(const std::vector<float>& samples, double duration) {
    const int columns = static_cast<int>(std::ceil(
            duration * AudioAnalysisService::kWaveformColumnsPerSecond));
    QByteArray peaks(columns, 0);
    if (columns <= 0 || samples.empty()) {
        return peaks;
    }

    std::vector<double> values(static_cast<size_t>(columns), 0.0);
    const double secondsPerColumn = 1.0 / AudioAnalysisService::kWaveformColumnsPerSecond;
    for (size_t i = 0; i < samples.size(); ++i) {
        const int column = static_cast<int>(
                (static_cast<double>(i) / AudioAnalysisService::kSampleRate)
                / secondsPerColumn);
        if (column < 0 || column >= columns) {
            continue;
        }
        values[static_cast<size_t>(column)] =
                std::max(values[static_cast<size_t>(column)],
                        std::abs(static_cast<double>(samples[i])));
    }
    double maximum = 0.0;
    for (const double value : values) {
        maximum = std::max(maximum, value);
    }
    if (maximum < 1e-9) {
        maximum = 1.0;
    }
    for (int i = 0; i < columns; ++i) {
        peaks[i] = static_cast<char>(static_cast<unsigned char>(
                std::min(255.0, values[static_cast<size_t>(i)] / maximum * 255.0)));
    }
    return peaks;
}

// Pearson correlation between a chromagram and a rotated key profile.
double correlation(const double chroma[12], const double profile[12], int rotation) {
    double sumA = 0.0, sumB = 0.0;
    for (int i = 0; i < 12; ++i) {
        sumA += chroma[i];
        sumB += profile[i];
    }
    const double meanA = sumA / 12.0;
    const double meanB = sumB / 12.0;
    double numerator = 0.0, denomA = 0.0, denomB = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double a = chroma[i] - meanA;
        const double b = profile[(i + rotation) % 12] - meanB;
        numerator += a * b;
        denomA += a * a;
        denomB += b * b;
    }
    if (denomA < 1e-12 || denomB < 1e-12) {
        return 0.0;
    }
    return numerator / std::sqrt(denomA * denomB);
}

// Estimates the key from a chromagram of the first minutes of the track.
QString detectKey(const std::vector<float>& samples) {
    const size_t limit = std::min(samples.size(),
            static_cast<size_t>(kChromaSeconds) * AudioAnalysisService::kSampleRate);
    if (limit < static_cast<size_t>(kFftSize)) {
        return {};
    }

    double chroma[12] = {};
    for (size_t start = 0; start + kFftSize <= limit; start += kChromaHop) {
        std::vector<std::complex<float>> window(kFftSize);
        for (int i = 0; i < kFftSize; ++i) {
            const double hann = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (kFftSize - 1)));
            window[static_cast<size_t>(i)] =
                    std::complex<float>(samples[start + i] * static_cast<float>(hann), 0.0F);
        }
        fft(window);

        for (int bin = 1; bin < kFftSize / 2; ++bin) {
            const double frequency = static_cast<double>(bin)
                    * AudioAnalysisService::kSampleRate / kFftSize;
            if (frequency < 65.0 || frequency > 2000.0) {
                continue;
            }
            const double midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
            const int pitch = static_cast<int>(std::round(midi)) % 12;
            chroma[pitch] += std::abs(window[static_cast<size_t>(bin)]);
        }
    }

    double total = 0.0;
    for (const double value : chroma) {
        total += value;
    }
    if (total < 1e-9) {
        return {};
    }

    int bestRoot = 0;
    bool bestMajor = true;
    double bestScore = -1e30;
    for (int root = 0; root < 12; ++root) {
        const double majorScore = correlation(chroma, kMajorProfile, root);
        const double minorScore = correlation(chroma, kMinorProfile, root);
        if (majorScore > bestScore) {
            bestScore = majorScore;
            bestRoot = root;
            bestMajor = true;
        }
        if (minorScore > bestScore) {
            bestScore = minorScore;
            bestRoot = root;
            bestMajor = false;
        }
    }
    return QString::fromLatin1(bestMajor ? kMajorCamelot[bestRoot] : kMinorCamelot[bestRoot]);
}

} // namespace

bool AudioAnalysisService::ffmpegAvailable() {
    return !QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty();
}

AnalysisResult AudioAnalysisService::analyze(const QString& filePath) {
    AnalysisResult result;
    QString error;
    const std::vector<float> samples = decode(filePath, &error);
    if (samples.empty()) {
        result.error = error;
        return result;
    }

    result.duration = static_cast<double>(samples.size()) / kSampleRate;
    const std::vector<double> envelope = onsetEnvelope(samples);
    const Tempo tempo = detectTempo(envelope);
    result.bpm = tempo.bpm;
    result.beats = buildBeatGrid(tempo, result.duration);
    result.peaks = buildWaveform(samples, result.duration);
    result.musicalKey = detectKey(samples);
    result.ok = result.bpm > 0.0 && !result.peaks.isEmpty();
    return result;
}

} // namespace audio
} // namespace opendeck
