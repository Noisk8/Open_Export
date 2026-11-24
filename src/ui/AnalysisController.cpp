#include "ui/AnalysisController.h"

#include <QFileInfo>
#include <QThread>

#include "audio/AudioAnalysisService.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/AnalysisWorker.h"
#include "ui/LibraryController.h"

namespace opendeck {
namespace ui {

AnalysisController::AnalysisController(library::DatabaseManager& db, QObject* parent)
        : QObject(parent), m_db(db) {
    qRegisterMetaType<audio::AnalysisResult>("opendeck::audio::AnalysisResult");
    refresh();
}

AnalysisController::~AnalysisController() {
    m_cancelRequested.storeRelaxed(true);
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->quit();
        m_thread->wait();
    }
}

bool AnalysisController::ffmpegAvailable() const {
    return audio::AudioAnalysisService::ffmpegAvailable();
}

void AnalysisController::setStatus(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void AnalysisController::bindToLibrary(LibraryController& library) {
    connect(&library, &LibraryController::importFinished, this,
            [this](int, int) { refresh(); });
    connect(&library, &LibraryController::trackCountChanged, this,
            &AnalysisController::refresh);
}

void AnalysisController::refresh() {
    updatePendingCount();
    if (!m_running) {
        setStatus(m_pendingCount > 0
                          ? QStringLiteral("%1 pistas pendientes de analizar")
                                    .arg(m_pendingCount)
                          : QStringLiteral("Toda la biblioteca está analizada"));
    }
}

void AnalysisController::updatePendingCount() {
    library::TrackRepository repository(m_db);
    const int count = static_cast<int>(repository.unanalyzedTracks().size());
    if (count != m_pendingCount) {
        m_pendingCount = count;
        emit pendingCountChanged();
    }
}

void AnalysisController::analyzeAll() {
    startAnalysis(nullptr, false);
}

void AnalysisController::analyzeTracks(const QList<qint64>& ids, bool useWavForFlac) {
    startAnalysis(&ids, useWavForFlac);
}

void AnalysisController::startAnalysis(const QList<qint64>* ids, bool useWavForFlac) {
    if (m_running) {
        return;
    }
    m_cancelRequested.storeRelaxed(false);
    m_saveFailures = 0;
    m_unscheduled = 0;
    library::TrackRepository repository(m_db);
    auto tracks = ids ? repository.tracksByIds(*ids) : repository.unanalyzedTracks();
    tracks.removeIf([](const core::Track& track) {
        return track.analysisStatus == core::AnalysisStatus::Analyzed;
    });
    if (tracks.isEmpty()) {
        setStatus(ids ? QStringLiteral("Las pistas seleccionadas ya están analizadas")
                      : QStringLiteral("Toda la biblioteca está analizada"));
        emit finished(0, 0);
        return;
    }

    QList<QPair<qint64, QString>> jobs;
    for (const core::Track& track : tracks) {
        QString path = track.filePath;
        if (useWavForFlac && path.endsWith(".flac", Qt::CaseInsensitive))
            path = repository.verifiedWav(track.fileHash).first;
        if (QFileInfo(path).isFile()) jobs.append({track.id, path});
        else if (ids && useWavForFlac && track.filePath.endsWith(".flac", Qt::CaseInsensitive))
            ++m_unscheduled;
    }
    if (jobs.isEmpty()) {
        setStatus(QStringLiteral("No hay archivos de audio accesibles para analizar"));
        emit finished(0, m_unscheduled);
        return;
    }
    if (!audio::AudioAnalysisService::ffmpegAvailable()) {
        setStatus(QStringLiteral("No se encontró ffmpeg: instálalo para analizar"));
        emit finished(0, jobs.size() + m_unscheduled);
        return;
    }

    m_running = true;
    m_progress = 0;
    m_cancelRequested.storeRelaxed(false);
    emit runningChanged();
    emit progressChanged();

    delete m_thread;
    m_thread = new QThread(this);
    m_worker = new AnalysisWorker();
    m_worker->setTracks(jobs);
    m_worker->setCancelFlag(&m_cancelRequested);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &AnalysisWorker::run);
    connect(m_worker, &AnalysisWorker::resultReady, this,
            &AnalysisController::onResult, Qt::QueuedConnection);
    connect(m_worker, &AnalysisWorker::progress, this,
            &AnalysisController::onProgress, Qt::QueuedConnection);
    connect(m_worker, &AnalysisWorker::finished, this,
            &AnalysisController::onWorkerFinished, Qt::QueuedConnection);
    connect(m_worker, &AnalysisWorker::finished, m_thread, &QThread::quit, Qt::DirectConnection);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    setStatus(QStringLiteral("Analizando %1 pistas…").arg(jobs.size()));
    m_thread->start();
}

void AnalysisController::cancel() {
    // The worker polls this during decoding and between analysis stages.
    m_cancelRequested.storeRelaxed(true);
}

void AnalysisController::onProgress(int current, int total, const QString& title) {
    m_progress = total > 0 ? static_cast<int>((current * 100) / total) : 0;
    emit progressChanged();
    setStatus(QStringLiteral("Analizando %1 (%2/%3)")
                      .arg(QFileInfo(title).fileName())
                      .arg(current + 1)
                      .arg(total));
}

void AnalysisController::onResult(qint64 trackId, const audio::AnalysisResult& result) {
    library::TrackRepository repository(m_db);
    const QList<core::Track> tracks = repository.tracksByIds({trackId});
    if (tracks.isEmpty()) {
        if (result.ok) ++m_saveFailures;
        return;
    }
    core::Track track = tracks.first();
    track.bpm = result.bpm;
    track.musicalKey = result.musicalKey;
    if (result.duration > 0.0) {
        track.duration = result.duration;
    }
    track.analysisStatus = result.ok ? core::AnalysisStatus::Analyzed
                                     : core::AnalysisStatus::Failed;
    if (result.ok && !repository.saveAnalysis(track, result.beats, result.peaks)) {
        ++m_saveFailures;
        return;
    }
    if (!result.ok && !repository.updateTrack(track)) return;
    emit trackAnalyzed(trackId);
}

void AnalysisController::onWorkerFinished(int analyzed, int failed) {
    analyzed -= m_saveFailures;
    failed += m_saveFailures + m_unscheduled;
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait();
    }
    m_running = false;
    m_progress = cancelled() ? 0 : 100;
    emit runningChanged();
    emit progressChanged();
    updatePendingCount();
    setStatus((cancelled() ? QStringLiteral("Análisis cancelado: %1 correctas, %2 con errores")
                           : QStringLiteral("Análisis completado: %1 correctas, %2 con errores"))
                      .arg(analyzed)
                      .arg(failed));
    emit finished(analyzed, failed);
}

} // namespace ui
} // namespace opendeck
