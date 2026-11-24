#pragma once

// Runtime plugin discovery. The pattern mirrors CloudCompare's
// ccPluginManager + ccPluginInterface (GPL - study only), but the code here is
// original and much smaller: scan a directory, QPluginLoader::instance(),
// qobject_cast to IDevice, keep the loader alive for the whole session.
// Forgetting to keep the loader alive unloads the DLL and leaves a dangling
// pointer - the classic Qt plugin crash.

#include <QDir>
#include <QList>
#include <QObject>
#include <QPluginLoader>
#include <QString>
#include <memory>

#include "IDevice.hpp"

namespace sf {

struct DeviceHandle {
    QString pluginFile;
    DeviceInfo info;
    IDevice* device = nullptr;                    // owned by the loader
    std::shared_ptr<QPluginLoader> loader;        // MUST outlive `device`
    QString uri;   // selects this entry inside its plugin ("" = default)
};

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(QObject* parent = nullptr);

    // Returns the number of usable IDevice plugins found.
    int scan(const QString& directory);
    [[nodiscard]] QList<DeviceInfo> discovered() const;

    // Opens and returns the device with the given id (or nullptr).
    IDevice* acquire(const QString& id);
    void     releaseAll();

signals:
    void logMessage(const QString& level, const QString& text);

private:
    QList<DeviceHandle> devices_;
    DeviceHandle* openedHandle_ = nullptr;   // entry whose device is open now
};

} // namespace sf
