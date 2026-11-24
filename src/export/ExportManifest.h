#pragma once

#include <QHash>
#include <QString>

#include "export/ExportModel.h"

namespace opendeck {
namespace device {

// ExportManifest records what an export wrote, so that a later export can
// skip the audio files and ANLZ data that have not changed.
//
// It lives next to export.pdb as a hidden JSON file; players ignore unknown
// files, so it is harmless on a real USB stick.
class ExportManifest {
  public:
    static constexpr int kVersion = 1;
    static constexpr const char* kFileName = ".open-deck-manifest.json";

    // Loads the manifest of an existing export. `valid` is false when there
    // is no previous export (or it was written by an incompatible version).
    [[nodiscard]] static ExportManifest load(const QString& exportRoot, QString* error);
    [[nodiscard]] bool save(const QString& exportRoot, QString* error) const;

    [[nodiscard]] static QString pathIn(const QString& exportRoot);

    // Hash of everything that affects the files written for a single track:
    // metadata, analysis (beat grid, cues, waveform) and artwork.
    [[nodiscard]] static QString trackHash(const ExportTrack& item);

    // Hash of the playlist tree and its entries.
    [[nodiscard]] static QString libraryHash(const ExportLibrary& library);

    bool valid = false;
    int version = 0;
    QString createdDate;
    QString libraryDigest;
    QHash<QString, QString> ownedAudio; // devicePath -> SHA-256 of audio copied by this app
    QHash<QString, QString> tracks; // devicePath -> trackHash
};

} // namespace device
} // namespace opendeck
