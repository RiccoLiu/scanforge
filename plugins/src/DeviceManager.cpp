#include "scanforge/DeviceManager.hpp"

#include <QDir>
#include <QPluginLoader>

namespace sf {

DeviceManager::DeviceManager(QObject* parent) : QObject(parent) {}

int DeviceManager::scan(const QString& directory) {
    const QDir dir(directory);
    if (!dir.exists()) {
        emit logMessage("WARN", "plugin directory does not exist: " + directory);
        return 0;
    }
    const QStringList candidates = dir.entryList(QDir::Files);
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
        devices_.push_back(DeviceHandle{fileName, device->info(), device, loader});
        emit logMessage("INFO", "loaded device plugin: " + fileName +
                                 " (" + QString::fromStdString(device->info().model) + ")");
    }
    return static_cast<int>(devices_.size());
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
        const Status status = handle.device->open();
        if (!status) {
            emit logMessage("ERROR", QString::fromStdString(status.error().toString()));
            return nullptr;
        }
        return handle.device;
    }
    emit logMessage("WARN", "device not found: " + id);
    return nullptr;
}

void DeviceManager::releaseAll() {
    for (auto& handle : devices_) {
        if (handle.device) handle.device->close();
    }
    // Loaders are intentionally kept alive until the manager is destroyed.
}

} // namespace sf
