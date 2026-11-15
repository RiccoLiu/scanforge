#pragma once

// Qt plugin contract. Parameters follow the GenICam NODE-TREE idea: a device
// exposes typed key/value nodes with ranges and units instead of a hard-coded
// setExposure()/setGain() API. Swapping a camera then means swapping a plugin.
// Reference: Aravis src/arvgc*.c / arv-camera.c (LGPL-2.1-or-later),
// GenICam GenApi node maps, CloudCompare ccPluginInterface.h (GPL - READ ONLY).

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtPlugin>

#include "scanforge/Contracts.hpp"
#include "scanforge/DeviceStateMachine.hpp"
#include "scanforge/Result.hpp"
#include "scanforge/Types.hpp"

namespace sf {

enum class ParamType : std::uint8_t {
    Integer = 0, Real, Boolean, Enum, Text, Command
};

struct DeviceParam {
    QString     key;           // GenICam-like name, e.g. "ExposureTimeUs"
    QString     displayName;
    QString     unit;
    QString     description;
    ParamType   type = ParamType::Real;
    QVariant    value;
    QVariant    minimum;
    QVariant    maximum;
    QStringList options;       // only for Enum
    bool        readOnly = false;
};

using DeviceParamList = QList<DeviceParam>;

class IDevice {
public:
    virtual ~IDevice() = default;

    [[nodiscard]] virtual DeviceInfo          info() const = 0;
    [[nodiscard]] virtual SourceCapabilities  capabilities() const = 0;
    [[nodiscard]] virtual DeviceParamList     parameters() const = 0;

    // Never throw from here: these calls cross thread boundaries.
    [[nodiscard]] virtual Status       setParameter(const QString& key,
                                                    const QVariant& value) = 0;
    [[nodiscard]] virtual Status       open(const QString& uri = {}) = 0;
    virtual void                       close() = 0;
    [[nodiscard]] virtual Result<Frame> nextFrame(const CancellationToken& token) = 0;
};

} // namespace sf

Q_DECLARE_INTERFACE(sf::IDevice, "org.scanforge.IDevice/1.0")

// Required for Qt::QueuedConnection across the GUI / acquisition / processing
// threads. main.cpp also calls qRegisterMetaType() for each of them.
Q_DECLARE_METATYPE(sf::Frame)
Q_DECLARE_METATYPE(sf::PointCloudPtr)
Q_DECLARE_METATYPE(sf::Error)
Q_DECLARE_METATYPE(sf::DeviceState)
