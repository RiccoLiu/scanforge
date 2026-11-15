#include "PointCloudView.hpp"

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <limits>

namespace sf {

PointCloudView::PointCloudView(QWidget* parent) : QOpenGLWidget(parent) {
    vbo_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    setFocusPolicy(Qt::StrongFocus);
}

PointCloudView::~PointCloudView() {
    makeCurrent();
    vbo_.destroy();
    vao_.destroy();
    program_.removeAllShaders();
    doneCurrent();
}

void PointCloudView::setCloud(PointCloudPtr cloud, double processingMs) {
    pending_ = std::move(cloud);
    lastProcessingMs_ = processingMs;
    dirty_ = true;
    update();   // schedules paintGL() on the GUI thread
}

void PointCloudView::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_PROGRAM_POINT_SIZE);

    // Shaders live in resources.qrc; no hand-written shader strings in C++.
    if (!program_.addShaderFromSourceFile(QOpenGLShader::Vertex,
                                          ":/shaders/point.vert") ||
        !program_.addShaderFromSourceFile(QOpenGLShader::Fragment,
                                          ":/shaders/point.frag") ||
        !program_.link()) {
        qWarning("PointCloudView: shader setup failed: %s",
                 qPrintable(program_.log()));
        return;
    }
    uMVP_       = program_.uniformLocation("uMVP");
    uPointSize_ = program_.uniformLocation("uPointSize");
    uZMin_      = program_.uniformLocation("uZMin");
    uZMax_      = program_.uniformLocation("uZMax");

    vao_.create();
    vbo_.create();
    {
        QOpenGLVertexArrayObject::Binder binder(&vao_);
        vbo_.bind();
        program_.enableAttributeArray(0);
        program_.setAttributeBuffer(0, GL_FLOAT, 0, 3, 4 * sizeof(float));
        program_.enableAttributeArray(1);
        program_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 1,
                                    4 * sizeof(float));
        vbo_.release();
    }
    updateViewMatrix();
}

void PointCloudView::resizeGL(int width, int height) {
    projection_.setToIdentity();
    projection_.perspective(45.0f,
        static_cast<float>(width) / static_cast<float>(std::max(1, height)),
        1.0f, 10000.0f);
    updateViewMatrix();
}

void PointCloudView::updateViewMatrix() {
    const float cp = std::cos(pitch_);
    const QVector3D eye(distance_ * cp * std::sin(yaw_),
                        distance_ * std::sin(pitch_),
                        distance_ * cp * std::cos(yaw_));
    view_.setToIdentity();
    view_.lookAt(eye, QVector3D(0.0f, 0.0f, 300.0f), QVector3D(0.0f, 1.0f, 0.0f));
}

void PointCloudView::uploadPending() {
    dirty_ = false;
    if (!pending_) { pointCount_ = 0; return; }

    vertices_.clear();
    vertices_.reserve(pending_->size() * 4);
    zMin_ = std::numeric_limits<float>::max();
    zMax_ = -std::numeric_limits<float>::max();
    for (const auto& p : pending_->points) {
        vertices_.push_back(p.x);
        vertices_.push_back(p.y);
        vertices_.push_back(p.z);
        vertices_.push_back(p.intensity);
        zMin_ = std::min(zMin_, p.z);
        zMax_ = std::max(zMax_, p.z);
    }
    unitText_ = pending_->unit == LengthUnit::Meter ? "m" : "mm";
    frameIdText_ = QString::fromStdString(pending_->frameId);

    const int bytes = static_cast<int>(vertices_.size() * sizeof(float));
    vbo_.bind();
    // Allocate ONCE and grow geometrically; subsequent frames use write()
    // (glBufferSubData under the hood). No allocation inside paintGL().
    if (bytes > bufferCapacity_) {
        vbo_.allocate(bytes * 2);
        bufferCapacity_ = bytes * 2;
    }
    if (bytes > 0) vbo_.write(0, vertices_.data(), bytes);
    vbo_.release();

    pointCount_ = static_cast<int>(pending_->size());
    pending_.reset();
}

void PointCloudView::paintGL() {
    QPainter painter(this);
    painter.beginNativePainting();

    glClearColor(0.09f, 0.10f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (dirty_) uploadPending();
    if (pointCount_ > 0 && uMVP_ >= 0) {
        program_.bind();
        program_.setUniformValue(uMVP_, projection_ * view_);
        program_.setUniformValue(uPointSize_,
                                 2.5f * static_cast<float>(devicePixelRatioF()));
        program_.setUniformValue(uZMin_, zMin_);
        program_.setUniformValue(uZMax_, zMax_);
        {
            QOpenGLVertexArrayObject::Binder binder(&vao_);
            glDrawArrays(GL_POINTS, 0, pointCount_);
        }
        program_.release();
    }
    painter.endNativePainting();

    // HUD: frame cost, point count AND the unit / coordinate frame. Showing
    // units permanently prevents the classic mm-vs-m import accident.
    painter.setPen(QColor(230, 235, 245));
    painter.drawText(12, 22,
        QString("points: %1 | processing: %2 ms | unit: %3 | frame: %4")
            .arg(pointCount_)
            .arg(lastProcessingMs_, 0, 'f', 2)
            .arg(unitText_)
            .arg(frameIdText_));
    painter.end();
}

void PointCloudView::mousePressEvent(QMouseEvent* event) {
    lastMousePos_ = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void PointCloudView::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->pos() - lastMousePos_;
        lastMousePos_ = event->pos();
        yaw_   -= static_cast<float>(delta.x()) * 0.01f;
        pitch_ = std::clamp(pitch_ + static_cast<float>(delta.y()) * 0.01f,
                            -1.4f, 1.4f);
        updateViewMatrix();
        update();
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void PointCloudView::wheelEvent(QWheelEvent* event) {
    const float factor = event->angleDelta().y() > 0 ? 0.9f : 1.1f;
    distance_ = std::clamp(distance_ * factor, 50.0f, 5000.0f);
    updateViewMatrix();
    update();
    QOpenGLWidget::wheelEvent(event);
}

} // namespace sf
