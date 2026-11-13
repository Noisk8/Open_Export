#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace opendeck {
namespace library {

// EmbeddedArtwork is an image stored inside an audio file.
struct EmbeddedArtwork {
    QByteArray data;
    QString mimeType;
};

// Reads the cover art of an audio file, if it has one.
//
// Only the formats that Pioneer players accept are inspected; the images are
// returned as they are stored in the file.
[[nodiscard]] QList<EmbeddedArtwork> readEmbeddedArtwork(const QString& filePath);

} // namespace library
} // namespace opendeck
