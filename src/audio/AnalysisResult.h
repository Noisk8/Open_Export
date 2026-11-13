#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "core/models/track.h"

namespace opendeck {
namespace audio {

// AnalysisResult holds everything the ANLZ writer needs from a track.
struct AnalysisResult {
    bool ok = false;
    QString error;
    double bpm = 0.0;
    QString musicalKey; // Camelot notation, e.g. "8A"
    double duration = 0.0; // seconds
    QList<core::Beat> beats;
    QByteArray peaks; // 150 columns per second, one byte per column
};

} // namespace audio
} // namespace opendeck

Q_DECLARE_METATYPE(opendeck::audio::AnalysisResult)
