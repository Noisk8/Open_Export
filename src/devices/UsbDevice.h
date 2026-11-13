#pragma once

#include <QString>

namespace opendeck {
namespace devices {

// UsbDevice describes a mounted volume that can hold a Pioneer export.
struct UsbDevice {
    QString name;         // volume label, or the name of the mount point
    QString rootPath;     // mount point, e.g. /run/media/user/ABCD-1234
    QString device;       // /dev/sdb1
    QString fileSystem;   // vfat, exfat, ...
    qint64 bytesTotal = 0;
    qint64 bytesFree = 0;
    bool writable = false;
    bool fat32 = false;   // Pioneer players require FAT32 (MBR + 512 byte sectors)

    [[nodiscard]] QString sizeLabel() const;
    [[nodiscard]] QString summary() const;
};

} // namespace devices
} // namespace opendeck
