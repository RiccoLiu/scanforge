#pragma once

// Hikvision MVS (GenICam) camera plugin for ScanForge.
//
// All SDK access goes through the C++ wrapper CMvCamera (include/MvCamera.h),
// which is the sample wrapper from MVS Development/Samples/C++/AreaScanCamera.
// This plugin only maps CMvCamera calls onto the sf::IDevice node-tree
// contract, so the app needs no vendor-specific code outside this plugin.
//
// CMvCamera methods used (BasicDemo equivalents in parentheses):
//   CMvCamera::EnumDevices (OnBnClickedEnumButton)
//   Open / Close           (OnBnClickedOpenButton / CloseDevice)
//   GetOptimalPacketSize + SetIntValue("GevSCPSPacketSize")
//   StartGrabbing          (OnBnClickedStartGrabbingButton, lazy on first frame)
//   GetImageBuffer + ConvertPixelType + FreeImageBuffer (GrabThreadProcess)
//   Get*/Set*Value nodes   (OnBnClickedGet/SetParameterButton)
//   CommandExecute("TriggerSoftware") (OnBnClickedSoftwareOnceButton)
//
// Preview: Mono8 passes through; deep mono -> Mono16; Bayer/color -> RGB8.
// The pipeline layer converts all three into QImage preview frames.
//
// URI scheme handled by open():
//   ""                  -> first enumerated device
//   <n>                 -> device at enumeration index n
//   <serial>            -> device with that serial number
//   <ip>                -> GigE device with that IP address
//   <pluginId>://<sel>  -> DeviceManager form; selector is one of the above

#include <QObject>
#include <QString>
#include <QVariant>
#include <QtPlugin>

#include <memory>
#include <mutex>

#include "MvCamera.h"   // C++ wrapper bundled in plugins/hik_camera/include/
#include "scanforge/IDevice.hpp"

class HikCamera final : public QObject, public sf::IDevice {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.scanforge.IDevice/1.0" FILE "HikCamera.json")
    Q_INTERFACES(sf::IDevice)

public:
    explicit HikCamera(QObject* parent = nullptr);
    ~HikCamera() override;

    [[nodiscard]] sf::DeviceInfo         info() const override;
    [[nodiscard]] sf::SourceCapabilities capabilities() const override;
    [[nodiscard]] sf::DeviceParamList    parameters() const override;
    [[nodiscard]] sf::Status             setParameter(const QString& key,
                                                      const QVariant& value) override;
    [[nodiscard]] sf::Status             open(const QString& uri = {}) override;
    void                                 close() override;
    [[nodiscard]] sf::Result<sf::Frame>  nextFrame(
        const sf::CancellationToken& token) override;

private:
    // Stop grabbing, close and release the CMvCamera instance. Requires mutex_.
    void teardownLocked();

    std::unique_ptr<CMvCamera> camera_;   // owns the SDK device handle
    bool    opened_   = false;
    bool    grabbing_ = false;            // lazy StartGrabbing on first nextFrame()
    mutable std::mutex mutex_;            // guards camera_ / flags
    sf::DeviceInfo info_;
};
