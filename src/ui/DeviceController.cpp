#include "ui/DeviceController.h"

#include "devices/UsbDeviceManager.h"

namespace opendeck {
namespace ui {

DeviceController::DeviceController(QObject* parent) : QAbstractListModel(parent) {
    m_timer.setInterval(3000);
    m_timer.setSingleShot(false);
    connect(&m_timer, &QTimer::timeout, this, &DeviceController::refresh);
    refresh();
    m_timer.start();
}

int DeviceController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_devices.size());
}

QVariant DeviceController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size()) {
        return {};
    }
    const devices::UsbDevice& device = m_devices.at(index.row());
    switch (role) {
    case NameRole: return device.name;
    case RootPathRole: return device.rootPath;
    case DeviceRole: return device.device;
    case FileSystemRole: return device.fileSystem;
    case SizeRole: return device.sizeLabel();
    case FreeRole: return device.bytesFree;
    case WritableRole: return device.writable;
    case Fat32Role: return device.fat32;
    case SummaryRole: return device.summary();
    default: return {};
    }
}

QHash<int, QByteArray> DeviceController::roleNames() const {
    return {
            {NameRole, "name"},
            {RootPathRole, "rootPath"},
            {DeviceRole, "device"},
            {FileSystemRole, "fileSystem"},
            {SizeRole, "size"},
            {FreeRole, "freeBytes"},
            {WritableRole, "writable"},
            {Fat32Role, "fat32"},
            {SummaryRole, "summary"},
    };
}

void DeviceController::refresh() {
    const devices::UsbDeviceManager manager;
    const QList<devices::UsbDevice> found = manager.scan();

    // Only reset the model when something actually changed: the timer fires
    // every few seconds and resetting it would interrupt QML selections.
    bool changed = found.size() != m_devices.size();
    if (!changed) {
        for (int i = 0; i < found.size(); ++i) {
            // Deliberately ignore bytesFree: it changes constantly while an
            // export is writing and would reset the model every three seconds.
            if (found.at(i).rootPath != m_devices.at(i).rootPath
                    || found.at(i).device != m_devices.at(i).device) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        return;
    }

    beginResetModel();
    m_devices = found;
    endResetModel();
    emit devicesChanged();
}

QString DeviceController::rootPathAt(int row) const {
    if (row < 0 || row >= m_devices.size()) {
        return {};
    }
    return m_devices.at(row).rootPath;
}

} // namespace ui
} // namespace opendeck
