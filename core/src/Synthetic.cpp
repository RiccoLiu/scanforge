#include "scanforge/Synthetic.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <utility>

namespace sf {
namespace {

// Back-project a point that lies on `plane` at Y = yLineMm and image column u.
// Plane: a*X + b*Y + c*Z + d = 0, ray: (t*rx, t*ry, t).
// With Y fixed:  a*t*rx + b*Y + c*t + d = 0  =>  t = -(b*Y+d)/(a*rx+c).
// Then ry = Y/t and v = cy + fy*ry. This is a CLOSED FORM, so the generated
// pixels are exactly consistent with the ground-truth plane.
struct LinePoint { double t = 0.0, v = 0.0; bool valid = false; };

LinePoint projectPlaneAtColumn(const Plane& plane, const CameraIntrinsics& K,
                               double yLineMm, std::uint32_t u) {
    const double rx = (static_cast<double>(u) - K.cx) / K.fx;
    const double denominator = plane.a * rx + plane.c;
    if (std::abs(denominator) < 1e-9) return {};
    const double t = -(plane.b * yLineMm + plane.d) / denominator;
    if (!(t > 0.0) || !std::isfinite(t)) return {};
    const double ry = yLineMm / t;
    return LinePoint{t, K.cy + K.fy * ry, true};
}

double yLineAt(const SynthScene& scene, std::size_t frameIndex) {
    const std::size_t n = std::max<std::size_t>(scene.config.frames, 2);
    const double f = static_cast<double>(frameIndex) / static_cast<double>(n - 1);
    return scene.yStartMm + (scene.yEndMm - scene.yStartMm) * f;
}

void drawStripe(Image& image, std::uint32_t u, double vCenter,
                std::mt19937_64& rng,
                std::optional<std::normal_distribution<double>>& noise) {
    // An empty `noise` means "noise disabled". Never construct a
    // std::normal_distribution with stddev == 0: that is UB and MSVC's Debug
    // CRT aborts at <random>:3489 (_STL_ASSERT(0.0 < _Sigma0)), which looks
    // like a hang when the debugger is attached.
    const double jitter = noise ? (*noise)(rng) : 0.0;
    const double vSample = vCenter + jitter;
    if (!std::isfinite(vSample) ||
        vSample < static_cast<double>(std::numeric_limits<int>::min()) ||
        vSample > static_cast<double>(std::numeric_limits<int>::max())) {
        return;   // guard lround() against out-of-range / NaN casts
    }
    const int centre = static_cast<int>(std::lround(vSample));
    for (int dv = -1; dv <= 1; ++dv) {
        const int v = centre + dv;
        if (v < 0 || v >= static_cast<int>(image.height)) continue;
        const std::uint8_t value = dv == 0 ? 255 : (dv < 0 ? 170 : 120);
        image.data[static_cast<std::size_t>(v) * image.width + u] = value;
    }
}

} // namespace

SynthScene makeDefaultScene() {
    SynthScene scene;
    scene.config = SynthConfig{};

    CameraIntrinsics K;
    K.fx = 800.0; K.fy = 800.0; K.cx = 320.0; K.cy = 240.0;
    scene.truth.camera = K;
    scene.truth.laserPlane = Plane{scene.config.alpha, scene.config.beta,
                                   -1.0, scene.config.z0};
    scene.truth.unit = LengthUnit::Millimeter;
    scene.truth.coordinateFrame = "camera";
    scene.truth.sourceFile = "<synthetic>";
    scene.truth.reprojectionErrorRmsPx = 0.0;
    scene.truthPlane = scene.truth.laserPlane;
    return scene;
}

SynthScene withPlaneBias(const SynthScene& scene, double deltaMm) {
    SynthScene copy = scene;
    // Only the CALIBRATION used by reconstruction is perturbed. truthPlane
    // stays intact, so evaluateAgainstPlane() can expose the induced error.
    copy.truth.laserPlane.d += deltaMm;
    return copy;
}

SynthScene withTimeOffset(const SynthScene& scene, Nanos offset) {
    SynthScene copy = scene;
    copy.config.timeOffset = offset;
    copy.truth.cameraToImuOffset = offset;
    return copy;
}

std::vector<Frame> generateFrames(const SynthScene& scene,
                                  const CancellationToken& token) {
    std::vector<Frame> frames;
    frames.reserve(scene.config.frames);

    std::mt19937_64 rng(scene.config.seed);

    // [rand.dist.norm.normal] requires stddev > 0. Clamping a zero/negative
    // sigma to 0.0 used to "work" in Release (the assertion is compiled out)
    // but broke Debug builds. sigma <= 0 now means noise is simply disabled.
    const double sigma = scene.config.noiseSigmaPx;
    std::optional<std::normal_distribution<double>> noise;
    if (sigma > 0.0 && std::isfinite(sigma)) {
        noise.emplace(0.0, sigma);
    }
    const auto periodNs = static_cast<long long>(1e9 / std::max(1.0, scene.config.fps));

    for (std::uint32_t i = 0; i < scene.config.frames; ++i) {
        if (token.isCancelled()) break;
        if (scene.config.dropEvery > 0 &&
            (i + 1) % scene.config.dropEvery == 0) {
            continue;   // injected dropped frame
        }

        Frame frame;
        frame.sequence = i;
        frame.timestamp = Nanos(static_cast<long long>(i) * periodNs) +
                          scene.config.timeOffset;
        frame.timeBase = "steady_clock";
        frame.deviceId = "sf::SyntheticSource";
        frame.exposureUs = static_cast<float>(scene.config.exposureUs);
        frame.gainDb = static_cast<float>(scene.config.gainDb);
        frame.injectedTimeOffset = scene.config.timeOffset;
        frame.isSynthetic = true;

        Image image;
        image.width = scene.config.width;
        image.height = scene.config.height;
        image.format = PixelFormat::Mono8;
        image.data.assign(static_cast<std::size_t>(image.width) * image.height, 0);
        frame.image = image;

        const double yLine = yLineAt(scene, i);
        for (std::uint32_t u = 0; u < image.width; ++u) {
            const LinePoint p = projectPlaneAtColumn(
                scene.truthPlane, scene.truth.camera, yLine, u);
            if (!p.valid) continue;
            drawStripe(frame.image, u, p.v, rng, noise);
        }
        frames.push_back(std::move(frame));
    }
    return frames;
}

PointCloudPtr generateGroundTruthCloud(const SynthScene& scene,
                                       std::size_t samplesPerFrame) {
    auto cloud = std::make_shared<PointCloud>();
    cloud->unit = scene.truth.unit;
    cloud->frameId = scene.truth.coordinateFrame;
    const std::size_t step = samplesPerFrame == 0
        ? 1u
        : std::max<std::size_t>(1, scene.config.width / samplesPerFrame);
    for (std::uint32_t i = 0; i < scene.config.frames; ++i) {
        const double yLine = yLineAt(scene, i);
        for (std::uint32_t u = 0; u < scene.config.width; u += static_cast<std::uint32_t>(step)) {
            const double rx = (static_cast<double>(u) - scene.truth.camera.cx) /
                              scene.truth.camera.fx;
            const LinePoint p = projectPlaneAtColumn(
                scene.truthPlane, scene.truth.camera, yLine, u);
            if (!p.valid) continue;

            // Model PIXEL QUANTISATION explicitly: ground truth is derived from
            // the rounded pixel coordinate via the SAME ray-plane intersection
            // as triangulateLaserLine(). A noise-free reconstruction then has
            // RMSE ~ 0, and any remaining error is a real injected defect.
            const double vQuantised = std::round(p.v);
            const double ry = (vQuantised - scene.truth.camera.cy) /
                              scene.truth.camera.fy;
            const double denominator = scene.truthPlane.a * rx +
                                       scene.truthPlane.b * ry +
                                       scene.truthPlane.c;
            if (std::abs(denominator) < 1e-9) continue;
            const double t = -scene.truthPlane.d / denominator;
            if (!(t > 0.0) || !std::isfinite(t)) continue;
            cloud->points.push_back(PointXYZI{
                static_cast<float>(t * rx),
                static_cast<float>(t * ry),
                static_cast<float>(t),
                1.0f});
        }
    }
    return cloud;
}

// ---------------------------------------------------------------------------
// SyntheticSource
// ---------------------------------------------------------------------------
SyntheticSource::SyntheticSource(SynthScene scene) : scene_(std::move(scene)) {}

DeviceInfo SyntheticSource::info() const {
    return DeviceInfo{"synthetic-01", "ScanForge", "SyntheticLineLaser", "SIM-0001"};
}

SourceCapabilities SyntheticSource::capabilities() const {
    SourceCapabilities caps;
    caps.supportsContinuous = true;
    caps.supportsHardTrigger = false;   // the whole point: timestamps are simulated
    caps.maxFrameRateHz = scene_.config.fps;
    caps.maxWidth = scene_.config.width;
    caps.maxHeight = scene_.config.height;
    return caps;
}

Status SyntheticSource::open(const std::string&) {
    frames_ = generateFrames(scene_);
    cursor_ = 0;
    opened_ = true;
    return ok();
}

void SyntheticSource::close() {
    opened_ = false;
    frames_.clear();
    cursor_ = 0;
}

Result<Frame> SyntheticSource::nextFrame(const CancellationToken& token) {
    if (!opened_) {
        return Result<Frame>::failure(makeError(
            ErrorCode::DeviceDisconnected, "source is not open", "SyntheticSource"));
    }
    if (token.isCancelled()) {
        return Result<Frame>::failure(makeError(
            ErrorCode::Cancelled, "cancelled", "SyntheticSource"));
    }
    if (cursor_ >= frames_.size()) {
        return Result<Frame>::failure(makeError(
            ErrorCode::EndOfStream, "synthetic sequence finished", "SyntheticSource"));
    }
    return Result<Frame>::success(frames_[cursor_++]);
}

} // namespace sf
