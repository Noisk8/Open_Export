#pragma once

#include <QString>

#include "core/models/track.h"

namespace opendeck {
namespace library {

// AudioImportService reads metadata from audio files without modifying them.
// It is intentionally synchronous: metadata extraction is fast. The (heavier)
// musical analysis runs in its own workers in a later phase.
class AudioImportService {
  public:
    // Reads title/artist/album/genre/year/duration/sample rate/bitrate/channels
    // from an audio file using TagLib. Unknown fields are left empty/zero.
    static core::Track readMetadata(const QString& filePath);

    // Returns the SHA-256 hash of the file contents (persistent identity).
    static QString fileHash(const QString& filePath);

    // The file extensions the importer currently accepts.
    static bool isSupportedExtension(const QString& filePath);

    // The depth of the sample in bits, when the format can tell it.
    static int sampleDepth(const QString& filePath);
};

} // namespace library
} // namespace opendeck
