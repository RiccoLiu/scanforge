#include "MainWindow.hpp"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QVBoxLayout>
#include <QMenuBar>

#include "scanforge/PlyIO.hpp"
#include "scanforge/Synthetic.hpp"

namespace sf {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ScanForge - Industrial 3D Scanning Host (reference implementation)");
    resize(1280, 800);

    // --- central area: QMainWindow must NOT be given a layout directly ---
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    tabs_ = new QTabWidget(central);
    tabs_->setObjectName("CentralTabs");
    view_    = new PointCloudView();
    preview_ = new QLabel(tr("No preview yet"));
    preview_->setAlignment(Qt::AlignCenter);
    report_  = new QTextBrowser();
    report_->setPlainText(tr("Run a scan, then export the measurement report."));
    tabs_->addTab(view_, tr("3D Point Cloud"));
    tabs_->addTab(preview_, tr("2D Preview"));
    tabs_->addTab(report_, tr("Report"));
    centralLayout->addWidget(tabs_);
    setCentralWidget(central);

    // --- docks: unique objectName is required for saveState/restoreState ---
    devices_ = new DeviceTreeDock(this);
    params_  = new ParameterDock(this);
    log_     = new LogDock(this);
    addDockWidget(Qt::LeftDockWidgetArea,  devices_);
    addDockWidget(Qt::RightDockWidgetArea, params_);
    addDockWidget(Qt::BottomDockWidgetArea, log_);

    // --- toolbar ---
    QToolBar* bar = addToolBar(tr("Main"));
    bar->setObjectName("MainToolBar");
    connectAction_ = bar->addAction(tr("Connect"));
    startAction_   = bar->addAction(tr("Start"));
    stopAction_    = bar->addAction(tr("Stop"));
    bar->addSeparator();
    exportAction_  = bar->addAction(tr("Export PLY"));

    statusLabel_ = new QLabel(tr("unit: mm | frame: camera | points: 0"), this);
    statusBar()->addPermanentWidget(statusLabel_);

    // --- wiring ---
    connect(connectAction_, &QAction::triggered, this, &MainWindow::onConnect);
    connect(startAction_,   &QAction::triggered, this, &MainWindow::onStart);
    connect(stopAction_,    &QAction::triggered, this, &MainWindow::onStop);
    connect(exportAction_,  &QAction::triggered, this, &MainWindow::onExport);

    connect(devices_, &DeviceTreeDock::deviceActivated,
            this,     &MainWindow::onDeviceActivated);
    connect(devices_, &DeviceTreeDock::deviceSelected, this,
            [this](const QString& id) { onDeviceActivated(id); });
    connect(params_,  &ParameterDock::parameterChanged,
            this,     &MainWindow::onParameterChanged);

    connect(&controller_, &PipelineController::cloudReady,
            this,         &MainWindow::onCloud);
    connect(&controller_, &PipelineController::previewReady,
            this,         &MainWindow::onPreview);
    connect(&controller_, &PipelineController::stateChanged,
            this,         &MainWindow::onStateChanged);
    connect(&controller_, &PipelineController::failed,
            this,         &MainWindow::onError);
    connect(&controller_, &PipelineController::logMessage, log_, &LogDock::append);
    connect(&manager_,    &DeviceManager::logMessage,      log_, &LogDock::append);
    connect(this,         &MainWindow::logLine,            log_, &LogDock::append);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(devices_->toggleViewAction());
    viewMenu->addAction(params_->toggleViewAction());
    viewMenu->addAction(log_->toggleViewAction());

    QSettings settings;
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    updateActions(controller_.state());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    controller_.stop();
    manager_.releaseAll();
    QSettings settings;
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::updateActions(DeviceState state) {
    // ONE source of truth: the same canTransition() table used by the
    // controller. No duplicated enable/disable logic in every widget.
    // Connect is allowed whenever no acquisition is in progress - including
    // Idle (connected, stopped, or after a finished scan).
    const bool idle = !controller_.isRunning();
    connectAction_->setEnabled(idle);
    startAction_->setEnabled(current_ != nullptr &&
                             canTransition(state, DeviceEvent::StartAcquire));
    stopAction_->setEnabled(canTransition(state, DeviceEvent::StopAcquire));
    exportAction_->setEnabled(lastCloud_ != nullptr);
    params_->setEnabled(!controller_.isRunning());   // no parameter changes mid-scan
}

void MainWindow::onConnect() {
    // Connect enumerates the plugin directory and lists every physical
    // device it exposes (one tree row per device). Opening happens when the
    // user single-clicks a row (onDeviceActivated).
    const bool idle = !controller_.isRunning();
    if (!idle) {
        emit logLine("WARN", "cannot scan while acquisition is running");
        return;
    }
    const QString pluginDir = QCoreApplication::applicationDirPath() + "/plugins";
    const int count = manager_.scan(pluginDir);
    emit logLine("INFO", QString("found %1 device(s) in %2").arg(count).arg(pluginDir));
    devices_->setDevices(manager_.discovered());
    current_ = nullptr;   // nothing open yet - a click picks the target
    params_->setParameters({});
    updateActions(controller_.state());
}

void MainWindow::onDeviceActivated(const QString& id) {
    if (controller_.isRunning()) return;
    clickedDeviceId_ = id;
    current_ = manager_.acquire(id);
    if (!current_) { emit logLine("ERROR", "cannot open device " + id); return; }
    // open() may have learned the real hardware identity (model/serial are
    // only readable from the device itself) - refresh the tree row.
    devices_->updateDevice(current_->info());
    params_->setParameters(current_->parameters());
    emit logLine("INFO", "device opened: " + id);
    // Mark the opened row (bold + accent) so the active camera is obvious.
    devices_->highlightDevice(id);
    // Device is open -> Start must light up right now.
    controller_.noteDeviceConnected();
    updateActions(controller_.state());
}

void MainWindow::onParameterChanged(const QString& key, const QVariant& value) {
    if (!current_) return;
    if (controller_.isRunning()) {
        emit logLine("WARN", "parameter changes are locked during acquisition");
        return;
    }
    const Status status = current_->setParameter(key, value);
    if (!status)
        emit logLine("ERROR", QString::fromStdString(status.error().toString()));
}

void MainWindow::onStart() {
    if (!current_) { emit logLine("WARN", "connect a device first"); return; }
    // Stage 1 uses the synthetic calibration. Stage 5 loads it from a
    // calibration file / plugin node instead of hard-coding it here.
    calibration_ = makeDefaultScene().truth;
    if (!controller_.start(current_, calibration_))
        emit logLine("ERROR", "failed to start acquisition");
    devices_->setLocked(true);   // no device switching while grabbing
    updateActions(controller_.state());
}

void MainWindow::onStop() {
    controller_.stop();
    devices_->setLocked(false);   // clicking devices is allowed again
    updateActions(controller_.state());
}

void MainWindow::onCloud(PointCloudPtr cloud, double elapsedMs) {
    lastCloud_ = std::move(cloud);
    view_->setCloud(lastCloud_, elapsedMs);
    const QString unit = lastCloud_->unit == LengthUnit::Meter ? "m" : "mm";
    statusLabel_->setText(QString("unit: %1 | frame: %2 | points: %3 | processing: %4 ms")
        .arg(unit)
        .arg(QString::fromStdString(lastCloud_->frameId))
        .arg(lastCloud_->size())
        .arg(elapsedMs, 0, 'f', 2));
    updateActions(controller_.state());
}

void MainWindow::onPreview(QImage image) {
    if (image.isNull()) return;
    preview_->setPixmap(QPixmap::fromImage(image).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

void MainWindow::onStateChanged(DeviceState state) {
    // Unlock the device tree whenever the pipeline leaves the running
    // states - covers both manual Stop and a naturally finished scan.
    if (!controller_.isRunning()) devices_->setLocked(false);
    updateActions(state);
    emit logLine("INFO", QString("device state -> %1")
                 .arg(QString::fromUtf8(toString(state))));
}

void MainWindow::onError(Error error) {
    emit logLine("ERROR", QString::fromStdString(error.toString()));
    updateActions(controller_.state());
}

void MainWindow::onExport() {
    if (!lastCloud_) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export point cloud"), "scan.ply", tr("PLY (*.ply)"));
    if (path.isEmpty()) return;
    const Status status = writePly(path.toStdString(), *lastCloud_);
    if (!status) {
        emit logLine("ERROR", QString::fromStdString(status.error().toString()));
        return;
    }
    emit logLine("INFO", "exported " + path);
}

} // namespace sf
