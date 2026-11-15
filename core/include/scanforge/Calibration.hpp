#pragma once

// Geometry contracts. Everything here is CALIBRATION DATA + PROJECTION MATH,
// not algorithm research. You must be able to read an error report and decide
// whether the cause is exposure, trigger, intrinsics or mechanics.
// Reference: OpenCV modules/calib3d (Apache-2.0 since 4.5.0) for the camera
// model; Kalibr (BSD) for the time-offset / extrinsics report format.

#include "Types.hpp"
#include "Result.hpp"

#include <array>
#include <string>
#include <vector>

namespace sf {

struct CameraIntrinsics {
    double fx = 1000.0, fy = 1000.0;
    double cx =  320.0, cy =  240.0;
    double k1 = 0.0, k2 = 0.0, p1 = 0.0, p2 = 0.0, k3 = 0.0;

    [[nodiscard]] bool valid() const noexcept { return fx > 0.0 && fy > 0.0; }
};

// Plane equation a*X + b*Y + c*Z + d = 0, expressed in the CAMERA frame.
// For a plane written as Z = z0 + alpha*X + beta*Y use:
//   {a=alpha, b=beta, c=-1.0, d=z0}
struct Plane {
    double a = 0.0, b = 0.0, c = -1.0, d = 300.0;

    [[nodiscard]] bool valid() const noexcept { return c != 0.0 || a != 0.0 || b != 0.0; }
};

// Row-major 3x3 rotation + translation, camera -> scanner coordinate frame.
struct RigidTransform {
    std::array<double, 9> rotation{1.0, 0.0, 0.0,
                                   0.0, 1.0, 0.0,
                                   0.0, 0.0, 1.0};
    std::array<double, 3> translation{0.0, 0.0, 0.0};
};

struct CalibrationSet {
    CameraIntrinsics camera;
    Plane            laserPlane;
    RigidTransform   cameraToScanner;
    LengthUnit       unit = LengthUnit::Millimeter;
    std::string      coordinateFrame = "camera";
    std::string      sourceFile;
    double           reprojectionErrorRmsPx = -1.0;  // -1 means "not calibrated"
    Nanos            cameraToImuOffset{0};           // reserved for multi-sensor

    [[nodiscard]] bool valid() const noexcept {
        return camera.valid() && laserPlane.valid();
    }
};

// A 3D point and the pixel where it was observed. Used for reprojection error.
struct Correspondence {
    double X = 0.0, Y = 0.0, Z = 0.0;
    double u = 0.0, v = 0.0;
};

// ---------------------------------------------------------------------------
// Stage 1 implementations (single-line laser triangulation).
// Deliberately simple: no distortion correction yet, that is a roadmap item.
// ---------------------------------------------------------------------------

// For every image column, find the brightest pixel, back-project a ray and
// intersect it with the calibrated laser plane. This is the whole "image ->
// point cloud" step in line-laser scanning.
[[nodiscard]] Result<PointCloudPtr> triangulateLaserLine(
    const Frame& frame, const CalibrationSet& calibration);

// RMS reprojection error in pixels. A tight, centred distribution is good;
// a long tail usually means bad board flatness or motion blur, not bad math.
[[nodiscard]] Result<double> reprojectionRms(
    const std::vector<Correspondence>& samples, const CameraIntrinsics& camera);

} // namespace sf
