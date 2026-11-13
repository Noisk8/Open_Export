#pragma once

#include <QString>

#include "audio/AnalysisResult.h"

namespace opendeck {
namespace audio {

// AudioAnalysisService decodes an audio file and derives the musical data
// that the Pioneer ANLZ files carry: tempo, beat grid, tonalidad and the
// waveform envelope.
//
// The PCM is decoded with the external `ffmpeg` binary (mono, 22050 Hz,
// 32 bit float little endian), so the project does not need the libav
// development headers. All the signal processing is plain C++.
//
// This is an *estimate*, not a rekordbox analysis: the tempo comes from the
// autocorrelation of an onset envelope, the phase from the strongest beat
// alignment, and the key from a chromagram correlated against
// Krumhansl-Schmuckler profiles. It is enough for a player to show a usable
// beat grid, key and waveform.
class AudioAnalysisService {
  public:
    // Decodes and analyses a file. Never throws; check `ok` and `error`.
    [[nodiscard]] static AnalysisResult analyze(const QString& filePath);

    // True when an ffmpeg executable is reachable.
    [[nodiscard]] static bool ffmpegAvailable();

    // Sample rate of the internal PCM and the resolution of the waveform.
    static constexpr int kSampleRate = 22050;
    static constexpr int kWaveformColumnsPerSecond = 150;
};

} // namespace audio
} // namespace opendeck
