#include "ui/ExportWorker.h"

#include <QFileInfo>

#include "export/ExportValidator.h"

namespace opendeck {
namespace ui {

ExportWorker::ExportWorker(QObject* parent) : QObject(parent) {
}

void ExportWorker::run() {
    device::ExportCallbacks callbacks;
    callbacks.progress = [this](int current, int total, const QString& fileName) {
        emit progress(current, total, fileName);
    };
    callbacks.cancelled = [this]() {
        return m_cancelFlag != nullptr && m_cancelFlag->loadRelaxed();
    };

    QString error;
    const device::ExportReport report =
            device::ExportManager::write(m_library, m_options, &error, callbacks);

    if (report.cancelled) {
        emit finished(false, QStringLiteral("Exportación cancelada"), true);
        return;
    }
    if (!error.isEmpty()) {
        emit finished(false, error, false);
        return;
    }

    QStringList issues;
    if (!device::ExportValidator::validateExport(m_options.destination, &issues)) {
        // A full list of issues can be hundreds of lines long. Show a short
        // summary plus the first few entries, which is what the UI can display.
        QStringList shown = issues.mid(0, 4);
        if (issues.size() > shown.size()) {
            shown.append(QStringLiteral("… y %1 más").arg(issues.size() - shown.size()));
        }
        emit finished(false,
                QStringLiteral("La validación encontró %1 problemas: %2")
                        .arg(issues.size())
                        .arg(shown.join(QStringLiteral(" · "))),
                false);
        return;
    }

    QString message = QStringLiteral("Exportadas %1 pistas, %2 listas y %3 análisis")
                              .arg(report.trackCount)
                              .arg(report.playlistCount)
                              .arg(report.analysisCount);
    if (report.missingTracks > 0) {
        message += QStringLiteral(" · %1 pistas omitidas (archivo de audio no encontrado)")
                           .arg(report.missingTracks);
    }
    emit finished(true, message, false);
}

} // namespace ui
} // namespace opendeck
