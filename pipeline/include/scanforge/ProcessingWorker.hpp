#pragma once

// Processing lane. Frame -> ReconstructionChain -> PointCloudPtr.
// The chain itself is pure C++ (core/), so this QObject is only a threading
// adapter: no business logic belongs here.
// Reference: PDAL pdal/Stage.hpp chaining (BSD), 3DUNDERWORLD-SLS
// ImageProcessor -> Buckets -> Reconstructor layering (LGPL-3.0).

#include <QObject>
#include <chrono>
#include <memory>

#include "scanforge/Calibration.hpp"
#include "scanforge/Contracts.hpp"
#include "scanforge/Filters.hpp"
#include "scanforge/Result.hpp"
#include "scanforge/Types.hpp"

namespace sf {

class ProcessingWorker : public QObject {
    Q_OBJECT
public:
    explicit ProcessingWorker(QObject* parent = nullptr) : QObject(parent) {}

    // Call ONCE, before start(). Afterwards the chain is read-only.
    void configure(CalibrationSet calibration,
                   std::shared_ptr<ReconstructionChain> chain,
                   CancellationToken token);

public slots:
    void onFrame(sf::Frame frame);

signals:
    void cloudReady(sf::PointCloudPtr cloud, double elapsedMs);
    void failed(sf::Error error);

private:
    CalibrationSet calibration_;
    std::shared_ptr<ReconstructionChain> chain_;
    CancellationToken token_;
};

} // namespace sf
