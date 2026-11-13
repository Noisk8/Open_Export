#include "library/ArtworkExtractor.h"

#include <QFile>

#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/wavfile.h>

namespace opendeck {
namespace library {

namespace {

QByteArray toQByteArray(const TagLib::ByteVector& vector) {
    return QByteArray(reinterpret_cast<const char*>(vector.data()),
            static_cast<int>(vector.size()));
}

QString toQString(const TagLib::String& value) {
    return QString::fromUtf8(value.toCString(true));
}

} // namespace

QList<EmbeddedArtwork> readEmbeddedArtwork(const QString& filePath) {
    QList<EmbeddedArtwork> result;
    const QByteArray path = filePath.toLocal8Bit();

    // --- MPEG (mp3) ---------------------------------------------------------
    if (filePath.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive)) {
        TagLib::MPEG::File mpeg(path.constData());
        const TagLib::ID3v2::Tag* tag = mpeg.ID3v2Tag();
        if (tag != nullptr) {
            for (const TagLib::ID3v2::Frame* frame : tag->frameList("APIC")) {
                const auto* picture =
                        dynamic_cast<const TagLib::ID3v2::AttachedPictureFrame*>(frame);
                if (picture == nullptr) {
                    continue;
                }
                EmbeddedArtwork artwork;
                artwork.data = toQByteArray(picture->picture());
                artwork.mimeType = toQString(picture->mimeType());
                result.append(artwork);
            }
        }
    }

    // --- WAV ----------------------------------------------------------------
    if (filePath.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        TagLib::RIFF::WAV::File wav(path.constData());
        const TagLib::ID3v2::Tag* tag = wav.ID3v2Tag();
        if (tag != nullptr) {
            for (const TagLib::ID3v2::Frame* frame : tag->frameList("APIC")) {
                const auto* picture =
                        dynamic_cast<const TagLib::ID3v2::AttachedPictureFrame*>(frame);
                if (picture == nullptr) {
                    continue;
                }
                EmbeddedArtwork artwork;
                artwork.data = toQByteArray(picture->picture());
                artwork.mimeType = toQString(picture->mimeType());
                result.append(artwork);
            }
        }
    }

    // --- FLAC ---------------------------------------------------------------
    if (filePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
        TagLib::FLAC::File flac(path.constData());
        for (const TagLib::FLAC::Picture* picture : flac.pictureList()) {
            EmbeddedArtwork artwork;
            artwork.data = toQByteArray(picture->data());
            artwork.mimeType = toQString(picture->mimeType());
            result.append(artwork);
        }
    }

    return result;
}

} // namespace library
} // namespace opendeck
