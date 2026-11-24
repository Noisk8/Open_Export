#pragma once

#include <QAtomicInteger>
#include <QList>
#include <QObject>
#include <QString>

namespace opendeck {
namespace ui {

struct ConversionJob {
    QString source;
    QString target;
};

// Result of converting one file. It crosses a thread boundary, so it is a
// registered metatype.
struct ConversionOutcome {
    int row = 0;
    QString target;
    QString sourceHash;
    QString wavHash;
    bool ok = false;
    QString message;
    int sampleRate = 0;
    int channels = 0;
    int bitDepth = 0;
    int tagCount = 0;
    bool lossless = false;
};

// ConversionWorker converts FLAC files to WAV off the GUI thread.
//
// The conversion is bit-exact: ffmpeg decodes the FLAC and re-encodes it as
// PCM with the original sample rate, channel count and bit depth, and the
// decoded PCM of both files is hashed afterwards to prove nothing was lost.
// All Vorbis-comment tags and embedded pictures are written back into the WAV
// with TagLib (ID3v2 / INFO chunks).
class ConversionWorker : public QObject {
    Q_OBJECT
  public:
    explicit ConversionWorker(QObject* parent = nullptr);

    void setJobs(const QList<ConversionJob>& jobs) { m_jobs = jobs; }
    void setCancelFlag(QAtomicInteger<bool>* flag) { m_cancelFlag = flag; }

  public slots:
    void run();

  signals:
    void itemStarted(int row, const QString& fileName);
    void itemFinished(int row, const opendeck::ui::ConversionOutcome& outcome);
    void progress(int current, int total);
    void finished(int succeeded, int failed);

  private:
    [[nodiscard]] ConversionOutcome convert(const ConversionJob& job, int row) const;

    QList<ConversionJob> m_jobs;
    QAtomicInteger<bool>* m_cancelFlag = nullptr;
};

} // namespace ui
} // namespace opendeck

Q_DECLARE_METATYPE(opendeck::ui::ConversionOutcome)
