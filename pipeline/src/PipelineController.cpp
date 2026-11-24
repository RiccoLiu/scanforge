#include "scanforge/PipelineController.hpp"

#include <QMetaObject>
#include <QThread>

#include "scanforge/Filters.hpp"

namespace sf {

PipelineController::PipelineController(QObject* parent) : QObject(parent) {
    chain_ = std::make_shared<ReconstructionChain>();
    chain_->setReconstructor(std::make_shared<LaserReconstructor>());
    chain_->addOperator(std::make_shared<VoxelFilterOperator>(1.5));

    // No parent: a QObject with a parent cannot be moved to another QThread.
    acquisition_ = new AcquisitionWorker();
    processing_  = new ProcessingWorker();
    acquisition_->moveToThread(&acquisitionThread_);
    processing_->moveToThread(&processingThread_);

    // Cross-lane data flow. PointCloudPtr is shared_ptr<const ...>, so the
    // hand-off is zero-copy and immutable by construction.
    connect(acquisition_, &AcquisitionWorker::frameReady,
            processing_,  &ProcessingWorker::onFrame, Qt::QueuedConnection);
    connect(acquisition_, &AcquisitionWorker::frameReady,
            this,         &PipelineController::frameReady, Qt::QueuedConnection);
    connect(acquisition_, &AcquisitionWorker::previewReady,
            this,         &PipelineController::previewReady, Qt::QueuedConnection);
    connect(processing_,  &ProcessingWorker::cloudReady,
            this,         &PipelineController::cloudReady, Qt::QueuedConnection);
    connect(acquisition_, &AcquisitionWorker::failed,
            this,         &PipelineController::failed, Qt::QueuedConnection);
    connect(processing_,  &ProcessingWorker::failed,
            this,         &PipelineController::failed, Qt::QueuedConnection);

    connect(acquisition_, &AcquisitionWorker::finished,
            this, [this](quint64 count) {
                running_.store(false);
                if (fsm_.can(DeviceEvent::StopAcquire)) fsm_.apply(DeviceEvent::StopAcquire);
                emit stateChanged(fsm_.state());
                emit logMessage("INFO",
                    QString("acquisition finished after %1 frames").arg(count));
                // Keep both worker threads alive: start() posts the next
                // acquisition to these very event loops. Calling quit() here
                // would make the second Start click enqueue a task on a
                // stopped loop -> no frames, no preview. Threads are stopped
                // only in the destructor.
            });

    acquisitionThread_.start();
    processingThread_.start();
}

PipelineController::~PipelineController() {
    stop();
    acquisitionThread_.quit();
    processingThread_.quit();
    acquisitionThread_.wait(5000);
    processingThread_.wait(5000);
    delete acquisition_;   // parentless on purpose
    delete processing_;
}

void PipelineController::noteDeviceConnected() {
    if (fsm_.state() == DeviceState::Disconnected)
        fsm_.apply(DeviceEvent::Connect);
    emit stateChanged(fsm_.state());
}

bool PipelineController::start(IDevice* device, const CalibrationSet& calibration) {
    if (!device || running_.exchange(true)) return false;

    token_ = CancellationToken{};
    processingToken_ = token_;
    processing_->configure(calibration, chain_, processingToken_);

    if (fsm_.state() == DeviceState::Disconnected)
        fsm_.apply(DeviceEvent::Connect);
    const Status status = fsm_.apply(DeviceEvent::StartAcquire);
    if (!status) {
        running_.store(false);
        emit failed(status.error());
        return false;
    }
    emit stateChanged(fsm_.state());

    // A functor posted to the worker's event queue; the token is captured by
    // value so cancellation remains visible even after start() returns.
    QMetaObject::invokeMethod(acquisition_,
        [this, device]() { acquisition_->start(device, token_); },
        Qt::QueuedConnection);
    return true;
}

void PipelineController::stop() {
    if (!running_.exchange(false)) return;
    // Graceful: flip the token, let the acquisition loop notice it, then let
    // finished() shut the threads down. The GUI thread never waits here.
    token_.cancel();
    processingToken_.cancel();
    if (fsm_.can(DeviceEvent::StopAcquire)) fsm_.apply(DeviceEvent::StopAcquire);
    emit stateChanged(fsm_.state());
    emit logMessage("INFO", "stop requested; waiting for in-flight frames");
}

} // namespace sf
