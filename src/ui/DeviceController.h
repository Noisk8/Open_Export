#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QTimer>

#include "devices/UsbDevice.h"

namespace opendeck {
namespace ui {

// DeviceController exposes the removable volumes to QML. It refreshes
// automatically so that plugging or unplugging a stick is noticed without
// restarting the application.
class DeviceController : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int deviceCount READ rowCount NOTIFY devicesChanged)
  public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        RootPathRole,
        DeviceRole,
        FileSystemRole,
        SizeRole,
        FreeRole,
        WritableRole,
        Fat32Role,
        SummaryRole,
    };
    Q_ENUM(Roles)

    explicit DeviceController(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Forces a rescan (also called by the "Actualizar" button).
    Q_INVOKABLE void refresh();

    // Mount point of the device at a given row, or an empty string.
    Q_INVOKABLE QString rootPathAt(int row) const;

  signals:
    void devicesChanged();

  private:
    QList<devices::UsbDevice> m_devices;
    QTimer m_timer;
};

} // namespace ui
} // namespace opendeck
