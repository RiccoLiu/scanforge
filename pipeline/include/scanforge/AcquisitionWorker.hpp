#pragma once

// Acquisition lane. Runs on its own QThread via moveToThread().
//
// Cancellation uses CancellationToken, NOT a queued "stop" slot: start() is a
// long-running loop that never returns to the event loop, so a queued slot
// would never be delivered. This is the single most common Qt threading bug.
// Reference: Qt docs "QThread / Worker Object" + Aravis arv-camera streaming.

#include <QImage>
#include <QObject>

#include "scanforge/Contracts.hpp"
#include "scanforge/IDevice.hpp"
#include "scanforge/Result.hpp"
#include "scanforge/Types.hpp"

namespace sf {

class AcquisitionWorker : public QObject {
    Q_OBJECT
public:
    explicit AcquisitionWorker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void start(IDevice* device, CancellationToken token);

signals:
    void frameReady(sf::Frame frame);
    void previewReady(QImage image);
    void failed(sf::Error error);
    void finished(quint64 frameCount);
};

} // namespace sf
