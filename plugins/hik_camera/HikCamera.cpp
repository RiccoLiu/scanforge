// Hikvision MVS (GenICam) camera plugin - implementation.
//
// Every SDK operation goes through CMvCamera (include/MvCamera.h), the C++
// wrapper shipped with the MVS BasicDemo sample. No MV_CC_* calls are made
// directly in this file. Mapping from the demo:
//
//   BasicDemo                          CMvCamera call
//   ----------------------------------------------------------------
//   OnBnClickedEnumButton          -> CMvCamera::EnumDevices + URI selection
//   OnBnClickedOpenButton          -> Open() + GigE optimal packet size
//   OnBnClickedStartGrabbingButton -> lazy StartGrabbing() on first frame
//   GrabThreadProcess()            -> GetImageBuffer() + ConvertPixelType()
//                                     + FreeImageBuffer() (pull instead of
//                                     the demo's MFC grab thread + display)
//   Get/SetParameterButton         -> Get*/Set*Value node access
//   OnBnClickedSoftwareOnceButton  -> CommandExecute("TriggerSoftware")

#include "HikCamera.h"

#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "scanforge/Result.hpp"

namespace {

// Transport layers supported by the bundled (older) MvCamera.h wrapper.
// Newer SDK headers may add more GenTL layer constants - pick them up when
// available, but do not require them (build works with either SDK version).
unsigned int supportedLayerTypes() {
    unsigned int layers = MV_GIGE_DEVICE | MV_USB_DEVICE | MV_GENTL_GIGE_DEVICE;
#ifdef MV_GENTL_CAMERALINK_DEVICE
    layers |= MV_GENTL_CAMERALINK_DEVICE;
#endif
#ifdef MV_GENTL_CXP_DEVICE
    layers |= MV_GENTL_CXP_DEVICE;
#endif
#ifdef MV_GENTL_XOF_DEVICE
    layers |= MV_GENTL_XOF_DEVICE;
#endif
#ifdef MV_GENTL_XOC_DEVICE
    layers |= MV_GENTL_XOC_DEVICE;
#endif
    return layers;
}

constexpr int          kGrabTimeoutMs   = 1000;   // BasicDemo uses the same
constexpr unsigned int kBufferNodeCount = 8;

// MV_CC device info fields are unsigned char buffers; convert safely to
// std::string (truncating at the buffer's NUL terminator).
std::string toStr(const unsigned char* text) {
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string();
}

// Wrappers around CMvCamera's GenICam node getters. All calls are guarded by
// HikCamera::mutex_, so no extra locking is needed here.

MVCC_INTVALUE_EX getIntValue(CMvCamera& camera, const char* key,
                             MVCC_INTVALUE_EX fallback = {}) {
    MVCC_INTVALUE_EX value = fallback;
    if (camera.GetIntValue(key, &value) != MV_OK) return fallback;
    return value;
}

MVCC_FLOATVALUE getFloatValue(CMvCamera& camera, const char* key,
                              MVCC_FLOATVALUE fallback = {}) {
    MVCC_FLOATVALUE value = fallback;
    if (camera.GetFloatValue(key, &value) != MV_OK) return fallback;
    return value;
}

// Symbolic name of the current enum value (BasicDemoDlg::GetPixelFormat shows
// the intended pattern: GetEnumValue + GetEnumEntrySymbolic).
std::string getEnumSymbolic(CMvCamera& camera, const char* key) {
    MVCC_ENUMVALUE enumValue{};
    if (camera.GetEnumValue(key, &enumValue) != MV_OK) return {};
    MVCC_ENUMENTRY entry{};
    entry.nValue = enumValue.nCurValue;
    if (camera.GetEnumEntrySymbolic(key, &entry) != MV_OK) return {};
    return entry.chSymbolic;
}

// Full option list of an IEnumeration node, straight from the device:
// GetEnumValue returns the supported integer values (nSupportValue /
// nSupportedNum) and GetEnumEntrySymbolic resolves each one to its symbolic
// name. Nothing here is hard-coded - different cameras report different sets.
QStringList getEnumOptions(CMvCamera& camera, const char* key) {
    QStringList options;
    MVCC_ENUMVALUE enumValue{};
    if (camera.GetEnumValue(key, &enumValue) != MV_OK) return options;
    const auto count = static_cast<unsigned int>(
        std::min<unsigned long>(enumValue.nSupportedNum, MV_MAX_XML_SYMBOLIC_NUM));
    for (unsigned int i = 0; i < count; ++i) {
        MVCC_ENUMENTRY entry{};
        entry.nValue = enumValue.nSupportValue[i];
        if (camera.GetEnumEntrySymbolic(key, &entry) == MV_OK) {
            options << QString::fromLatin1(entry.chSymbolic);
        }
    }
    return options;
}

std::size_t dstBytesPerSample(MvGvspPixelType type) {
    return type == PixelType_Gvsp_Mono16 ? 2u : 3u;   // Mono16 or RGB8 packed
}

// CMvCamera::ConvertPixelType wrapper: converts src into a tightly-packed dst.
bool convertPixels(CMvCamera& camera, std::uint32_t width, std::uint32_t height,
                   MvGvspPixelType srcType, const unsigned char* srcData,
                   unsigned int srcLen, MvGvspPixelType dstType,
                   std::vector<std::uint8_t>& dst) {
    dst.resize(static_cast<std::size_t>(width) * height * dstBytesPerSample(dstType));
    MV_CC_PIXEL_CONVERT_PARAM_EX cvt{};
    cvt.nWidth         = width;
    cvt.nHeight        = height;
    cvt.enSrcPixelType = srcType;
    cvt.pSrcData       = const_cast<unsigned char*>(srcData);
    cvt.nSrcDataLen    = srcLen;
    cvt.enDstPixelType = dstType;
    cvt.pDstBuffer     = dst.data();
    cvt.nDstBufferSize = static_cast<unsigned int>(dst.size());
    return camera.ConvertPixelType(&cvt) == MV_OK;
}

} // namespace

// ---------------------------------------------------------------- lifecycle

HikCamera::HikCamera(QObject* parent) : QObject(parent) {
    info_ = sf::DeviceInfo{"hik-camera", "Hikvision", "GenICam device", ""};
}

HikCamera::~HikCamera() {
    const std::lock_guard lock(mutex_);
    teardownLocked();
}

void HikCamera::teardownLocked() {
    if (camera_) {
        if (grabbing_) camera_->StopGrabbing();
        camera_->Close();       // CMvCamera::Close closes + destroys the handle
        camera_.reset();        // CMvCamera destructor releases any residual handle
    }
    grabbing_ = false;
    opened_   = false;
}

// ------------------------------------------------------------- introspection

sf::DeviceInfo HikCamera::info() const {
    const std::lock_guard lock(mutex_);
    return info_;
}

sf::SourceCapabilities HikCamera::capabilities() const {
    sf::SourceCapabilities caps;
    caps.supportsContinuous  = true;    // TriggerMode = Off
    caps.supportsHardTrigger = true;    // TriggerSource = Line0..3

    const std::lock_guard lock(mutex_);
    if (opened_ && camera_) {
        caps.maxWidth = static_cast<std::uint32_t>(
            getIntValue(*camera_, "WidthMax").nCurValue);
        caps.maxHeight = static_cast<std::uint32_t>(
            getIntValue(*camera_, "HeightMax").nCurValue);
        caps.maxFrameRateHz =
            static_cast<double>(getFloatValue(*camera_, "ResultingFrameRate").fCurValue);
    }
    return caps;
}

sf::DeviceParamList HikCamera::parameters() const {
    sf::DeviceParamList list;

    const std::lock_guard lock(mutex_);
    const bool live = opened_ && camera_;

    // Every value/range/option below comes from the device itself via the
    // universal GenICam node accessors (MV_CC_GetFloatValue / GetIntValueEx /
    // GetBoolValue / GetEnumValue + GetEnumEntrySymbolic). When the device is
    // closed the parameters simply cannot be listed - no fabricated defaults.
    if (!live) return list;

    // Float nodes: value + [min, max] straight from MVCC_FLOATVALUE.
    auto floatNode = [&](const char* key, const char* display, const char* unit) {
        MVCC_FLOATVALUE value{};
        if (camera_->GetFloatValue(key, &value) != MV_OK) return;
        sf::DeviceParam p;
        p.key         = key;
        p.displayName = display;
        p.unit        = unit;
        p.type        = sf::ParamType::Real;
        p.value   = static_cast<double>(value.fCurValue);
        p.minimum = static_cast<double>(value.fMin);
        p.maximum = static_cast<double>(value.fMax);
        list.push_back(std::move(p));
    };

    // Integer nodes: value + [min, max] from MVCC_INTVALUE_EX.
    auto intNode = [&](const char* key, const char* display, const char* unit) {
        MVCC_INTVALUE_EX value{};
        if (camera_->GetIntValue(key, &value) != MV_OK) return;
        sf::DeviceParam p;
        p.key         = key;
        p.displayName = display;
        p.unit        = unit;
        p.type        = sf::ParamType::Integer;
        p.value   = static_cast<double>(value.nCurValue);
        p.minimum = static_cast<double>(value.nMin);
        p.maximum = static_cast<double>(value.nMax);
        list.push_back(std::move(p));
    };

    // Boolean nodes.
    auto boolNode = [&](const char* key, const char* display) {
        bool enabled = false;
        if (camera_->GetBoolValue(key, &enabled) != MV_OK) return;
        sf::DeviceParam p;
        p.key         = key;
        p.displayName = display;
        p.type        = sf::ParamType::Boolean;
        p.value       = enabled;
        list.push_back(std::move(p));
    };

    // Enumeration nodes: current symbolic + the full supported option list.
    auto enumNode = [&](const char* key, const char* display) {
        const QStringList options = getEnumOptions(*camera_, key);
        const std::string symbolic = getEnumSymbolic(*camera_, key);
        if (options.isEmpty() && symbolic.empty()) return;
        sf::DeviceParam p;
        p.key         = key;
        p.displayName = display;
        p.type        = sf::ParamType::Enum;
        p.options     = options;
        p.value       = QString::fromStdString(symbolic);
        list.push_back(std::move(p));
    };

    // --- Image Format controls ------------------------------------------
    intNode("Width",  "Width",  "px");
    intNode("Height", "Height", "px");
    intNode("OffsetX", "Offset X", "px");   // skipped when not implemented
    intNode("OffsetY", "Offset Y", "px");
    enumNode("PixelFormat", "Pixel format");

    // --- Exposure / gain / white balance ---------------------------------
    enumNode("ExposureAuto", "Exposure auto");
    floatNode("ExposureTime", "Exposure", "us");
    enumNode("GainAuto", "Gain auto");
    floatNode("Gain", "Gain", "dB");

    // --- Frame rate -------------------------------------------------------
    boolNode("AcquisitionFrameRateEnable", "Frame rate control");
    floatNode("AcquisitionFrameRate", "Frame rate", "Hz");

    // --- Trigger ----------------------------------------------------------
    enumNode("TriggerMode", "Trigger mode");
    enumNode("TriggerSource", "Trigger source");
    enumNode("TriggerActivation", "Trigger activation");
    {
        sf::DeviceParam p;
        p.key         = "TriggerSoftware";
        p.displayName = "Software trigger once";
        p.description = "Execute the TriggerSoftware command (trigger mode + software source only)";
        p.type        = sf::ParamType::Command;
        list.push_back(std::move(p));
    }

    // --- Analog / image quality (present only if the camera has them) -----
    floatNode("BlackLevel", "Black level", "");
    floatNode("Gamma", "Gamma", "");

    // --- Device identity (read-only) --------------------------------------
    {
        MVCC_STRINGVALUE str{};
        if (camera_->GetStringValue("DeviceUserID", &str) == MV_OK) {
            sf::DeviceParam p;
            p.key         = "DeviceUserID";
            p.displayName = "Device user ID";
            p.type        = sf::ParamType::Text;
            p.value       = QString::fromLatin1(str.chCurValue);
            list.push_back(std::move(p));
        }
    }
    return list;
}

// ---------------------------------------------------------------- parameters

sf::Status HikCamera::setParameter(const QString& key, const QVariant& value) {
    const std::lock_guard lock(mutex_);
    if (!opened_ || !camera_) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::DeviceDisconnected,
                                                 "device is not open",
                                                 "HikCamera::setParameter"));
    }

    const std::string node = key.toStdString();
    bool unsupportedType = false;   // set by applyWrite() for unknown value types

    // The actual node write. Captured in a lambda so it can be retried once
    // after the acquisition has been stopped (see below).
    auto applyWrite = [&]() -> int {
        unsupportedType = false;

        // Commands: MV_CC_SetCommandValue via CMvCamera::CommandExecute.
        // BasicDemo::OnBnClickedSoftwareOnceButton -> CommandExecute("TriggerSoftware").
        if (key == "TriggerSoftware") {
            return camera_->CommandExecute(node.c_str());
        }

        // Enum nodes are written by symbolic name (SetEnumValueByString); the UI
        // sends exactly the option strings the device reported in parameters().
        if (value.typeId() == QMetaType::QString &&
            getEnumOptions(*camera_, node.c_str()).contains(value.toString())) {
            const std::string symbolic = value.toString().toStdString();
            return camera_->SetEnumValueByString(node.c_str(), symbolic.c_str());
        }

        switch (value.typeId()) {
        case QMetaType::Bool:
            return camera_->SetBoolValue(node.c_str(), value.toBool());
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
            return camera_->SetIntValue(node.c_str(),
                                        static_cast<std::int64_t>(value.toLongLong()));
        case QMetaType::Double:
            return camera_->SetFloatValue(node.c_str(),
                                          static_cast<float>(value.toDouble()));
        default:
            break;
        }

        // Strings (e.g. DeviceUserID) go through the string node accessor.
        if (value.canConvert<QString>()) {
            const QByteArray text = value.toString().toUtf8();
            return camera_->SetStringValue(node.c_str(), text.constData());
        }

        unsupportedType = true;
        return MV_OK;   // value ignored - failure reported via unsupportedType
    };

    int rc = applyWrite();

    // GenICam locks many nodes (Width/Height/OffsetX/OffsetY, PixelFormat, ...)
    // while the camera is acquiring. UI Stop only cancels the pipeline loop -
    // grabbing_ stays true and the device keeps streaming, so a resolution
    // change made after Stop would fail here and the next Start would
    // silently reuse the old resolution. Stop grabbing and retry once;
    // nextFrame() lazy-starts grabbing again with the new settings.
    if (rc != MV_OK && grabbing_) {
        camera_->StopGrabbing();
        grabbing_ = false;
        rc = applyWrite();
    }

    if (rc == MV_OK && !unsupportedType) return sf::ok();

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", rc);
    const std::string context = "HikCamera::setParameter(" + node + ")";
    if (unsupportedType) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::InvalidArgument,
                                                 "unsupported value type for parameter: " + node,
                                                 context));
    }
    return sf::Status::failure(sf::makeError(sf::ErrorCode::ProcessingFailed,
                                             "MVS SDK error 0x" + std::string(buf),
                                             context));
}

// ---------------------------------------------------------------------- open

sf::Status HikCamera::open(const QString& uri) {
    const std::lock_guard lock(mutex_);
    if (opened_ && camera_) return sf::ok();   // DeviceManager::acquire() idempotent

    MV_CC_DEVICE_INFO_LIST deviceList{};
    if (const int rc = CMvCamera::EnumDevices(supportedLayerTypes(), &deviceList);
        rc != MV_OK) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::IoError,
                                                 "CMvCamera::EnumDevices failed (0x" +
                                                     std::to_string(rc) + ")",
                                                 "HikCamera::open"));
    }
    if (deviceList.nDeviceNum == 0) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::DeviceNotFound,
                                                 "no Hikvision GenICam device found",
                                                 "HikCamera::open"));
    }

    // URI selection: "" -> first device, <n> -> index, otherwise match
    // serial number first, then GigE IP. For a "<pluginId>://<selector>"
    // uri (e.g. "hik-camera://2" as built by DeviceManager) everything
    // before "://" is just the plugin id and is ignored here.
    unsigned int index = 0;
    std::string  token = uri.toStdString();
    if (const auto pos = token.find("://"); pos != std::string::npos) {
        token = token.substr(pos + 3);
    }
    if (!token.empty()) {
        bool matched = false;
        if (std::all_of(token.begin(), token.end(), ::isdigit)) {
            const unsigned long requested = std::stoul(token);
            if (requested < deviceList.nDeviceNum) {
                index   = static_cast<unsigned int>(requested);
                matched = true;
            }
        }
        if (!matched) {
            for (unsigned int i = 0; i < deviceList.nDeviceNum && !matched; ++i) {
                const auto* info = static_cast<const MV_CC_DEVICE_INFO*>(
                    deviceList.pDeviceInfo[i]);
                std::string serial;
                std::string ip;
                if (info->nTLayerType == MV_GIGE_DEVICE) {
                    serial = toStr(info->SpecialInfo.stGigEInfo.chSerialNumber);
                    const std::uint32_t addr = info->SpecialInfo.stGigEInfo.nCurrentIp;
                    ip = std::to_string((addr >> 24) & 0xff) + "." +
                         std::to_string((addr >> 16) & 0xff) + "." +
                         std::to_string((addr >> 8) & 0xff) + "." +
                         std::to_string(addr & 0xff);
                } else if (info->nTLayerType == MV_USB_DEVICE) {
                    serial = toStr(info->SpecialInfo.stUsb3VInfo.chSerialNumber);
                }
                matched = (serial == token) || (ip == token);
                if (matched) index = i;
            }
        }
        if (!matched) {
            return sf::Status::failure(sf::makeError(sf::ErrorCode::DeviceNotFound,
                                                     "no device matches uri: " + token,
                                                     "HikCamera::open"));
        }
    }

    auto* deviceInfo = static_cast<MV_CC_DEVICE_INFO*>(deviceList.pDeviceInfo[index]);
    if (!deviceInfo) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::DeviceNotFound,
                                                 "selected device does not exist",
                                                 "HikCamera::open"));
    }

    // CMvCamera::Open wraps CreateHandle + OpenDevice (BasicDemo::CMvCamera::Open).
    auto camera = std::make_unique<CMvCamera>();
    if (const int rc = camera->Open(deviceInfo); rc != MV_OK) {
        return sf::Status::failure(sf::makeError(sf::ErrorCode::ProcessingFailed,
                                                 "CMvCamera::Open failed (0x" +
                                                     std::to_string(rc) + ")",
                                                 "HikCamera::open"));
    }
    camera_ = std::move(camera);

    // GigE: optimal packet size (BasicDemo::OnBnClickedOpenButton). Failure is
    // a warning only - the camera still streams with the default MTU.
    if (deviceInfo->nTLayerType == MV_GIGE_DEVICE) {
        unsigned int packetSize = 0;
        if (camera_->GetOptimalPacketSize(&packetSize) == MV_OK && packetSize > 0) {
            camera_->SetIntValue("GevSCPSPacketSize", packetSize);
        }
    }
    camera_->SetImageNodeNum(kBufferNodeCount);   // best effort

    opened_ = true;

    // Refresh identity so DeviceManager shows the real hardware.
    std::string model = "GenICam device";
    std::string serial;
    if (deviceInfo->nTLayerType == MV_GIGE_DEVICE) {
        model  = toStr(deviceInfo->SpecialInfo.stGigEInfo.chModelName);
        serial = toStr(deviceInfo->SpecialInfo.stGigEInfo.chSerialNumber);
    } else if (deviceInfo->nTLayerType == MV_USB_DEVICE) {
        model  = toStr(deviceInfo->SpecialInfo.stUsb3VInfo.chModelName);
        serial = toStr(deviceInfo->SpecialInfo.stUsb3VInfo.chSerialNumber);
    }
    info_ = sf::DeviceInfo{"hik-camera", "Hikvision", model, serial};
    return sf::ok();
}

void HikCamera::close() {
    const std::lock_guard lock(mutex_);
    teardownLocked();
}

// ----------------------------------------------------------------- grabbing

sf::Result<sf::Frame> HikCamera::nextFrame(const sf::CancellationToken& token) {
    if (token.isCancelled()) {
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::Cancelled, "cancelled", "HikCamera::nextFrame"));
    }

    const std::lock_guard lock(mutex_);
    if (!opened_ || !camera_) {
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::DeviceDisconnected, "device is not open",
            "HikCamera::nextFrame"));
    }

    // Lazy StartGrabbing: parameters (trigger mode etc.) can be set freely
    // between open() and the first frame, mirroring the BasicDemo order.
    if (!grabbing_) {
        if (const int rc = camera_->StartGrabbing(); rc != MV_OK) {
            return sf::Result<sf::Frame>::failure(sf::makeError(
                sf::ErrorCode::InvalidState, "CMvCamera::StartGrabbing failed (0x" +
                                                 std::to_string(rc) + ")",
                "HikCamera::nextFrame"));
        }
        grabbing_ = true;
    }

    MV_FRAME_OUT raw{};
    const int rc = camera_->GetImageBuffer(&raw, kGrabTimeoutMs);
    if (rc != MV_OK) {
        if (!camera_->IsDeviceConnected()) {
            return sf::Result<sf::Frame>::failure(sf::makeError(
                sf::ErrorCode::DeviceDisconnected, "camera disconnected",
                "HikCamera::nextFrame"));
        }
        // Timeout without disconnect is normal in trigger mode - the
        // AcquisitionWorker keeps polling and the UI can send TriggerSoftware.
        return sf::Result<sf::Frame>::failure(sf::makeError(
            sf::ErrorCode::Timeout,
            "no frame within " + std::to_string(kGrabTimeoutMs) +
                " ms (triggered mode? send TriggerSoftware)",
            "HikCamera::nextFrame"));
    }

    const auto& frameInfo = raw.stFrameInfo;
    const std::uint32_t width  = frameInfo.nExtendWidth ? frameInfo.nExtendWidth
                                                        : frameInfo.nWidth;
    const std::uint32_t height = frameInfo.nExtendHeight ? frameInfo.nExtendHeight
                                                         : frameInfo.nHeight;

    sf::Image image;
    image.width  = width;
    image.height = height;

    const auto srcType  = frameInfo.enPixelType;
    const auto* srcData = raw.pBufAddr;
    // The bundled SDK header has nFrameLen (and no nFrameLenEx).
    const auto  srcLen  = static_cast<unsigned int>(frameInfo.nFrameLen);

    if (srcType == PixelType_Gvsp_Mono8) {
        // Fast path: native Mono8 passes through unchanged.
        image.format = sf::PixelFormat::Mono8;
        const std::size_t expected  = static_cast<std::size_t>(width) * height;
        const std::size_t available = std::min<std::size_t>(expected, frameInfo.nFrameLen);
        image.data.resize(expected, 0);
        std::memcpy(image.data.data(), srcData, available);
    } else {
        // Deep mono -> Mono16; everything else (Bayer/color/YUV) -> RGB8.
        const bool deepMono =
            srcType == PixelType_Gvsp_Mono16 || srcType == PixelType_Gvsp_Mono14 ||
            srcType == PixelType_Gvsp_Mono12 || srcType == PixelType_Gvsp_Mono12_Packed ||
            srcType == PixelType_Gvsp_Mono10 || srcType == PixelType_Gvsp_Mono10_Packed;
        std::vector<std::uint8_t> converted;
        const MvGvspPixelType dstType =
            deepMono ? PixelType_Gvsp_Mono16 : PixelType_Gvsp_RGB8_Packed;
        if (!convertPixels(*camera_, width, height, srcType, srcData, srcLen, dstType,
                           converted)) {
            camera_->FreeImageBuffer(&raw);
            return sf::Result<sf::Frame>::failure(sf::makeError(
                sf::ErrorCode::UnsupportedFormat,
                "cannot convert pixel format " + std::to_string(srcType),
                "HikCamera::nextFrame"));
        }
        image.format = deepMono ? sf::PixelFormat::Mono16 : sf::PixelFormat::RGB8;
        image.data   = std::move(converted);
    }

    camera_->FreeImageBuffer(&raw);

    sf::Frame frame;
    frame.sequence  = frameInfo.nFrameNum;
    frame.timestamp = std::chrono::steady_clock::now().time_since_epoch();
    frame.timeBase  = "steady_clock";
    frame.deviceId  = info_.id;
    frame.image     = std::move(image);
    // Real per-frame metadata: read the actual nodes (MVCC_FLOATVALUE),
    // falling back to 0 when the node is not implemented by this camera.
    frame.exposureUs = static_cast<float>(getFloatValue(*camera_, "ExposureTime").fCurValue);
    frame.gainDb     = static_cast<float>(getFloatValue(*camera_, "Gain").fCurValue);
    return sf::Result<sf::Frame>::success(std::move(frame));
}
