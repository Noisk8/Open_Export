#include "devices/UsbDevice.h"

namespace opendeck {
namespace devices {

QString UsbDevice::sizeLabel() const {
    if (bytesTotal <= 0) {
        return {};
    }
    const double gb = static_cast<double>(bytesTotal) / (1024.0 * 1024.0 * 1024.0);
    return QStringLiteral("%1 GB").arg(gb, 0, 'f', 1);
}

QString UsbDevice::summary() const {
    return QStringLiteral("%1 · %2 · %3")
            .arg(fileSystem.toUpper(), sizeLabel(), rootPath);
}

} // namespace devices
} // namespace opendeck
