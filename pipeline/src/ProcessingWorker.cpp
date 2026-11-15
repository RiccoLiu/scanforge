#include "scanforge/ProcessingWorker.hpp"

namespace sf {

void ProcessingWorker::configure(CalibrationSet calibration,
                                 std::shared_ptr<ReconstructionChain> chain,
                                 CancellationToken token) {
    calibration_ = std::move(calibration);
    chain_ = std::move(chain);
    token_ = token;
}

void ProcessingWorker::onFrame(sf::Frame frame) {
    if (!chain_) {
        emit failed(makeError(ErrorCode::InvalidState,
                              "processing chain is not configured",
                              "ProcessingWorker::onFrame"));
        return;
    }
    const auto begin = std::chrono::steady_clock::now();
    Result<PointCloudPtr> result = chain_->run(frame, calibration_, token_);
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();

    if (!result) {
        if (result.error().code != ErrorCode::Cancelled) emit failed(result.error());
        return;
    }
    emit cloudReady(result.value(), elapsedMs);
}

} // namespace sf
