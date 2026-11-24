#include "scanforge/DeviceManager.hpp"

#include <QDir>
#include <QPluginLoader>

namespace sf {

DeviceManager::DeviceManager(QObject* parent) : QObject(parent) {}

int DeviceManager::scan(const QString& directory) {
    // A re-scan replaces the previous enumeration: close everything, drop
    // the old handles (this unloads their plugin DLLs - callers must not
    // hold on to IDevice* across scan()), then start fresh.
    for (auto& handle : devices_) {
        if (handle.device) handle.device->close();
    }
    devices_.clear();
    const QDir dir(directory);
    if (!dir.exists()) {
        emit logMessage("WARN", "plugin directory does not exist: " + directory);
        return 0;
    }
    const QStringList candidates = dir.entryList(QDir::Files);
    int total = 0;
    for (const QString& fileName : candidates) {
        auto loader = std::make_shared<QPluginLoader>(dir.absoluteFilePath(fileName));
        QObject* instance = loader->instance();
        if (!instance) {
            // Most non-plugin DLLs land here; that is normal, keep it DEBUG.
            emit logMessage("DEBUG", fileName + ": " + loader->errorString());
            continue;
        }
        auto* device = qobject_cast<IDevice*>(instance);
        if (!device) continue;

        // One plugin DLL can drive several physical devices (hik_camera
        // enumerates the whole subnet). Probe how many the plugin exposes:
        // repeatedly open() successive indices, read the hardware identity,
        // close() again. Each physical device becomes one list entry with a
        // unique id "<plugin>#<n>" and the uri that selects it.
        const DeviceInfo base = device->info();   // plugin placeholder identity
        const QString baseId = QString::fromStdString(base.id);
        int found = 0;
        std::string firstSerial;
        // Probe cap: a well-behaved plugin fails open() once the index is
        // past its last device. Plugins that ignore the uri (e.g. the mock)
        // would "succeed" forever - the cap keeps the scan finite, and the
        // serial check below stops it at the first duplicate identity.
        constexpr int kMaxDevicesPerPlugin = 64;
        for (int n = 0; n < kMaxDevicesPerPlugin; ++n) {
            const QString uri = baseId + "://" + QString::number(n);
            const Status st = device->open(uri);
            if (!st) break;                       // no device at index n -> done
            DeviceInfo info = device->info();     // real model/serial after open
            // If the plugin ignores the index and keeps returning the same
            // physical device (same serial), stop: one row is enough.
            if (found > 0 && info.serial == firstSerial) {
                device->close();
                break;
            }
            if (found == 0) firstSerial = info.serial;
            info.id     = baseId.toStdString() + "#" + std::to_string(n);
            info.vendor = base.vendor;
            devices_.push_back(DeviceHandle{fileName, info, device, loader, uri});
            device->close();
            ++found;
        }
        if (found == 0) {
            // Plugin enumerated nothing: keep one entry so the plugin is
            // still visible and open("") can try its default device.
            devices_.push_back(DeviceHandle{fileName, base, device, loader, {}});
            emit logMessage("INFO", "loaded device plugin: " + fileName +
                                    " (no devices found)");
        } else {
            emit logMessage("INFO", QString("loaded device plugin: %1 (%2 device(s))")
                                    .arg(fileName).arg(found));
        }
        total += found > 0 ? found : 1;
    }
    return total;
}

QList<DeviceInfo> DeviceManager::discovered() const {
    QList<DeviceInfo> result;
    result.reserve(devices_.size());
    for (const auto& handle : devices_) result.push_back(handle.info);
    return result;
}

IDevice* DeviceManager::acquire(const QString& id) {
    for (auto& handle : devices_) {
        if (QString::fromStdString(handle.info.id) != id) continue;
        // The plugin instance is shared by every row of the same DLL: before
        // opening the requested device, close whatever it currently holds
        // (device switching: Stop old -> click another -> open new).
        if (handle.device->info().id != handle.info.id || openedHandle_) {
            handle.device->close();
        }
        const Status status = handle.device->open(handle.uri);
        if (!status) {
            emit logMessage("ERROR", QString::fromStdString(status.error().toString()));
            return nullptr;
        }
        openedHandle_ = &handle;
        return handle.device;
    }
    emit logMessage("WARN", "device not found: " + id);
    return nullptr;
}

void DeviceManager::releaseAll() {
    for (auto& handle : devices_) {
        if (handle.device) handle.device->close();
    }
    openedHandle_ = nullptr;
    // Loaders are intentionally kept alive until the manager is destroyed.
}

} // namespace sf
