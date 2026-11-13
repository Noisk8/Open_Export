#include <QtTest>

#include "devices/UsbDeviceManager.h"

using namespace opendeck;

class TestDevices : public QObject {
    Q_OBJECT

  private slots:
    void recognisesPioneerFileSystems() {
        using devices::UsbDeviceManager;
        QVERIFY(UsbDeviceManager::isSupportedFileSystem(QStringLiteral("vfat")));
        QVERIFY(UsbDeviceManager::isSupportedFileSystem(QStringLiteral("FAT32")));
        QVERIFY(UsbDeviceManager::isSupportedFileSystem(QStringLiteral("exfat")));
        QVERIFY(!UsbDeviceManager::isSupportedFileSystem(QStringLiteral("ext4")));
        QVERIFY(!UsbDeviceManager::isSupportedFileSystem(QStringLiteral("ntfs")));
        QVERIFY(!UsbDeviceManager::isSupportedFileSystem(QStringLiteral("tmpfs")));
    }

    void rejectsNonRemovableDevices() {
        using devices::UsbDeviceManager;
        QVERIFY(!UsbDeviceManager::isRemovableBlockDevice(QString()));
        QVERIFY(!UsbDeviceManager::isRemovableBlockDevice(QStringLiteral("/media/usb")));
        QVERIFY(!UsbDeviceManager::isRemovableBlockDevice(QStringLiteral("/dev/loop0")));
        QVERIFY(!UsbDeviceManager::isRemovableBlockDevice(QStringLiteral("/dev/dm-0")));
    }

    void scanNeverReturnsSystemMounts() {
        const devices::UsbDeviceManager manager;
        const QList<devices::UsbDevice> found = manager.scan();
        for (const devices::UsbDevice& device : found) {
            QVERIFY(devices::UsbDeviceManager::isSupportedFileSystem(device.fileSystem));
            QVERIFY(!device.rootPath.isEmpty());
            // The internal EFI partition is also vfat but must not be offered.
            QVERIFY(device.rootPath != QStringLiteral("/"));
            QVERIFY(device.rootPath != QStringLiteral("/boot/efi"));
            QVERIFY(device.rootPath != QStringLiteral("/boot"));
            QVERIFY2(devices::UsbDeviceManager::isRemovableBlockDevice(device.device),
                    qPrintable(device.device));
        }
    }
};

QTEST_MAIN(TestDevices)
#include "test_devices.moc"
