#include "library/LibraryRelocator.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStorageInfo>

#include "devices/UsbDeviceManager.h"
#include "library/DatabaseManager.h"
#include "library/TrackRepository.h"

namespace opendeck {
namespace library {

namespace {

constexpr int kMaxIndexedFiles = 30000;
constexpr qint64 kMaxHashFileBytes = 256 * 1024 * 1024;

QStringList mountedRoots() {
    QStringList roots;
    // Removable volumes first (most likely to have been relabelled).
    const devices::UsbDeviceManager manager;
    for (const devices::UsbDevice& device : manager.scan()) {
        roots.append(device.rootPath);
    }
    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady() || info.isReadOnly()) {
            continue;
        }
        const QString path = info.rootPath();
        if (!roots.contains(path) && path != QStringLiteral("/")) {
            roots.append(path);
        }
    }
    // Also the parent directories of the paths that still resolve, which is
    // where the rest of a library usually lives.
    return roots;
}

// Tries the last `depth` components of `original` under `root`.
QString trySuffix(const QString& root, const QString& original, int depth) {
    const QStringList parts = original.split(QChar('/'), Qt::SkipEmptyParts);
    if (depth > parts.size()) {
        return {};
    }
    QString candidate = root;
    for (int i = parts.size() - depth; i < parts.size(); ++i) {
        candidate += QChar('/') + parts.at(i);
    }
    return QFileInfo::exists(candidate) && QFileInfo(candidate).isFile()
            ? QFileInfo(candidate).absoluteFilePath() : QString();
}

QHash<QString, QStringList> indexByFileName(const QStringList& roots) {
    QHash<QString, QStringList> index;
    int count = 0;
    for (const QString& root : roots) {
        const QString contents = root + QStringLiteral("/Contents");
        if (!QDir(contents).exists()) {
            continue;
        }
        QDirIterator it(contents, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && count < kMaxIndexedFiles) {
            const QString path = it.next();
            ++count;
            index[QFileInfo(path).fileName()].append(path);
        }
    }
    return index;
}

QByteArray sha256(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxHashFileBytes) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return hash.result();
}

} // namespace

QString RelocationReport::summary() const {
    if (checked == 0) {
        return QStringLiteral("Todas las pistas están localizadas");
    }
    if (relocated == 0) {
        return QStringLiteral("%1 pistas con el archivo original no encontrado; "
                              "vuelve a importar la música o conecta su dispositivo")
                .arg(checked);
    }
    return QStringLiteral("Relocalizadas %1 de %2 pistas")
            .arg(relocated)
            .arg(checked);
}

RelocationReport LibraryRelocator::relocate(DatabaseManager& db, QString* error,
                                            const QStringList& searchRoots) {
    RelocationReport report;
    if (error) {
        error->clear();
    }
    TrackRepository repository(db);
    const QList<core::Track> tracks = repository.allTracks();

    QList<core::Track> missing;
    for (const core::Track& track : tracks) {
        if (!QFileInfo::exists(track.filePath) || !QFileInfo(track.filePath).isFile()) {
            missing.append(track);
        }
    }
    report.checked = missing.size();
    if (missing.isEmpty()) {
        return report;
    }

    const QStringList roots = searchRoots.isEmpty() ? mountedRoots() : searchRoots;
    const QHash<QString, QStringList> byName = indexByFileName(roots);

    for (core::Track& track : missing) {
        QString found;

        // 1) Same relative path under another mount point.
        for (int depth = 1; depth <= 5 && found.isEmpty(); ++depth) {
            for (const QString& root : roots) {
                found = trySuffix(root, track.filePath, depth);
                if (!found.isEmpty()) {
                    break;
                }
            }
        }

        // 2) Original file name inside a previous export's Contents/.
        if (found.isEmpty()) {
            const QStringList candidates = byName.value(QFileInfo(track.filePath).fileName());
            if (candidates.size() == 1) {
                found = candidates.first();
            } else if (candidates.size() > 1 && !track.fileHash.isEmpty()) {
                // 3) Disambiguate by content hash when names collide.
                for (const QString& candidate : candidates) {
                    if (QString::fromLatin1(sha256(candidate).toHex()) == track.fileHash) {
                        found = candidate;
                        break;
                    }
                }
            }
        }

        if (found.isEmpty()) {
            continue;
        }
        track.filePath = found;
        if (repository.updateTrack(track)) {
            ++report.relocated;
            report.relocatedTitles.append(track.title);
        }
    }
    return report;
}

} // namespace library
} // namespace opendeck
