#include "MockCamera.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

#include "scanforge/Result.hpp"

MockCamera::MockCamera(QObject* parent) : QObject(parent) {
    initialiseParameters();
    rebuildScene();
}

void MockCamera::initialiseParameters() {
    params_.clear();
    params_.insert("ExposureTimeUs", 8000.0);
    params_.insert("GainDb",           0.0);
    params_.insert("FrameRateHz",     30.0);
    params_.insert("NoiseSigmaPx",     0.8);
    params_.insert("DropEveryN",         0);
    params_.insert("TimeOffsetMs",     0.0);
    params_.insert("PlaneBiasMm",      0.0);
    params_.insert("FrameCount",        12);
}

sf::DeviceInfo MockCamera::info() const {
    return sf::DeviceInfo{"mock-camera-01", "ScanForge",
                          "SyntheticLineLaser", "SIM-0001"};
}

sf::SourceCapabilities MockCamera::capabilities() const {
    sf::SourceCapabilities caps;
    caps.supportsContinuous = true;
    caps.supportsHardTrigger = false;   // timestamps are simulated, not locked
    caps.maxFrameRateHz = params_.value("FrameRateHz").toDouble();
    caps.maxWidth = scene_.config.width;
    caps.maxHeight = scene_.config.height;
    return caps;
}

sf::DeviceParamList MockCamera::parameters() const {
    sf::DeviceParamList list;
    auto add = [&list, this](const char* key, const char* display,
                             const char* unit, sf::ParamType type,
                             double min, double max) {
        sf::DeviceParam p;
        p.key = QString::fromUtf8(key);
        p.displayName = QString::fromUtf8(display);
        p.unit = QString::fromUtf8(unit);
        p.type = type;
        p.value = params_.value(QString::fromUtf8(key));
        p.minimum = min;
        p.maximum = max;
        list.push_back(p);
    };
    add("ExposureTimeUs", "Exposure", "us", sf::ParamType::Real, 100.0, 50000.0);
    add("GainDb",         "Gain",     "dB", sf::ParamType::Real,   0.0,    24.0);
    add("FrameRateHz",    "Frame rate", "Hz", sf::ParamType::Real,  1.0,   120.0);
    add("NoiseSigmaPx",   "Stripe noise", "px", sf::ParamType::Real, 0.0,    8.0);
    add("DropEveryN",     "Drop every N", "", sf::ParamType::Integer, 0.0,   20.0);
    add("TimeOffsetMs",   "Time offset", "ms", sf::ParamType::Real, -50.0,  50.0);
    add("PlaneBiasMm",    "Plane bias", "mm", sf::ParamType::Real, -10.0,  10.0);
    add("FrameCount",     "Frame count", "", sf::ParamType::Integer, 1.0,  200.0);
    return list;
}

sf::Status MockCamera::setParameter(const QString& key, const QVariant& value) {
    if (!params_.contains(key)) {
        return sf::Status::failure(sf::makeError(
            sf::ErrorCode::InvalidArgument, "unknown parameter: " + key.toStdString(),
            "MockCamera::setParameter"));
    }
    // GenICam-style node validation happens BEFORE touching the device state.
    bool converted = false;
    const double number = value.toDouble(&converted);
    if (!converted || !std::isfinite(number)) {
        return sf::Status::failure(sf::makeError(
            sf::ErrorCode::InvalidArgument, "parameter is not numeric",
            "MockCamera::setParameter"));
    }
    params_[key] = value;
    rebuildScene();   // synthetic device: parameter change regenerates the stream
    return sf::ok();
}

void MockCamera::rebuildScene() {
    scene_ = sf::makeDefaultScene();
    auto& config = scene_.config;
    config.fps          = params_.value("FrameRateHz").toDouble();
    config.noiseSigmaPx = params_.value("NoiseSigmaPx").toDouble();
    config.dropEvery    = static_cast<std::uint32_t>(params_.value("DropEveryN").toInt());
    config.frames       = static_cast<std::uint32_t>(params_.value("FrameCount").toInt());
    config.exposureUs   = params_.value("ExposureTimeUs").toDouble();
    config.gainDb       = params_.value("GainDb").toDouble();

    const double offsetMs = params_.value("TimeOffsetMs").toDouble();
    scene_ = sf::withTimeOffset(scene_,
        std::chrono::nanoseconds(static_cast<long long>(offsetMs * 1e6)));
    scene_ = sf::withPlaneBias(scene_, params_.value("PlaneBiasMm").toDouble());
    frames_ = sf::generateFrames(scene_);
    cursor_ = 0;
}

sf::Status MockCamera::open(const QString&) {
    rebuildScene();
    opened_ = true;
    return sf::ok();
}

void MockCamera::close() {
    opened_ = false;
    frames_.clear();
    cursor_ = 0;
}

sf::Result<sf::Frame> MockCamera::nextFrame(const sf::CancellationToken& token) {
    if (!opened_) {
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::DeviceDisconnected, "device is not open", "MockCamera"));
    }
    if (token.isCancelled()) {
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::Cancelled, "cancelled", "MockCamera"));
    }
    if (cursor_ >= frames_.size()) {
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::EndOfStream, "sequence finished", "MockCamera"));
    }
    // Simulate the acquisition period. A real plugin would block on the
    // camera's wait-for-buffer call here instead.
    const double period = 1.0 / std::max(1.0, scene_.config.fps);
    std::this_thread::sleep_for(std::chrono::duration<double>(period));
    return sf::Result<sf::Frame>::success(frames_[cursor_++]);
}
