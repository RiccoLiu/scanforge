#include "scanforge/Calibration.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace sf {
namespace {

Result<PointCloudPtr> fail(ErrorCode code, const char* message) {
    return Result<PointCloudPtr>::failure(
        makeError(code, message, "triangulateLaserLine"));
}

// Stage 1 detects the brightest pixel per column on an 8-bit luminance plane.
// Real cameras (e.g. the HikCamera plugin) also deliver Mono16 and RGB8, so
// derive that plane instead of rejecting the frame. The returned pointer is
// either into the frame itself (Mono8) or into `buffer`.
const std::uint8_t* luminancePlane(const Image& image,
                                   std::vector<std::uint8_t>& buffer) {
    const auto count = static_cast<std::size_t>(image.width) * image.height;
    if (image.format == PixelFormat::Mono8) return image.data.data();

    buffer.resize(count);
    const auto* src = image.data.data();
    if (image.format == PixelFormat::Mono16) {
        const auto* words = reinterpret_cast<const std::uint16_t*>(src);
        for (std::size_t i = 0; i < count; ++i) {
            buffer[i] = static_cast<std::uint8_t>(words[i] >> 8);
        }
    } else if (image.format == PixelFormat::RGB8) {
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned r = src[3 * i];
            const unsigned g = src[3 * i + 1];
            const unsigned b = src[3 * i + 2];
            buffer[i] = static_cast<std::uint8_t>((77 * r + 150 * g + 29 * b) >> 8);
        }
    }
    return buffer.data();
}

} // namespace

Result<PointCloudPtr> triangulateLaserLine(const Frame& frame,
                                           const CalibrationSet& calibration) {
    const Image& image = frame.image;
    if (!image.valid()) return fail(ErrorCode::InvalidArgument, "invalid image buffer");
    if (!calibration.valid())
        return fail(ErrorCode::CalibrationInvalid, "calibration set is not usable");

    std::vector<std::uint8_t> luminance;
    const std::uint8_t* pixels = luminancePlane(image, luminance);

    const CameraIntrinsics& K = calibration.camera;
    const Plane&            P = calibration.laserPlane;
    const RigidTransform&   T = calibration.cameraToScanner;

    auto cloud = std::make_shared<PointCloud>();
    cloud->unit       = calibration.unit;
    cloud->frameId    = calibration.coordinateFrame;
    cloud->sequence   = frame.sequence;
    cloud->timestamp  = frame.timestamp;
    cloud->points.reserve(image.width);

    constexpr std::uint8_t kDetectionThreshold = 24;

    for (std::uint32_t u = 0; u < image.width; ++u) {
        // Stage 1 peak detector: brightest pixel in the column.
        // Roadmap: sub-pixel Gaussian centre + distortion correction.
        std::uint32_t bestV = 0;
        std::uint8_t  best  = 0;
        for (std::uint32_t v = 0; v < image.height; ++v) {
            const std::uint8_t pixel = pixels[static_cast<std::size_t>(v) * image.width + u];
            if (pixel > best) { best = pixel; bestV = v; }
        }
        if (best < kDetectionThreshold) continue;   // no laser stripe here

        const double rx = (static_cast<double>(u)     - K.cx) / K.fx;
        const double ry = (static_cast<double>(bestV) - K.cy) / K.fy;

        // Ray r = (rx, ry, 1); solve a*t*rx + b*t*ry + c*t + d = 0.
        const double denominator = P.a * rx + P.b * ry + P.c;
        if (std::abs(denominator) < 1e-9) continue;
        const double t = -P.d / denominator;
        if (!(t > 0.0) || !std::isfinite(t)) continue;

        const double x = t * rx;
        const double y = t * ry;
        const double z = t;

        const auto& R = T.rotation;
        const auto& tr = T.translation;
        cloud->points.push_back(PointXYZI{
            static_cast<float>(R[0] * x + R[1] * y + R[2] * z + tr[0]),
            static_cast<float>(R[3] * x + R[4] * y + R[5] * z + tr[1]),
            static_cast<float>(R[6] * x + R[7] * y + R[8] * z + tr[2]),
            static_cast<float>(best) / 255.0f});
    }

    return Result<PointCloudPtr>::success(std::move(cloud));
}

Result<double> reprojectionRms(const std::vector<Correspondence>& samples,
                               const CameraIntrinsics& camera) {
    if (samples.empty()) {
        return Result<double>::failure(makeError(
            ErrorCode::InvalidArgument, "no correspondences", "reprojectionRms"));
    }
    if (!camera.valid()) {
        return Result<double>::failure(makeError(
            ErrorCode::CalibrationInvalid, "invalid intrinsics", "reprojectionRms"));
    }
    double sum = 0.0;
    for (const auto& s : samples) {
        if (std::abs(s.Z) < 1e-9) continue;
        const double du = camera.fx * (s.X / s.Z) + camera.cx - s.u;
        const double dv = camera.fy * (s.Y / s.Z) + camera.cy - s.v;
        sum += du * du + dv * dv;
    }
    return Result<double>::success(std::sqrt(sum / static_cast<double>(samples.size())));
}

} // namespace sf
