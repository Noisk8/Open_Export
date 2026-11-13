#pragma once

#include <QList>

#include "devices/UsbDevice.h"

namespace opendeck {
namespace devices {

// UsbDeviceManager finds the removable volumes that a Pioneer player can read.
//
// On Linux Qt's QStorageInfo lists every mount, including the internal EFI
// partition (also vfat). To tell them apart the sysfs path of the block device
// is resolved: USB sticks live under a /usb…/ path, while internal disks live
// under /pci…/ or /ata…/. The removable flag of the parent disk is used as a
// fallback for other transports (SD cards, for example).
class UsbDeviceManager {
  public:
    // Enumerates the mounted volumes that are suitable for an export.
    [[nodiscard]] QList<UsbDevice> scan() const;

    // True when the given /dev/… path belongs to a removable USB/SD device.
    [[nodiscard]] static bool isRemovableBlockDevice(const QString& devicePath);

    // True when the file system is one a Pioneer player can read.
    [[nodiscard]] static bool isSupportedFileSystem(const QString& fileSystem);
};

} // namespace devices
} // namespace opendeck
