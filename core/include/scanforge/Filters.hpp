#pragma once

// Point-cloud operators + error metrics. No PCL / Open3D dependency yet:
// implementing the simple versions by hand is the point of stage 1.
// Reference: PCL pcl/filters/voxel_grid.h (BSD-3), Open3D
// open3d/geometry/PointCloud.cpp VoxelDownSample (MIT), CloudCompare's
// scalar-field / distance computation (GPL - read the idea, not the code).

#include "Types.hpp"
#include "Result.hpp"
#include "Calibration.hpp"
#include "Contracts.hpp"

#include <string>

namespace sf {

// z = a*x + b*y + c  (least-squares fit over the cloud)
struct PlaneFit {
    double a = 0.0, b = 0.0, c = 0.0;
};

struct ErrorReport {
    std::size_t points   = 0;
    double      rmseMm   = 0.0;
    double      meanAbsMm = 0.0;
    double      maxAbsMm = 0.0;
    PlaneFit    fitted;
    std::string attribution;   // human-readable hint, filled by evaluate()
};

[[nodiscard]] Result<PointCloudPtr> voxelDownsample(PointCloudPtr input,
                                                    double leafMm);
[[nodiscard]] Result<PointCloudPtr> transformCloud(PointCloudPtr input,
                                                    const RigidTransform& transform);
[[nodiscard]] Result<PlaneFit>      fitPlaneLeastSquares(const PointCloud& cloud);
// Two complementary metrics, because they catch DIFFERENT fault classes:
//   * evaluateAgainstPlane -> distance to the ground-truth plane.
//     A triangulated point ALWAYS lies on the CALIBRATION plane, so this metric
//     is ~0 for pixel noise, but explodes for a stale/wrong calibration.
//   * evaluateAgainstCloud -> nearest-neighbour distance to the ground-truth
//     cloud. Sensitive to peak-detection noise, trigger jitter and motion.
// Keeping both is the whole point of layered error attribution.
[[nodiscard]] Result<ErrorReport> evaluateAgainstPlane(const PointCloud& cloud,
                                                       const Plane& truthPlane);
[[nodiscard]] Result<ErrorReport> evaluateAgainstCloud(const PointCloud& actual,
                                                       const PointCloud& truthCloud);

// A concrete IPointCloudOperator adapter, so cli/ and app/ use the same chain.
class VoxelFilterOperator final : public IPointCloudOperator {
public:
    explicit VoxelFilterOperator(double leafMm) : leafMm_(leafMm) {}
    [[nodiscard]] std::string name() const override { return "VoxelDownsample"; }
    [[nodiscard]] Result<PointCloudPtr> apply(PointCloudPtr input,
                                              const CancellationToken&) override {
        return voxelDownsample(std::move(input), leafMm_);
    }
private:
    double leafMm_;
};

class LaserReconstructor final : public IReconstructor {
public:
    [[nodiscard]] std::string name() const override { return "LaserLineTriangulation"; }
    [[nodiscard]] Result<PointCloudPtr> reconstruct(
        const Frame& frame, const CalibrationSet& calibration,
        const CancellationToken&) override {
        return triangulateLaserLine(frame, calibration);
    }
};

} // namespace sf
