#pragma once

// The no-hardware camera. It is a REAL Qt plugin, so QPluginLoader, parameter
// node discovery, cross-thread acquisition and error handling can all be
// exercised without buying a camera.
// Reference: Aravis arv-fake-camera / arv-viewer (LGPL-2.1+) for the idea of a
// simulated GigE device; neuvision's camera plugin layout for the plugin shape.

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtPlugin>

#include "scanforge/IDevice.hpp"
#include "scanforge/Synthetic.hpp"

class MockCamera final : public QObject, public sf::IDevice {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.scanforge.IDevice/1.0" FILE "MockCamera.json")
    Q_INTERFACES(sf::IDevice)

public:
    explicit MockCamera(QObject* parent = nullptr);

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
    void rebuildScene();
    void initialiseParameters();

    QMap<QString, QVariant> params_;
    sf::SynthScene          scene_ = sf::makeDefaultScene();
    std::vector<sf::Frame>  frames_;
    std::size_t             cursor_ = 0;
    bool                    opened_ = false;
};
