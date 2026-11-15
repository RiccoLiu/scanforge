#include "scanforge/AcquisitionWorker.hpp"

#include <QImage>
#include <cstring>

namespace {

QImage toGrayscale(const sf::Image& image) {
    if (!image.valid() || image.format != sf::PixelFormat::Mono8) return {};
    QImage output(static_cast<int>(image.width), static_cast<int>(image.height),
                  QImage::Format_Grayscale8);
    for (std::uint32_t row = 0; row < image.height; ++row) {
        std::memcpy(output.scanLine(static_cast<int>(row)),
                    image.data.data() + static_cast<std::size_t>(row) * image.width,
                    image.width);
    }
    return output;
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
            if (error.code != ErrorCode::EndOfStream &&
                error.code != ErrorCode::Cancelled) {
                emit failed(error);
            }
            break;
        }
        emit previewReady(toGrayscale(frame.value().image));
        emit frameReady(frame.value());
        ++count;
    }
    emit finished(count);
}

} // namespace sf
