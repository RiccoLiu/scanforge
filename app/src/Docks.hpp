#pragma once

// The three dock widgets. Every dock sets a UNIQUE objectName - without it
// QMainWindow::saveState()/restoreState() silently fails to restore layout.
// Reference: CloudCompare mainwindow.cpp dock setup (GPL - concept only),
// Qt docs "QMainWindow / QDockWidget".

#include <QDockWidget>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

#include "scanforge/IDevice.hpp"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QPlainTextEdit;
class QTreeWidget;
class QWidget;

namespace sf {

class DeviceTreeDock : public QDockWidget {
    Q_OBJECT
public:
    explicit DeviceTreeDock(QWidget* parent = nullptr);
    void setDevices(const QList<DeviceInfo>& devices);

    // Refreshes the tree row whose id matches info.id (model + serial
    // columns). Some devices only know their real identity after open();
    // call this then so the tree shows the actual hardware.
    void updateDevice(const DeviceInfo& info);

    // Id of the device the user last CLICKED (single click marks it,
    // double click also opens it). Empty when nothing was clicked since
    // the last setDevices() - MainWindow falls back to the first device.
    [[nodiscard]] QString selectedDevice() const;

    // While an acquisition is running the whole list is read-only: every
    // row is greyed out and clicks are ignored. false restores clicking.
    void setLocked(bool locked);

    // Highlights the opened device row (bold + accent background) and
    // dims the rest, so the active camera is obvious at a glance.
    void highlightDevice(const QString& id);

signals:
    void deviceActivated(const QString& id);
    void deviceSelected(const QString& id);   // single click

private:
    QTreeWidget* tree_ = nullptr;
    QString      selectedId_;
    bool         locked_ = false;
};

class ParameterDock : public QDockWidget {
    Q_OBJECT
public:
    explicit ParameterDock(QWidget* parent = nullptr);

    // The form is GENERATED from the plugin's GenICam-like node list. Adding a
    // new parameter to a device never requires touching the UI code.
    void setParameters(const DeviceParamList& parameters);

signals:
    void parameterChanged(const QString& key, const QVariant& value);

private:
    QWidget*     formHost_ = nullptr;
    QFormLayout* form_     = nullptr;
};

class LogDock : public QDockWidget {
    Q_OBJECT
public:
    explicit LogDock(QWidget* parent = nullptr);

public slots:
    void append(const QString& level, const QString& text);

private:
    QPlainTextEdit* view_   = nullptr;
    QComboBox*      filter_ = nullptr;
};

} // namespace sf
