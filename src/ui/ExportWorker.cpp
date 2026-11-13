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
        emit finished(false,
                issues.isEmpty() ? QStringLiteral("La exportación tiene problemas")
                                 : issues.join(QStringLiteral("\n")),
                false);
        return;
    }

    const QString message =
            QStringLiteral("Exportadas %1 pistas, %2 listas y %3 análisis")
                    .arg(report.trackCount)
                    .arg(report.playlistCount)
                    .arg(report.analysisCount);
    emit finished(true, message, false);
}

} // namespace ui
} // namespace opendeck
