#pragma once
#include <QAtomicInteger>
#include <QElapsedTimer>
#include <QProcess>
#include <functional>

namespace opendeck {
// Polling also drains PCM output incrementally instead of retaining whole songs.
inline bool waitForProcess(QProcess& process, int timeout,
        const QAtomicInteger<bool>* cancel, QString* error,
        const std::function<void(const QByteArray&)>& consume = {}) {
    QElapsedTimer timer;
    timer.start();
    if (!process.waitForStarted(5000)) {
        if (error) *error = process.errorString();
        return false;
    }
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        if (consume) consume(process.readAllStandardOutput());
        if ((cancel && cancel->loadRelaxed()) || timer.elapsed() >= timeout) {
            process.kill();
            process.waitForFinished(-1);
            if (error) *error = cancel && cancel->loadRelaxed()
                ? QStringLiteral("Operación cancelada") : QStringLiteral("Tiempo de espera agotado");
            return false;
        }
    }
    if (consume) consume(process.readAllStandardOutput());
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}
}
