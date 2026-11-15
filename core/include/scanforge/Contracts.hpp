#pragma once

// The three core contracts. All of them are pure C++ so that the very same
// implementation can run in the GUI, in the CLI, and inside unit tests.
// Reference: PDAL pdal/Stage.hpp + pdal/Filter.hpp (BSD) for chainable stages;
// Aravis GenICam node tree (LGPL-2.1+) for parameter naming (see IDevice.hpp).

#include "Types.hpp"
#include "Result.hpp"
#include "Calibration.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace sf {

// Cooperative cancellation. Shared, cheap, thread-safe. Used instead of
// QThread::terminate() or a queued "stop" slot (a busy worker never processes
// its event queue, so a queued stop would never arrive).
class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    void cancel() noexcept { flag_->store(true, std::memory_order_relaxed); }
    [[nodiscard]] bool isCancelled() const noexcept {
        return flag_->load(std::memory_order_relaxed);
    }
private:
    std::shared_ptr<std::atomic_bool> flag_;
};

struct SourceCapabilities {
    bool     supportsContinuous  = false;
    bool     supportsHardTrigger = false;
    double   maxFrameRateHz      = 0.0;
    uint32_t maxWidth            = 0;
    uint32_t maxHeight           = 0;
};

// Pure C++ source contract (used by the CLI and by the simulated source).
// The Qt plugin contract (IDevice) lives in plugins/ and wraps this shape.
class IDataSource {
public:
    virtual ~IDataSource() = default;
    [[nodiscard]] virtual DeviceInfo            info() const = 0;
    [[nodiscard]] virtual SourceCapabilities   capabilities() const = 0;
    [[nodiscard]] virtual Status               open(const std::string& uri) = 0;
    virtual void                               close() = 0;
    [[nodiscard]] virtual Result<Frame>        nextFrame(const CancellationToken& token) = 0;
};

// Frame + calibration -> point cloud (triangulation / structured light decode).
class IReconstructor {
public:
    virtual ~IReconstructor() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual Result<PointCloudPtr> reconstruct(
        const Frame& frame, const CalibrationSet& calibration,
        const CancellationToken& token) = 0;
};

// PointCloud -> PointCloud stages (voxel filter, outlier removal, transform).
class IPointCloudOperator {
public:
    virtual ~IPointCloudOperator() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual Result<PointCloudPtr> apply(
        PointCloudPtr input, const CancellationToken& token) = 0;
};

// A minimal pipeline: reconstructor followed by N operators. Deliberately
// synchronous here; the Qt threading wrapper lives in pipeline/.
class ReconstructionChain {
public:
    void setReconstructor(std::shared_ptr<IReconstructor> r) { reconstructor_ = std::move(r); }
    void addOperator(std::shared_ptr<IPointCloudOperator> op) { operators_.push_back(std::move(op)); }

    [[nodiscard]] Result<PointCloudPtr> run(const Frame& frame,
                                            const CalibrationSet& calibration,
                                            const CancellationToken& token) const {
        if (!reconstructor_) {
            return Result<PointCloudPtr>::failure(
                makeError(ErrorCode::InvalidArgument, "no reconstructor configured",
                          "ReconstructionChain::run"));
        }
        auto cloud = reconstructor_->reconstruct(frame, calibration, token);
        if (!cloud) return cloud;
        PointCloudPtr current = cloud.value();
        for (const auto& op : operators_) {
            if (token.isCancelled()) {
                return Result<PointCloudPtr>::failure(
                    makeError(ErrorCode::Cancelled, "cancelled", op->name()));
            }
            auto next = op->apply(current, token);
            if (!next) return next;
            current = next.value();
        }
        return Result<PointCloudPtr>::success(std::move(current));
    }

private:
    std::shared_ptr<IReconstructor> reconstructor_;
    std::vector<std::shared_ptr<IPointCloudOperator>> operators_;
};

} // namespace sf
