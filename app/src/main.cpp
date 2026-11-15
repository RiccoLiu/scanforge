#include "MainWindow.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>

#include "scanforge/IDevice.hpp"

int main(int argc, char** argv) {
    // Core Profile must be requested BEFORE the QApplication / GL context is
    // created. macOS is out of scope for stage 1 (Qt uses Metal there); the
    // stage-5 fallback is Qt RHI or VTK.
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ScanForge");
    QCoreApplication::setApplicationName("ScanForge");
    QCoreApplication::setApplicationVersion("0.1.0");

    // Mandatory for Qt::QueuedConnection arguments crossing the three lanes.
    qRegisterMetaType<sf::Frame>("sf::Frame");
    qRegisterMetaType<sf::PointCloudPtr>("sf::PointCloudPtr");
    qRegisterMetaType<sf::Error>("sf::Error");
    qRegisterMetaType<sf::DeviceState>("sf::DeviceState");

    sf::MainWindow window;
    window.show();
    return QApplication::exec();
}
