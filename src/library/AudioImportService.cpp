#include "library/AudioImportService.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

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
#include <taglib/tpropertymap.h>

namespace opendeck {
namespace library {

namespace {
QString toQString(const TagLib::String& s) {
    return QString::fromUtf8(s.toCString(true));
}
} // namespace

core::Track AudioImportService::readMetadata(const QString& filePath, const QAtomicInteger<bool>* cancel,
                                             bool hashFile) {
    core::Track t;
    t.filePath = filePath;
    if (hashFile) t.fileHash = fileHash(filePath, cancel);

#ifdef Q_OS_WIN
    const QByteArray path = filePath.toUtf8();
#else
    const QByteArray path = filePath.toLocal8Bit();
#endif
    TagLib::FileRef file(path.constData());
    if (file.isNull() || !file.file()->isValid() || !file.audioProperties()) {
        t.fileHash.clear();
        return t;
    }

    if (TagLib::Tag* tag = file.tag()) {
        t.title = toQString(tag->title());
        t.artist = toQString(tag->artist());
        t.album = toQString(tag->album());
        t.genre = toQString(tag->genre());
        t.year = static_cast<int>(tag->year());
        t.comment = toQString(tag->comment());
        t.trackNumber = static_cast<int>(tag->track());
    }

    const auto properties = file.file()->properties();
    const auto property = [&](const char* key) {
        const auto values = properties.value(key);
        return values.isEmpty() ? QString() : toQString(values.front());
    };
    t.isrc = property("ISRC");
    t.composer = property("COMPOSER");
    t.label = property("LABEL");
    if (t.label.isEmpty()) t.label = property("PUBLISHER");
    t.mixName = property("MIXNAME");
    t.discNumber = property("DISCNUMBER").section('/', 0, 0).toInt();
    if (t.trackNumber == 0) t.trackNumber = property("TRACKNUMBER").section('/', 0, 0).toInt();

    if (TagLib::AudioProperties* ap = file.audioProperties()) {
        t.duration = ap->lengthInMilliseconds() / 1000.0;
        t.sampleRate = ap->sampleRate();
        t.bitrate = ap->bitrate();
        t.channels = ap->channels();
    }
    t.sampleDepth = sampleDepth(filePath);
    return t;
}

bool AudioImportService::isIntegerPcmWav(const QString& filePath) {
    const QByteArray path = filePath.toLocal8Bit();
    TagLib::RIFF::WAV::File wav(path.constData());
    if (!wav.isValid() || !wav.audioProperties()) return false;
    const int format = wav.audioProperties()->format();
    if (format == 1) return true;
    if (format != 0xfffe) return false;
    static const QByteArray pcmGuid = QByteArray::fromHex("0100000000001000800000aa00389b71");
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly) || !input.seek(12)) return false;
    while (!input.atEnd()) {
        const QByteArray header = input.read(8);
        if (header.size() != 8) return false;
        const qint64 length = qFromLittleEndian<quint32>(header.constData() + 4);
        if (length > input.size() - input.pos()) return false;
        if (header.startsWith("fmt "))
            return length >= 40 && input.read(40).mid(24, 16) == pcmGuid;
        if (!input.seek(input.pos() + length + (length % 2))) return false;
    }
    return false;
}

int AudioImportService::sampleDepth(const QString& filePath) {
    // The generic audio properties do not carry the depth of the sample, so
    // the formats that do have it are opened directly.
    const QByteArray path = filePath.toLocal8Bit();
    if (filePath.endsWith(QStringLiteral(".flac"), Qt::CaseInsensitive)) {
        TagLib::FLAC::File flac(path.constData());
        return flac.isValid() && flac.audioProperties() ? flac.audioProperties()->bitsPerSample() : 0;
    }
    if (filePath.endsWith(QStringLiteral(".wav"), Qt::CaseInsensitive)) {
        TagLib::RIFF::WAV::File wav(path.constData());
        return wav.isValid() && wav.audioProperties() ? wav.audioProperties()->bitsPerSample() : 0;
    }
    if (filePath.endsWith(QStringLiteral(".aif"), Qt::CaseInsensitive)
            || filePath.endsWith(QStringLiteral(".aiff"), Qt::CaseInsensitive)) {
        TagLib::RIFF::AIFF::File aiff(path.constData());
        return aiff.isValid() && aiff.audioProperties() ? aiff.audioProperties()->bitsPerSample() : 0;
    }
    return 0;
}

QString AudioImportService::fileHash(const QString& filePath, const QAtomicInteger<bool>* cancel) {
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
        if (cancel && cancel->loadRelaxed()) return {};
        const qint64 n = f.read(buffer.data(), kChunkSize);
        if (n < 0) return {};
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
