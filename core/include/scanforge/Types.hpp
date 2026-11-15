#pragma once

// Core value types. Qt-free by contract: no QObject, QString, QVector, ...
// Reference for this file: Open3D open3d/geometry/PointCloud.h (MIT),
// PCL pcl/PCLPointCloud2.h (BSD-3), PDAL pdal/PointView.hpp (BSD).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <chrono>

namespace sf {

using TimePoint = std::chrono::steady_clock::time_point;
using Nanos     = std::chrono::nanoseconds;

enum class LengthUnit : std::uint8_t { Millimeter = 0, Meter = 1 };

inline double toMeters(double value, LengthUnit unit) noexcept {
    return unit == LengthUnit::Meter ? value : value * 1e-3;
}

struct Point3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct PointXYZI {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float intensity = 0.0f;
};

struct PointCloud {
    std::vector<PointXYZI> points;
    LengthUnit  unit       = LengthUnit::Millimeter;
    std::string frameId    = "scanner";   // ALWAYS display this in the UI
    std::uint64_t sequence = 0;           // source frame that produced it
    Nanos     timestamp{0};               // time base documented in Frame

    [[nodiscard]] std::size_t size()  const noexcept { return points.size(); }
    [[nodiscard]] bool        empty() const noexcept { return points.empty(); }
};

// Zero-copy hand-off between worker threads: producers create, consumers only
// read. Never mutate a PointCloud after publishing a const shared_ptr.
using PointCloudPtr = std::shared_ptr<const PointCloud>;

enum class PixelFormat : std::uint8_t { Mono8 = 0, Mono16 = 1, RGB8 = 2 };

[[nodiscard]] constexpr std::size_t bytesPerPixel(PixelFormat f) noexcept {
    return f == PixelFormat::Mono8 ? 1u : (f == PixelFormat::Mono16 ? 2u : 3u);
}

struct Image {
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
    PixelFormat   format = PixelFormat::Mono8;
    std::vector<std::uint8_t> data;   // tightly packed, row-major

    [[nodiscard]] bool valid() const noexcept {
        return width > 0 && height > 0 &&
               data.size() == static_cast<std::size_t>(width) * height * bytesPerPixel(format);
    }
};

// Cross-thread data contract. A Frame must carry enough information to explain
// WHY two scans do not line up: sequence, timestamp, AND the time base.
struct Frame {
    std::uint64_t sequence = 0;
    Nanos         timestamp{0};
    std::string   timeBase = "steady_clock";  // "steady_clock" | "ptp" | "device_ticks"
    std::string   deviceId;
    Image         image;
    float         exposureUs = 0.0f;
    float         gainDb     = 0.0f;

    // Injected by the mock source for diagnostics; real devices leave them 0.
    Nanos         injectedTimeOffset{0};
    bool          isSynthetic = false;
};

struct DeviceInfo {
    std::string id;
    std::string vendor;
    std::string model;
    std::string serial;
};

} // namespace sf
