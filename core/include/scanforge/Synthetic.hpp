#pragma once

// Known-ground-truth data generator. This is the single most valuable learning
// device in the repository: real hardware never gives you the true answer,
// synthetic data always does. Inject bias, noise and time offset, then observe
// how the error report changes -> that is "layered error attribution".
// Reference: Kalibr's simulated calibration datasets, EuRoC MAV ground truth
// (ETH Zurich), 3DUNDERWORLD-SLS example_data (LGPL-3.0).

#include "Types.hpp"
#include "Calibration.hpp"
#include "Contracts.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

struct SynthConfig {
    std::uint32_t width  = 640;
    std::uint32_t height = 480;
    std::uint32_t frames = 12;
    double        fps    = 30.0;

    // Ground-truth plane Z = z0 + alpha*X + beta*Y (millimetres, camera frame).
    double z0    = 300.0;
    double alpha = 0.05;
    double beta  = -0.03;

    // Injectable defects.
    double          noiseSigmaPx = 0.8;
    std::uint32_t   dropEvery    = 0;      // 0 disables frame dropping
    Nanos           timeOffset{0};         // camera<->platform time offset
    double          exposureUs   = 8000.0;
    double          gainDb       = 0.0;
    std::uint64_t   seed         = 20261115;
};

struct SynthScene {
    SynthConfig    config;
    CalibrationSet truth;        // the calibration that generated the data
    Plane          truthPlane;   // same plane in a*X+b*Y+c*Z+d=0 form
    double         yStartMm = -30.0;
    double         yEndMm   =  30.0;
};

[[nodiscard]] SynthScene makeDefaultScene();

// Build a scene, then perturb its calibration on purpose:
//   withPlaneBias()  -> simulates a bad/stale calibration
//   withTimeOffset() -> simulates a trigger/synchronisation fault
[[nodiscard]] SynthScene withPlaneBias(const SynthScene& scene, double deltaMm);
[[nodiscard]] SynthScene withTimeOffset(const SynthScene& scene, Nanos offset);

// Frames whose bright laser line is computed by back-projecting the TRUE plane.
[[nodiscard]] std::vector<Frame> generateFrames(const SynthScene& scene,
                                                const CancellationToken& token = {});

// Noise-free points sampled on the true plane, for cloud-to-cloud RMSE.
// samplesPerFrame = 0 means one ground-truth point per image column, so each
// reconstructed point has an exact counterpart (recommended for stage 1).
[[nodiscard]] PointCloudPtr generateGroundTruthCloud(const SynthScene& scene,
                                                     std::size_t samplesPerFrame = 0);

// A pure C++ IDataSource over synthetic frames -> CLI and tests are identical
// to the GUI data path.
class SyntheticSource final : public IDataSource {
public:
    explicit SyntheticSource(SynthScene scene);
    [[nodiscard]] DeviceInfo          info() const override;
    [[nodiscard]] SourceCapabilities  capabilities() const override;
    [[nodiscard]] Status              open(const std::string& uri) override;
    void                              close() override;
    [[nodiscard]] Result<Frame>       nextFrame(const CancellationToken& token) override;

private:
    SynthScene scene_;
    std::vector<Frame> frames_;
    std::size_t cursor_ = 0;
    bool opened_ = false;
};

} // namespace sf
