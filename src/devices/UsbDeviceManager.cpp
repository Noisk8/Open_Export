#include "devices/UsbDeviceManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

namespace opendeck {
namespace devices {

bool UsbDeviceManager::isSupportedFileSystem(const QString& fileSystem) {
    const QString fs = fileSystem.toLower();
    return fs == QStringLiteral("vfat") || fs == QStringLiteral("fat32")
            || fs == QStringLiteral("fat") || fs == QStringLiteral("msdos")
            || fs == QStringLiteral("exfat");
}

bool UsbDeviceManager::isRemovableBlockDevice(const QString& devicePath) {
    if (devicePath.isEmpty() || !devicePath.startsWith(QStringLiteral("/dev/"))) {
        return false;
    }
    QString name = devicePath.section(QChar('/'), -1);
    if (name.startsWith(QStringLiteral("loop")) || name.startsWith(QStringLiteral("dm-"))) {
        return false;
    }

    // Resolving the symlink in /sys/class/block reveals the bus: a USB stick
    // looks like /sys/devices/pci…/usb1/1-3/…/block/sdb/sdb1.
    const QFileInfo link(QStringLiteral("/sys/class/block/%1").arg(name));
    const QString target = link.canonicalFilePath();
    if (target.contains(QStringLiteral("/usb"), Qt::CaseInsensitive)) {
        return true;
    }

    // Fall back to the removable flag of the parent disk (sdb1 -> sdb).
    QString parent = name;
    while (!parent.isEmpty() && parent.at(parent.size() - 1).isDigit()) {
        parent.chop(1);
    }
    if (parent.isEmpty()) {
        return false;
    }
    QFile flag(QStringLiteral("/sys/class/block/%1/removable").arg(parent));
    if (!flag.open(QIODevice::ReadOnly)) {
        return false;
    }
    return flag.readAll().trimmed() == "1";
}

QList<UsbDevice> UsbDeviceManager::scan() const {
    QList<UsbDevice> result;
    for (const QStorageInfo& info : QStorageInfo::mountedVolumes()) {
        if (!info.isValid() || !info.isReady() || info.isReadOnly()) {
            continue;
        }
        const QString fs = QString::fromLatin1(info.fileSystemType()).toLower();
        if (!isSupportedFileSystem(fs)) {
            continue;
        }
        const QString device = QString::fromLatin1(info.device());
        if (!isRemovableBlockDevice(device)) {
            continue;
        }

        UsbDevice entry;
        entry.rootPath = info.rootPath();
        entry.device = device;
        entry.fileSystem = fs;
        entry.bytesTotal = info.bytesTotal();
        entry.bytesFree = info.bytesAvailable();
        entry.writable = QFileInfo(info.rootPath()).isWritable();
        entry.fat32 = (fs != QStringLiteral("exfat"));
        entry.name = QFileInfo(info.rootPath()).fileName();
        if (entry.name.isEmpty()) {
            entry.name = info.displayName();
        }
        result.append(entry);
    }
    return result;
}

} // namespace devices
} // namespace opendeck
