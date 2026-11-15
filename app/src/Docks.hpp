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

signals:
    void deviceActivated(const QString& id);

private:
    QTreeWidget* tree_ = nullptr;
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
