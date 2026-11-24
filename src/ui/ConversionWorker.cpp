#include "ui/ConversionWorker.h"
#include "library/AudioImportService.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include "core/ProcessWait.h"
#include <QProcess>
#include <QStandardPaths>

#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/flacproperties.h>
#include <taglib/id3v2tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/wavfile.h>

namespace opendeck {
namespace ui {

namespace {

QString ffmpegExecutable() {
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QByteArray decodedPcm(const QString& ffmpeg, const QString& path, QString* error, const QAtomicInteger<bool>* cancel) {
    QProcess process;
    process.start(ffmpeg,
            {QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-nostdin"),
                    QStringLiteral("-i"), path,
                    QStringLiteral("-map"), QStringLiteral("0:a:0"),
                    QStringLiteral("-f"), QStringLiteral("f64le"),
                    QStringLiteral("-c:a"), QStringLiteral("pcm_f64le"),
                    QStringLiteral("-")});
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 bytes = 0;
    if (!waitForProcess(process, 180000, cancel, error, [&](const QByteArray& chunk) {
        hash.addData(chunk);
        bytes += chunk.size();
    }) || bytes == 0) return {};
    return hash.result();
}

QString codecFor(int bitDepth) {
    if (bitDepth > 24) return QStringLiteral("pcm_s32le");
    if (bitDepth > 16) return QStringLiteral("pcm_s24le");
    return QStringLiteral("pcm_s16le");
}

QString toQString(const TagLib::String& value) {
    return QString::fromUtf8(value.toCString(true));
}

} // namespace

ConversionWorker::ConversionWorker(QObject* parent) : QObject(parent) {
}

void ConversionWorker::run() {
    int succeeded = 0;
    int failed = 0;
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_cancelFlag != nullptr && m_cancelFlag->loadRelaxed()) {
            break;
        }
        emit itemStarted(i, QFileInfo(m_jobs.at(i).source).fileName());
        emit progress(i, m_jobs.size());
        const ConversionOutcome outcome = convert(m_jobs.at(i), i);
        if (outcome.ok) {
            ++succeeded;
        } else {
            ++failed;
        }
        emit itemFinished(i, outcome);
    }
    emit finished(succeeded, failed);
}

ConversionOutcome ConversionWorker::convert(const ConversionJob& job, int row) const {
    ConversionOutcome outcome;
    outcome.row = row;
    outcome.target = job.target;

    const QString ffmpeg = ffmpegExecutable();
    if (ffmpeg.isEmpty()) {
        outcome.message = QStringLiteral("No se encontró ffmpeg en el PATH");
        return outcome;
    }

    outcome.sourceHash = library::AudioImportService::fileHash(job.source, m_cancelFlag);
    if (outcome.sourceHash.isEmpty()) {
        outcome.message = QStringLiteral("No se pudo verificar el FLAC de origen");
        return outcome;
    }
    const QByteArray sourcePath = job.source.toLocal8Bit();
    TagLib::FLAC::File flac(sourcePath.constData());
    if (!flac.isValid() || flac.audioProperties() == nullptr) {
        outcome.message = QStringLiteral("No se pudo leer el FLAC con TagLib");
        return outcome;
    }

    const int sampleRate = flac.audioProperties()->sampleRate();
    const int channels = flac.audioProperties()->channels();
    const int bitDepth = flac.audioProperties()->bitsPerSample();
    const TagLib::PropertyMap properties = flac.properties();
    const auto pictures = flac.pictureList();
    outcome.sampleRate = sampleRate;
    outcome.channels = channels;
    outcome.bitDepth = bitDepth > 0 ? bitDepth : 16;

    // Only our own temporary file is ever passed to ffmpeg with overwrite enabled.
    QTemporaryFile temporary(QFileInfo(job.target).absolutePath() + "/.ode-XXXXXX.wav");
    if (!temporary.open()) {
        outcome.message = temporary.errorString();
        return outcome;
    }
    const QString workingPath = temporary.fileName();
    temporary.close();

    // 1) Decode and re-encode as PCM. -map_metadata 0 copies what ffmpeg can;
    //    TagLib restores the full tag set below.
    QProcess conversion;
    conversion.start(ffmpeg,
            {QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-nostdin"),
                    QStringLiteral("-y"),
                    QStringLiteral("-i"), job.source,
                    QStringLiteral("-map"), QStringLiteral("0:a:0"),
                    QStringLiteral("-map_metadata"), QStringLiteral("0"),
                    QStringLiteral("-c:a"), codecFor(outcome.bitDepth),
                    QStringLiteral("-ar"), QString::number(sampleRate),
                    QStringLiteral("-ac"), QString::number(channels),
                    workingPath});
    if (!waitForProcess(conversion, 300000, m_cancelFlag, &outcome.message)) return outcome;
    if (QFileInfo(workingPath).size() == 0) {
        outcome.message = QStringLiteral("No se generó el WAV");
        return outcome;
    }

    // 2) Restore every tag (including custom Vorbis comment keys) and the
    //    embedded cover art in the WAV container.
    {
        const QByteArray targetPath = workingPath.toLocal8Bit();
        TagLib::RIFF::WAV::File wav(targetPath.constData());
        if (!wav.isValid()) {
            outcome.message = QStringLiteral("No se pudo abrir el WAV generado");
            return outcome;
        }
        if (TagLib::Tag* tag = wav.tag()) {
            tag->setTitle(properties.value("TITLE").isEmpty()
                    ? TagLib::String() : properties.value("TITLE").front());
            tag->setArtist(properties.value("ARTIST").isEmpty()
                    ? TagLib::String() : properties.value("ARTIST").front());
            tag->setAlbum(properties.value("ALBUM").isEmpty()
                    ? TagLib::String() : properties.value("ALBUM").front());
            tag->setGenre(properties.value("GENRE").isEmpty()
                    ? TagLib::String() : properties.value("GENRE").front());
            tag->setComment(properties.value("COMMENT").isEmpty()
                    ? TagLib::String() : properties.value("COMMENT").front());
            tag->setTrack(properties.value("TRACKNUMBER").isEmpty()
                    ? 0 : properties.value("TRACKNUMBER").front().toInt());
            tag->setYear(properties.value("DATE").isEmpty()
                    ? 0 : properties.value("DATE").front().toInt());
        }
        // Returns the keys the container could not store; the basics are also
        // written explicitly above so no common tag is lost.
        wav.setProperties(properties);

        if (!pictures.isEmpty()) {
            TagLib::ID3v2::Tag* id3 = wav.ID3v2Tag();
            if (id3 != nullptr) {
                for (const TagLib::FLAC::Picture* picture : pictures) {
                    auto* frame = new TagLib::ID3v2::AttachedPictureFrame;
                    frame->setMimeType(picture->mimeType());
                    frame->setPicture(picture->data());
                    frame->setDescription(picture->description());
                    frame->setType(static_cast<TagLib::ID3v2::AttachedPictureFrame::Type>(
                            picture->type()));
                    id3->addFrame(frame);
                }
            }
        }
        if (!wav.save()) {
            outcome.message = QStringLiteral("No se pudo guardar la metadata en el WAV");
            return outcome;
        }
    }

    // Count the tags that survived the round trip.
    {
        const QByteArray targetPath = workingPath.toLocal8Bit();
        TagLib::RIFF::WAV::File wav(targetPath.constData());
        if (wav.isValid() && wav.audioProperties()) {
            outcome.bitDepth = wav.audioProperties()->bitsPerSample();
            const TagLib::PropertyMap restored = wav.properties();
            for (auto tag = properties.begin(); tag != properties.end(); ++tag) {
                if (restored.value(tag->first) == tag->second) ++outcome.tagCount;
            }
            if (outcome.tagCount < static_cast<int>(properties.size())) {
                outcome.message = QStringLiteral("Metadata parcial: %1 de %2 claves")
                                          .arg(outcome.tagCount)
                                          .arg(static_cast<int>(properties.size()));
            }
        }
    }

    // 3) Prove the conversion is bit-exact by hashing the decoded PCM of both
    //    files. No dithering, resampling or lossy step is involved.
    QString verifyError;
    const QByteArray originalPcm = decodedPcm(ffmpeg, job.source, &verifyError, m_cancelFlag);
    const QByteArray convertedPcm = originalPcm.isEmpty() ? QByteArray() : decodedPcm(ffmpeg, workingPath, &verifyError, m_cancelFlag);
    if (originalPcm.isEmpty() || convertedPcm.isEmpty()) {
        outcome.message = QStringLiteral("No se pudo verificar el PCM: %1")
                                  .arg(verifyError);
        return outcome;
    }
    outcome.lossless = originalPcm == convertedPcm;
    if (!outcome.lossless) {
        outcome.message = QStringLiteral("El PCM no coincide: la conversión no es sin pérdida");
        return outcome;
    }

    if (m_cancelFlag && m_cancelFlag->loadRelaxed()) {
        outcome.message = QStringLiteral("Operación cancelada");
        return outcome;
    }
    outcome.wavHash = library::AudioImportService::fileHash(workingPath, m_cancelFlag);
    if (outcome.wavHash.isEmpty()
            || outcome.sourceHash != library::AudioImportService::fileHash(job.source, m_cancelFlag)) {
        outcome.message = QStringLiteral("Conversión cancelada o el archivo de origen cambió");
        return outcome;
    }
    // QFile::rename refuses an existing destination, including a late collision.
    if (!QFile::rename(workingPath, job.target)) {
        outcome.message = QStringLiteral("No se publicó el WAV: destino ocupado o no escribible. El archivo existente se conserva.");
        return outcome;
    }
    temporary.setAutoRemove(false);
    outcome.ok = true;
    if (outcome.message.isEmpty()) {
        outcome.message = QStringLiteral("Sin pérdida · %1 kHz · %2 bit · %3 canales · %4 tags")
                                  .arg(sampleRate / 1000.0, 0, 'f', 1)
                                  .arg(outcome.bitDepth)
                                  .arg(channels)
                                  .arg(outcome.tagCount);
    }
    return outcome;
}

} // namespace ui
} // namespace opendeck
