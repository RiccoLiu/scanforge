#include "Docks.hpp"

#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace sf {

DeviceTreeDock::DeviceTreeDock(QWidget* parent)
    : QDockWidget(tr("Devices"), parent) {
    setObjectName("DeviceTreeDock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(3);
    tree_->setHeaderLabels(QStringList{"ID", "Model", "Serial"});
    tree_->setRootIsDecorated(false);
    setWidget(tree_);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (item) emit deviceActivated(item->text(0));
            });
    // A single click opens the device (MainWindow::onDeviceActivated). While
    // the tree is locked (acquisition running) clicks are swallowed.
    connect(tree_, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item || locked_) return;
                selectedId_ = item->text(0);
                emit deviceSelected(selectedId_);
            });
}

void DeviceTreeDock::setLocked(bool locked) {
    locked_ = locked;
    const QBrush grey(QColor(128, 128, 128));
    for (QTreeWidgetItem* it = tree_->topLevelItem(0); it;
         it                 = tree_->itemBelow(it)) {
        it->setDisabled(locked);
        for (int col = 0; col < tree_->columnCount(); ++col) {
            it->setForeground(col, locked ? grey : tree_->palette().brush(
                                       QPalette::WindowText));
        }
    }
    if (locked) tree_->clearSelection();
}

void DeviceTreeDock::highlightDevice(const QString& id) {
    const QBrush accent(QColor(0, 102, 204));
    QFont bold;
    bold.setBold(true);
    for (QTreeWidgetItem* it = tree_->topLevelItem(0); it;
         it                 = tree_->itemBelow(it)) {
        const bool active = it->text(0) == id;
        for (int col = 0; col < tree_->columnCount(); ++col) {
            it->setBackground(col, active ? QBrush(QColor(222, 238, 255))
                                          : QBrush());
            it->setForeground(col, active ? accent : tree_->palette().brush(
                                       QPalette::WindowText));
        }
        it->setFont(0, active ? bold : QFont());
    }
}

void DeviceTreeDock::setDevices(const QList<DeviceInfo>& devices) {
    tree_->clear();
    selectedId_.clear();   // a fresh enumeration invalidates the old choice
    for (const auto& info : devices) {
        auto* item = new QTreeWidgetItem(tree_);
        item->setText(0, QString::fromStdString(info.id));
        item->setText(1, QString::fromStdString(info.model));
        item->setText(2, QString::fromStdString(info.serial));
    }
}

void DeviceTreeDock::updateDevice(const DeviceInfo& info) {
    for (QTreeWidgetItem* it = tree_->topLevelItem(0); it;
         it                 = tree_->itemBelow(it)) {
        if (it->text(0) == QString::fromStdString(info.id)) {
            it->setText(1, QString::fromStdString(info.model));
            it->setText(2, QString::fromStdString(info.serial));
            return;
        }
    }
}

QString DeviceTreeDock::selectedDevice() const {
    // Drop the selection if it no longer matches a listed device.
    for (QTreeWidgetItem* it = tree_->topLevelItem(0); it;
         it                 = tree_->itemBelow(it)) {
        if (it->text(0) == selectedId_) return selectedId_;
    }
    return {};
}

ParameterDock::ParameterDock(QWidget* parent)
    : QDockWidget(tr("Parameters"), parent) {
    setObjectName("ParameterDock");
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    formHost_ = new QWidget(this);
    form_ = new QFormLayout(formHost_);
    setWidget(formHost_);
}

void ParameterDock::setParameters(const DeviceParamList& parameters) {
    // Rebuild the host widget instead of deleting rows one by one: QFormLayout
    // row removal is notoriously easy to get wrong.
    if (formHost_) {
        formHost_->deleteLater();
        formHost_ = nullptr;
        form_ = nullptr;
    }
    formHost_ = new QWidget(this);
    form_ = new QFormLayout(formHost_);

    for (const auto& p : parameters) {
        QWidget* editor = nullptr;
        const QString key = p.key;
        switch (p.type) {
        case ParamType::Boolean: {
            auto* w = new QCheckBox(formHost_);
            w->setChecked(p.value.toBool());
            connect(w, &QCheckBox::toggled, this,
                    [this, key](bool v) { emit parameterChanged(key, v); });
            editor = w;
            break;
        }
        case ParamType::Integer: {
            auto* w = new QSpinBox(formHost_);
            w->setRange(p.minimum.toInt(), p.maximum.toInt());
            w->setValue(p.value.toInt());
            connect(w, &QSpinBox::valueChanged, this,
                    [this, key](int v) { emit parameterChanged(key, v); });
            editor = w;
            break;
        }
        case ParamType::Real: {
            auto* w = new QDoubleSpinBox(formHost_);
            w->setDecimals(3);
            w->setRange(p.minimum.toDouble(), p.maximum.toDouble());
            w->setValue(p.value.toDouble());
            connect(w, &QDoubleSpinBox::valueChanged, this,
                    [this, key](double v) { emit parameterChanged(key, v); });
            editor = w;
            break;
        }
        case ParamType::Enum: {
            auto* w = new QComboBox(formHost_);
            w->addItems(p.options);
            w->setCurrentText(p.value.toString());
            connect(w, &QComboBox::currentTextChanged, this,
                    [this, key](const QString& v) { emit parameterChanged(key, v); });
            editor = w;
            break;
        }
        default:
            editor = new QLabel(p.value.toString(), formHost_);
            break;
        }
        editor->setEnabled(!p.readOnly);
        editor->setToolTip(p.description);
        const QString label = p.unit.isEmpty()
            ? p.displayName : p.displayName + " (" + p.unit + ")";
        form_->addRow(label, editor);
    }
    setWidget(formHost_);
}

LogDock::LogDock(QWidget* parent) : QDockWidget(tr("Log"), parent) {
    setObjectName("LogDock");
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    auto* host = new QWidget(this);
    auto* layout = new QVBoxLayout(host);
    filter_ = new QComboBox(host);
    filter_->addItems(QStringList{"DEBUG", "INFO", "WARN", "ERROR"});
    filter_->setCurrentIndex(1);
    view_ = new QPlainTextEdit(host);
    view_->setReadOnly(true);
    view_->setMaximumBlockCount(2000);   // bounded memory during long runs
    layout->addWidget(filter_);
    layout->addWidget(view_);
    setWidget(host);
}

void LogDock::append(const QString& level, const QString& text) {
    const QStringList levels{"DEBUG", "INFO", "WARN", "ERROR"};
    if (levels.indexOf(level) < levels.indexOf(filter_->currentText())) return;
    view_->appendPlainText("[" + level + "] " + text);
}

} // namespace sf
