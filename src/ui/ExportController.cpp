#include "ui/ExportController.h"

#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>
#include <exception>

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
        m_thread->quit();
        m_thread->wait();
    }
}

void ExportController::setStatus(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void ExportController::setWriteDeviceLibrary(bool enabled) {
    if (m_writeDeviceLibrary == enabled) {
        return;
    }
    m_writeDeviceLibrary = enabled;
    emit optionsChanged();
}

void ExportController::setIncremental(bool enabled) {
    if (m_incremental == enabled) {
        return;
    }
    m_incremental = enabled;
    emit optionsChanged();
}

void ExportController::setUseWavForFlac(bool enabled) {
    if (m_useWavForFlac == enabled) return;
    m_useWavForFlac = enabled;
    emit optionsChanged();
}

QList<qint64> ExportController::selectedTrackIds() const {
    if (m_extensionFile.isEmpty()) return m_playlists.exportTrackIds(m_selectedPlaylists, m_allPlaylists);
    try {
        const auto library = m_playlists.buildExportLibrary(m_selectedPlaylists, m_allPlaylists,
                useWavForFlac(), PlaylistController::localPath(m_extensionFile));
        QList<qint64> ids;
        for (const auto& track : library.tracks) ids.append(track.track.id);
        return ids;
    } catch (const std::exception&) {
        return {};
    }
}

QVariantMap ExportController::summary(const QVariant& destination) const {
    auto result = m_playlists.exportSummary(m_selectedPlaylists, m_allPlaylists, useWavForFlac());
    if (!m_extensionFile.isEmpty()) {
        try {
            const auto library = m_playlists.buildExportLibrary(m_selectedPlaylists, m_allPlaylists,
                    useWavForFlac(), PlaylistController::localPath(m_extensionFile));
            if (!library.banks.isEmpty() && (!m_writeDeviceLibrary || m_xdjRxProfile))
                result.insert("extensionError", QStringLiteral("Los bancos de hot cues requieren OneLibrary"));
            qint64 bytes = 0;
            int missing = 0;
            int needsWav = 0;
            for (const auto& item : library.tracks) {
                const QFileInfo file(item.track.filePath);
                if (!file.isFile()) {
                    ++missing;
                    if (item.track.filePath.isEmpty() || !item.verifiedWavHash.isEmpty()) ++needsWav;
                } else {
                    bytes += file.size();
                }
            }
            for (const auto& bank : library.banks) {
                for (const auto& entry : bank.entries) {
                    if (!QFileInfo(library.tracks.at(entry.trackId - 1).track.filePath).isFile())
                        result.insert("extensionError", QStringLiteral("Falta audio en el banco «%1»").arg(bank.name));
                }
            }
            result.insert("tracks", library.tracks.size());
            result.insert("audioBytes", bytes);
            result.insert("missing", missing);
            result.insert("needsWav", needsWav);
            result.insert("tags", library.tags.size());
            result.insert("banks", library.banks.size());
        } catch (const std::exception& error) {
            result.insert("extensionError", QString::fromUtf8(error.what()));
        }
    }
    const QString path = PlaylistController::localPath(destination);
    const QStorageInfo storage(path);
    result.insert("destination", path);
    result.insert("availableBytes", storage.isValid() ? storage.bytesAvailable() : -1);
    return result;
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
    device::ExportLibrary library;
    try {
        library = m_playlists.buildExportLibrary(m_selectedPlaylists, m_allPlaylists, useWavForFlac(),
                PlaylistController::localPath(m_extensionFile));
    } catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        setStatus(message);
        emit finished(false, message);
        return;
    }
    if (library.tracks.isEmpty()) {
        const QString message = QStringLiteral("No hay pistas para exportar");
        setStatus(message);
        emit finished(false, message);
        return;
    }

    device::ExportOptions options;
    options.destination = target.absoluteFilePath();
    options.writeDeviceLibrary = m_writeDeviceLibrary && !m_xdjRxProfile;
    options.xdjRxProfile = m_xdjRxProfile;
    options.incremental = m_incremental;

    m_running = true;
    m_progress = 0;
    m_cancelRequested.storeRelaxed(false);
    emit runningChanged();
    emit progressChanged();

    delete m_thread;
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
    connect(m_worker, &ExportWorker::finished, m_thread, &QThread::quit, Qt::DirectConnection);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    setStatus(QStringLiteral("Exportando a %1…").arg(target.fileName()));
    m_thread->start();
}

void ExportController::cancel() {
    // The writer polls between tracks and during chunked audio copies.
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
        m_thread->wait();
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
