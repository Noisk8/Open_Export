#include "ui/AnalysisController.h"

#include <QFileInfo>
#include <QThread>

#include "audio/AudioAnalysisService.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"
#include "ui/AnalysisWorker.h"

namespace opendeck {
namespace ui {

AnalysisController::AnalysisController(library::DatabaseManager& db, QObject* parent)
        : QObject(parent), m_db(db) {
    qRegisterMetaType<audio::AnalysisResult>("opendeck::audio::AnalysisResult");
    updatePendingCount();
    setStatus(m_pendingCount > 0
                      ? QStringLiteral("%1 pistas pendientes de analizar").arg(m_pendingCount)
                      : QStringLiteral("Toda la biblioteca está analizada"));
}

AnalysisController::~AnalysisController() {
    m_cancelRequested.storeRelaxed(true);
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->wait(30000);
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

void AnalysisController::updatePendingCount() {
    library::TrackRepository repository(m_db);
    const int count = static_cast<int>(repository.unanalyzedTracks().size());
    if (count != m_pendingCount) {
        m_pendingCount = count;
        emit pendingCountChanged();
    }
}

void AnalysisController::analyzeAll() {
    if (m_running) {
        return;
    }
    if (!audio::AudioAnalysisService::ffmpegAvailable()) {
        setStatus(QStringLiteral("No se encontró ffmpeg: instálalo para analizar"));
        return;
    }

    library::TrackRepository repository(m_db);
    const QList<core::Track> tracks = repository.unanalyzedTracks();
    if (tracks.isEmpty()) {
        setStatus(QStringLiteral("Toda la biblioteca está analizada"));
        emit finished(0, 0);
        return;
    }

    QList<QPair<qint64, QString>> jobs;
    for (const core::Track& track : tracks) {
        if (QFileInfo::exists(track.filePath)) {
            jobs.append({track.id, track.filePath});
        }
    }
    if (jobs.isEmpty()) {
        setStatus(QStringLiteral("No hay archivos de audio accesibles para analizar"));
        return;
    }

    m_running = true;
    m_progress = 0;
    m_cancelRequested.storeRelaxed(false);
    emit runningChanged();
    emit progressChanged();

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
    connect(m_worker, &AnalysisWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    setStatus(QStringLiteral("Analizando %1 pistas…").arg(jobs.size()));
    m_thread->start();
}

void AnalysisController::cancel() {
    // Only set the flag: the worker checks it between tracks. Quitting the
    // thread here would not stop a decode in progress and could destroy
    // objects that still have queued signal deliveries.
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
    repository.updateTrack(track);
    if (result.ok) {
        repository.replaceBeats(trackId, result.beats);
        repository.storeWaveform(trackId, 0, result.peaks);
    }
    emit trackAnalyzed(trackId);
}

void AnalysisController::onWorkerFinished(int analyzed, int failed) {
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait(30000);
    }
    m_running = false;
    m_progress = 100;
    emit runningChanged();
    emit progressChanged();
    updatePendingCount();
    setStatus(QStringLiteral("Análisis completado: %1 correctas, %2 con errores")
                      .arg(analyzed)
                      .arg(failed));
    emit finished(analyzed, failed);
}

} // namespace ui
} // namespace opendeck
