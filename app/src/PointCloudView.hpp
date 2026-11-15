#pragma once

// Display layer only. 15-20% of the project effort on purpose: PCL/VTK/
// Open3D already wrap OpenGL, and hand-written shader boilerplate is exactly
// the kind of code AI generates best. Your OpenGL background goes into the
// parts that matter - stable upload, LOD, no allocation in paintGL().
// Reference: CloudCompare ccGLWindowInterface (GPL - idea only), Potree
// octree LOD (BSD), Open3D visualization/ (MIT), Qt QOpenGLWidget docs.

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include <vector>

#include "scanforge/Types.hpp"

namespace sf {

class PointCloudView : public QOpenGLWidget,
                       protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PointCloudView(QWidget* parent = nullptr);
    ~PointCloudView() override;

    // Called on the GUI thread. Copies to a pending buffer, NEVER touches a
    // GL context from a worker thread (a GL context is thread-affine).
    void setCloud(PointCloudPtr cloud, double processingMs = 0.0);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void uploadPending();
    void updateViewMatrix();

    QOpenGLShaderProgram      program_;
    QOpenGLBuffer             vbo_;
    QOpenGLVertexArrayObject  vao_;
    int uMVP_ = -1, uPointSize_ = -1, uZMin_ = -1, uZMax_ = -1;

    std::vector<float> vertices_;      // x, y, z, intensity (stride = 4 floats)
    PointCloudPtr      pending_;
    bool               dirty_ = false;
    int                bufferCapacity_ = 0;
    int                pointCount_ = 0;
    float              zMin_ = 0.0f, zMax_ = 1.0f;

    QMatrix4x4 projection_;
    QMatrix4x4 view_;
    float distance_ = 600.0f;
    float yaw_ = 0.0f, pitch_ = 0.25f;
    QPoint lastMousePos_;
    double lastProcessingMs_ = 0.0;
    QString unitText_ = "mm";
    QString frameIdText_ = "camera";
};

} // namespace sf
