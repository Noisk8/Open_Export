#include "library/AudioImportService.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacproperties.h>
#include <taglib/aiffproperties.h>
#include <taglib/aifffile.h>
#include <taglib/rifffile.h>
#include <taglib/wavfile.h>
#include <taglib/wavproperties.h>
#include <taglib/tag.h>

namespace opendeck {
namespace library {

namespace {
QString toQString(const TagLib::String& s) {
    return QString::fromUtf8(s.toCString(true));
}
} // namespace

core::Track AudioImportService::readMetadata(const QString& filePath) {
    core::Track t;
    t.filePath = filePath;
    t.fileHash = fileHash(filePath);

#ifdef Q_OS_WIN
    const QByteArray path = filePath.toUtf8();
#else
    const QByteArray path = filePath.toLocal8Bit();
#endif
    TagLib::FileRef file(path.constData());
    if (file.isNull()) {
        return t;
    }

    if (TagLib::Tag* tag = file.tag()) {
        t.title = toQString(tag->title());
        t.artist = toQString(tag->artist());
        t.album = toQString(tag->album());
        t.genre = toQString(tag->genre());
        t.year = static_cast<int>(tag->year());
    }

    if (TagLib::AudioProperties* ap = file.audioProperties()) {
        t.duration = static_cast<double>(ap->lengthInSeconds());
        t.sampleRate = ap->sampleRate();
        t.bitrate = ap->bitrate();
        t.channels = ap->channels();
    }
    t.sampleDepth = sampleDepth(filePath);
    return t;
}

int AudioImportService::sampleDepth(const QString& filePath) {
    // The generic audio properties do not carry the depth of the sample, so
    // the formats that do have it are opened directly.
    const QByteArray path = filePath.toLocal8Bit();
    if (filePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
        TagLib::FLAC::File flac(path.constData());
        return flac.audioProperties()->bitsPerSample();
    }
    if (filePath.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        TagLib::RIFF::WAV::File wav(path.constData());
        return wav.audioProperties()->bitsPerSample();
    }
    if (filePath.endsWith(QStringLiteral(".aif"), Qt::CaseInsensitive)
            || filePath.endsWith(QStringLiteral(".aiff"), Qt::CaseInsensitive)) {
        TagLib::RIFF::AIFF::File aiff(path.constData());
        return aiff.audioProperties()->bitsPerSample();
    }
    return 0;
}

QString AudioImportService::fileHash(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    // Read in chunks so very large files do not exhaust memory.
    constexpr qint64 kChunkSize = 1024 * 1024;
    QByteArray buffer;
    buffer.resize(static_cast<int>(kChunkSize));
    while (!f.atEnd()) {
        const qint64 n = f.read(buffer.data(), kChunkSize);
        if (n > 0) {
            hash.addData(QByteArrayView(buffer.constData(), n));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool AudioImportService::isSupportedExtension(const QString& filePath) {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    static const QSet<QString> supported = {
            QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("flac"),
            QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("m4a"),
            QStringLiteral("aac"), QStringLiteral("ogg"),
    };
    return supported.contains(ext);
}

} // namespace library
} // namespace opendeck
