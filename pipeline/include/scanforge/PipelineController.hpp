#pragma once

// Orchestrates the two worker threads and owns the ONE state machine that
// drives UI enable/disable rules. GUI thread must never block here: stop()
// only flips the token, thread shutdown is signalled by finished().
// Reference: Qt QThread docs, CloudCompare mainwindow (GPL - concept only),
// neuvision apps/ application wiring.

#include <QObject>
#include <QThread>
#include <atomic>

#include "scanforge/Calibration.hpp"
#include "scanforge/DeviceStateMachine.hpp"
#include "scanforge/IDevice.hpp"
#include "AcquisitionWorker.hpp"
#include "ProcessingWorker.hpp"

namespace sf {

class PipelineController : public QObject {
    Q_OBJECT
public:
    explicit PipelineController(QObject* parent = nullptr);
    ~PipelineController() override;

    bool start(IDevice* device, const CalibrationSet& calibration);
    void stop();

    // The GUI calls this after DeviceManager::acquire() so that the ONE state
    // machine (and therefore every button) reflects "device connected".
    void noteDeviceConnected();

    [[nodiscard]] DeviceState state() const noexcept { return fsm_.state(); }
    [[nodiscard]] bool isRunning() const noexcept { return running_.load(); }

signals:
    void cloudReady(sf::PointCloudPtr cloud, double elapsedMs);
    void frameReady(sf::Frame frame);
    void previewReady(QImage image);
    void stateChanged(sf::DeviceState state);
    void logMessage(const QString& level, const QString& text);
    void failed(sf::Error error);

private:
    QThread acquisitionThread_;
    QThread processingThread_;
    AcquisitionWorker* acquisition_ = nullptr;
    ProcessingWorker*  processing_  = nullptr;

    CancellationToken token_;
    CancellationToken processingToken_;
    DeviceStateMachine fsm_;
    std::atomic_bool running_{false};
    std::shared_ptr<ReconstructionChain> chain_;
};

} // namespace sf
