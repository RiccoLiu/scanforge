#include "scanforge/AcquisitionWorker.hpp"

#include <QImage>
#include <cstring>

namespace {

// Preview conversion for every pixel format a device plugin may deliver.
// Mono8 passes through; Mono16 is windowed down to 8 bit; RGB8 (color cameras,
// e.g. via the Hikvision plugin's pixel conversion) is mapped to QImage RGB888.
QImage toPreview(const sf::Image& image) {
    if (!image.valid()) return {};
    const int width  = static_cast<int>(image.width);
    const int height = static_cast<int>(image.height);
    const auto* data = image.data.data();

    if (image.format == sf::PixelFormat::Mono8) {
        QImage output(width, height, QImage::Format_Grayscale8);
        for (std::uint32_t row = 0; row < image.height; ++row) {
            std::memcpy(output.scanLine(static_cast<int>(row)),
                        data + static_cast<std::size_t>(row) * image.width,
                        image.width);
        }
        return output;
    }

    if (image.format == sf::PixelFormat::Mono16) {
        QImage output(width, height, QImage::Format_Grayscale8);
        const auto* pixels = reinterpret_cast<const std::uint16_t*>(data);
        for (int row = 0; row < height; ++row) {
            auto* line = static_cast<std::uint8_t*>(output.scanLine(row));
            const std::size_t offset = static_cast<std::size_t>(row) * image.width;
            for (int col = 0; col < width; ++col) {
                line[col] = static_cast<std::uint8_t>(pixels[offset + col] >> 8);
            }
        }
        return output;
    }

    if (image.format == sf::PixelFormat::RGB8) {
        QImage output(width, height, QImage::Format_RGB888);
        const std::size_t stride = static_cast<std::size_t>(image.width) * 3;
        for (std::uint32_t row = 0; row < image.height; ++row) {
            std::memcpy(output.scanLine(static_cast<int>(row)),
                        data + static_cast<std::size_t>(row) * stride, stride);
        }
        return output;
    }

    return {};
}

} // namespace

namespace sf {

void AcquisitionWorker::start(IDevice* device, CancellationToken token) {
    quint64 count = 0;
    if (!device) {
        emit failed(makeError(ErrorCode::DeviceNotFound,
                              "null device", "AcquisitionWorker::start"));
        emit finished(0);
        return;
    }

    // This loop owns the thread. Do NOT add a blocking wait here that depends
    // on a queued signal - the event loop is not running while we are inside.
    while (!token.isCancelled()) {
        Result<Frame> frame = device->nextFrame(token);
        if (!frame) {
            const Error& error = frame.error();
            if (error.code == ErrorCode::Timeout) {
                // Triggered cameras legitimately produce no frame until a
                // trigger arrives; keep the acquisition loop alive.
                continue;
            }
            if (error.code != ErrorCode::EndOfStream &&
                error.code != ErrorCode::Cancelled) {
                emit failed(error);
            }
            break;
        }
        emit previewReady(toPreview(frame.value().image));
        emit frameReady(frame.value());
        ++count;
    }
    emit finished(count);
}

} // namespace sf
