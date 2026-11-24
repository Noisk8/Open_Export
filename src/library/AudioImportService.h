#pragma once

#include <QString>
#include <QAtomicInteger>

#include "core/models/track.h"

namespace opendeck {
namespace library {

// AudioImportService reads metadata from audio files without modifying them.
// Synchronous primitive used by the import worker; optional cancellation
// is checked during hashing.
class AudioImportService {
  public:
    // Reads title/artist/album/genre/year/duration/sample rate/bitrate/channels
    // from an audio file using TagLib. Unknown fields are left empty/zero.
    static core::Track readMetadata(const QString& filePath, const QAtomicInteger<bool>* cancel = nullptr,
                                    bool hashFile = true);

    // Returns the SHA-256 hash of the file contents (persistent identity).
    static QString fileHash(const QString& filePath, const QAtomicInteger<bool>* cancel = nullptr);

    // The file extensions the importer currently accepts.
    static bool isSupportedExtension(const QString& filePath);

    // The depth of the sample in bits, when the format can tell it.
    static bool isIntegerPcmWav(const QString& filePath);
    static int sampleDepth(const QString& filePath);
};

} // namespace library
} // namespace opendeck
