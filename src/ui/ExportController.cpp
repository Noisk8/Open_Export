#include "ui/ExportController.h"

#include <QFileInfo>
#include <QThread>

#include "ui/ExportWorker.h"
#include "ui/PlaylistController.h"

namespace opendeck {
namespace ui {

ExportController::ExportController(PlaylistController& playlists, QObject* parent)
        : QObject(parent), m_playlists(playlists) {
}

ExportController::~ExportController() {
    m_cancelRequested.storeRelaxed(true);
    // Never destroy a QThread that is still running.
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->wait(30000);
    }
}

void ExportController::setStatus(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void ExportController::exportTo(const QVariant& destination) {
    if (m_running) {
        return;
    }
    const QString path = PlaylistController::localPath(destination);
    const QFileInfo target(path);
    if (!target.isDir()) {
        const QString message = QStringLiteral("%1 no es una carpeta").arg(path);
        setStatus(message);
        emit finished(false, message);
        return;
    }

    // This is the only part that needs the database, and it is fast.
    const device::ExportLibrary library = m_playlists.buildExportLibrary();
    if (library.tracks.isEmpty()) {
        const QString message = QStringLiteral("No hay pistas para exportar");
        setStatus(message);
        emit finished(false, message);
        return;
    }

    device::ExportOptions options;
    options.destination = target.absoluteFilePath();

    m_running = true;
    m_progress = 0;
    m_cancelRequested.storeRelaxed(false);
    emit runningChanged();
    emit progressChanged();

    m_thread = new QThread(this);
    m_worker = new ExportWorker();
    m_worker->setLibrary(library);
    m_worker->setOptions(options);
    m_worker->setCancelFlag(&m_cancelRequested);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &ExportWorker::run);
    connect(m_worker, &ExportWorker::progress, this,
            &ExportController::onProgress, Qt::QueuedConnection);
    connect(m_worker, &ExportWorker::finished, this,
            &ExportController::onWorkerFinished, Qt::QueuedConnection);
    connect(m_worker, &ExportWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    setStatus(QStringLiteral("Exportando a %1…").arg(target.fileName()));
    m_thread->start();
}

void ExportController::cancel() {
    // Only set the flag: the worker checks it between tracks. Quitting the
    // thread here would not stop a copy in progress and could destroy objects
    // that still have queued signal deliveries.
    m_cancelRequested.storeRelaxed(true);
    setStatus(QStringLiteral("Cancelando la exportación…"));
}

void ExportController::onProgress(int current, int total, const QString& fileName) {
    m_progress = total > 0 ? static_cast<int>((current * 100) / total) : 0;
    emit progressChanged();
    setStatus(QStringLiteral("Exportando %1 (%2/%3)")
                      .arg(fileName)
                      .arg(current + 1)
                      .arg(total));
}

void ExportController::onWorkerFinished(bool ok, const QString& message, bool cancelled) {
    // Wait for the thread to actually stop before allowing another export,
    // otherwise a second QThread would replace the still-running one.
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait(30000);
    }
    m_running = false;
    m_progress = cancelled ? 0 : 100;
    emit runningChanged();
    emit progressChanged();
    setStatus(message);
    emit finished(ok, message);
}

} // namespace ui
} // namespace opendeck
