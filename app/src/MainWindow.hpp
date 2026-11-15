#pragma once

// QMainWindow + 3 QDockWidget + central QTabWidget: the standard industrial
// host-application layout. QMainWindow ALREADY owns a layout (menu / toolbar /
// status / dock areas) - calling setLayout() on it destroys that layout.
// Business widgets therefore go on a separate central QWidget.
// Reference: CloudCompare mainwindow.cpp (GPL - layout concept only),
// Qt QMainWindow docs, neuvision apps/ UI skeleton.

#include <QMainWindow>
#include <QString>

#include "Docks.hpp"
#include "PointCloudView.hpp"
#include "scanforge/Calibration.hpp"
#include "scanforge/DeviceManager.hpp"
#include "scanforge/IDevice.hpp"
#include "scanforge/PipelineController.hpp"
#include "scanforge/Result.hpp"
#include "scanforge/Types.hpp"

class QAction;
class QCloseEvent;
class QImage;
class QLabel;
class QTabWidget;
class QTextBrowser;

namespace sf {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnect();
    void onStart();
    void onStop();
    void onExport();
    void onDeviceActivated(const QString& id);
    void onParameterChanged(const QString& key, const QVariant& value);
    void onCloud(PointCloudPtr cloud, double elapsedMs);
    void onPreview(QImage image);
    void onStateChanged(DeviceState state);
    void onError(Error error);

signals:
    void logLine(const QString& level, const QString& text);

private:
    void updateActions(DeviceState state);

    QTabWidget*   tabs_    = nullptr;
    PointCloudView* view_  = nullptr;
    QLabel*       preview_ = nullptr;
    QTextBrowser* report_  = nullptr;

    DeviceTreeDock* devices_ = nullptr;
    ParameterDock*  params_  = nullptr;
    LogDock*        log_     = nullptr;

    QAction* connectAction_ = nullptr;
    QAction* startAction_   = nullptr;
    QAction* stopAction_    = nullptr;
    QAction* exportAction_  = nullptr;
    QLabel*  statusLabel_   = nullptr;

    DeviceManager      manager_;      // declared BEFORE controller_ on purpose
    PipelineController controller_;
    IDevice*           current_ = nullptr;
    PointCloudPtr      lastCloud_;
    CalibrationSet     calibration_;
};

} // namespace sf
