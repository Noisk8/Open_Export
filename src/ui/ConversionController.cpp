#include "ui/ConversionController.h"

#include <QDir>
#include <QHash>
#include <QFileInfo>
#include <QFileInfoList>
#include <QStandardPaths>
#include <QThread>

namespace opendeck {
namespace ui {

ConversionController::ConversionController(QObject* parent)
        : QAbstractListModel(parent) {
    qRegisterMetaType<ConversionOutcome>("opendeck::ui::ConversionOutcome");
}

ConversionController::~ConversionController() {
    m_cancelRequested.storeRelaxed(true);
    if (m_thread != nullptr && m_thread->isRunning()) {
        m_thread->wait(30000);
    }
}

int ConversionController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant ConversionController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const ConversionEntry& entry = m_entries.at(index.row());
    switch (role) {
    case FileNameRole: return QFileInfo(entry.source).fileName();
    case SourceRole: return entry.source;
    case TargetRole: return entry.target;
    case StatusRole: return entry.status;
    case MessageRole: return entry.message;
    case SampleRateRole: return entry.sampleRate;
    case ChannelsRole: return entry.channels;
    case BitDepthRole: return entry.bitDepth;
    case TagCountRole: return entry.tagCount;
    case LosslessRole: return entry.lossless;
    case ProgressLabelRole:
        return QStringLiteral("%1/%2").arg(completedCount()).arg(m_entries.size());
    default: return {};
    }
}

QHash<int, QByteArray> ConversionController::roleNames() const {
    return {
            {FileNameRole, "fileName"},
            {SourceRole, "source"},
            {TargetRole, "target"},
            {StatusRole, "status"},
            {MessageRole, "message"},
            {SampleRateRole, "sampleRate"},
            {ChannelsRole, "channels"},
            {BitDepthRole, "bitDepth"},
            {TagCountRole, "tagCount"},
            {LosslessRole, "lossless"},
            {ProgressLabelRole, "progressLabel"},
    };
}

int ConversionController::completedCount() const {
    int count = 0;
    for (const ConversionEntry& entry : m_entries) {
        if (entry.status == 2 || entry.status == 3) {
            ++count;
        }
    }
    return count;
}

void ConversionController::setStatus(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void ConversionController::collectFlacFiles(const QString& directory, QStringList& out) const {
    QDir dir(directory);
    const QFileInfoList entries =
            dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& info : entries) {
        if (info.isDir()) {
            collectFlacFiles(info.absoluteFilePath(), out);
        } else if (info.suffix().compare(QStringLiteral("flac"), Qt::CaseInsensitive) == 0) {
            out.append(info.absoluteFilePath());
        }
    }
}

void ConversionController::appendFile(const QString& path) {
    if (!QFileInfo::exists(path)) {
        return;
    }
    for (const ConversionEntry& entry : m_entries) {
        if (entry.source == path) {
            return; // no duplicates
        }
    }
    ConversionEntry entry;
    entry.source = path;
    beginInsertRows(QModelIndex(), static_cast<int>(m_entries.size()),
            static_cast<int>(m_entries.size()));
    m_entries.append(entry);
    endInsertRows();
    refreshTargets();
    emit countChanged();
    setStatus(QStringLiteral("%1 archivos FLAC en la cola").arg(m_entries.size()));
}

void ConversionController::refreshTargets() {
    for (ConversionEntry& entry : m_entries) {
        const QFileInfo info(entry.source);
        const QString directory = m_outputDirectory.isEmpty()
                ? info.absolutePath() : m_outputDirectory;
        QString target = directory + QChar('/') + info.completeBaseName()
                + QStringLiteral(".wav");
        if (target == entry.source) {
            target = directory + QChar('/') + info.completeBaseName()
                    + QStringLiteral("_converted.wav");
        }
        entry.target = target;
    }
    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_entries.size()) - 1));
    }
}

void ConversionController::addFiles(const QList<QUrl>& urls) {
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (info.isDir()) {
            addFolder(path);
        } else if (info.suffix().compare(QStringLiteral("flac"), Qt::CaseInsensitive) == 0) {
            appendFile(info.absoluteFilePath());
        }
    }
}

void ConversionController::addFolder(const QString& path) {
    // FolderDialog hands over a QUrl (file:///…) which QML converts to a
    // string; turn it back into a filesystem path.
    const QString normalized = QUrl(path).isLocalFile()
            ? QUrl(path).toLocalFile() : path;
    QStringList files;
    collectFlacFiles(normalized, files);
    for (const QString& file : files) {
        appendFile(file);
    }
}

void ConversionController::removeAt(int row) {
    if (row < 0 || row >= m_entries.size() || m_running) {
        return;
    }
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void ConversionController::clear() {
    if (m_running) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    setStatus(QString());
}

void ConversionController::setOutputDirectory(const QString& path) {
    const QString normalized = QUrl(path).isLocalFile()
            ? QUrl(path).toLocalFile() : path;
    if (m_outputDirectory == normalized) {
        return;
    }
    m_outputDirectory = normalized;
    refreshTargets();
    emit outputDirectoryChanged();
}

void ConversionController::start() {
    if (m_running) {
        return;
    }
    if (m_entries.isEmpty()) {
        setStatus(QStringLiteral("Añade archivos FLAC primero"));
        return;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        setStatus(QStringLiteral("No se encontró ffmpeg en el PATH"));
        return;
    }

    refreshTargets();
    QList<ConversionJob> jobs;
    jobs.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        ConversionEntry& entry = m_entries[i];
        entry.status = 0;
        entry.message.clear();
        entry.lossless = false;
        jobs.append({entry.source, entry.target});
    }
    emit dataChanged(index(0), index(static_cast<int>(m_entries.size()) - 1));

    m_running = true;
    m_progress = 0;
    m_cancelRequested.storeRelaxed(false);
    emit runningChanged();
    emit progressChanged();

    m_thread = new QThread(this);
    m_worker = new ConversionWorker();
    m_worker->setJobs(jobs);
    m_worker->setCancelFlag(&m_cancelRequested);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker, &ConversionWorker::run);
    connect(m_worker, &ConversionWorker::itemStarted, this,
            &ConversionController::onItemStarted, Qt::QueuedConnection);
    connect(m_worker, &ConversionWorker::itemFinished, this,
            &ConversionController::onItemFinished, Qt::QueuedConnection);
    connect(m_worker, &ConversionWorker::progress, this,
            [this](int current, int total) {
                m_progress = total > 0 ? (current * 100) / total : 0;
                emit progressChanged();
            }, Qt::QueuedConnection);
    connect(m_worker, &ConversionWorker::finished, this,
            &ConversionController::onWorkerFinished, Qt::QueuedConnection);
    connect(m_worker, &ConversionWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    setStatus(QStringLiteral("Convirtiendo %1 archivos…").arg(m_entries.size()));
    m_thread->start();
}

void ConversionController::cancel() {
    if (!m_running) {
        return;
    }
    m_cancelRequested.storeRelaxed(true);
    setStatus(QStringLiteral("Cancelando…"));
}

void ConversionController::onItemStarted(int row, const QString& fileName) {
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    m_entries[row].status = 1;
    emit dataChanged(index(row), index(row));
    setStatus(QStringLiteral("Convirtiendo %1…").arg(fileName));
}

void ConversionController::onItemFinished(int row, const ConversionOutcome& outcome) {
    if (row < 0 || row >= m_entries.size()) {
        return;
    }
    ConversionEntry& entry = m_entries[row];
    entry.status = outcome.ok ? 2 : 3;
    entry.message = outcome.message;
    entry.sampleRate = outcome.sampleRate;
    entry.channels = outcome.channels;
    entry.bitDepth = outcome.bitDepth;
    entry.tagCount = outcome.tagCount;
    entry.lossless = outcome.lossless;
    entry.target = outcome.target;
    emit dataChanged(index(row), index(row));
    emit countChanged();
}

void ConversionController::onWorkerFinished(int succeeded, int failed) {
    if (m_thread != nullptr) {
        m_thread->quit();
        m_thread->wait(30000);
    }
    m_running = false;
    m_progress = 100;
    emit runningChanged();
    emit progressChanged();
    setStatus(QStringLiteral("Convertidos %1 · Errores %2").arg(succeeded).arg(failed));
    emit finished(succeeded, failed);
}

} // namespace ui
} // namespace opendeck
