#include "scanforge/Filters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>

namespace sf {
namespace {

struct Accumulator {
    double x = 0.0, y = 0.0, z = 0.0, intensity = 0.0;
    std::uint32_t count = 0;
};

using VoxelKey = std::array<long long, 3>;

VoxelKey voxelKey(const PointXYZI& p, double leaf) {
    return VoxelKey{
        static_cast<long long>(std::floor(p.x / leaf)),
        static_cast<long long>(std::floor(p.y / leaf)),
        static_cast<long long>(std::floor(p.z / leaf))};
}

// 3x3 Gaussian elimination for the normal equations of z = a*x + b*y + c.
bool solve3(std::array<std::array<double, 3>, 3> A,
            std::array<double, 3> b, std::array<double, 3>& out) {
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 3; ++row)
            if (std::abs(A[row][col]) > std::abs(A[pivot][col])) pivot = row;
        if (std::abs(A[pivot][col]) < 1e-12) return false;
        std::swap(A[pivot], A[col]);
        std::swap(b[pivot], b[col]);
        for (int row = col + 1; row < 3; ++row) {
            const double factor = A[row][col] / A[col][col];
            for (int k = col; k < 3; ++k) A[row][k] -= factor * A[col][k];
            b[row] -= factor * b[col];
        }
    }
    for (int row = 2; row >= 0; --row) {
        double sum = b[row];
        for (int k = row + 1; k < 3; ++k) sum -= A[row][k] * out[k];
        out[row] = sum / A[row][row];
    }
    return true;
}

} // namespace

Result<PointCloudPtr> voxelDownsample(PointCloudPtr input, double leafMm) {
    if (!input) {
        return Result<PointCloudPtr>::failure(makeError(
            ErrorCode::InvalidArgument, "null cloud", "voxelDownsample"));
    }
    if (!(leafMm > 0.0)) {
        return Result<PointCloudPtr>::failure(makeError(
            ErrorCode::InvalidArgument, "leaf size must be positive", "voxelDownsample"));
    }
    std::map<VoxelKey, Accumulator> voxels;
    for (const auto& p : input->points) {
        auto& acc = voxels[voxelKey(p, leafMm)];
        acc.x += p.x; acc.y += p.y; acc.z += p.z;
        acc.intensity += p.intensity;
        ++acc.count;
    }
    auto output = std::make_shared<PointCloud>();
    output->unit = input->unit;
    output->frameId = input->frameId;
    output->sequence = input->sequence;
    output->timestamp = input->timestamp;
    output->points.reserve(voxels.size());
    for (const auto& [key, acc] : voxels) {
        (void)key;
        const double n = static_cast<double>(acc.count);
        output->points.push_back(PointXYZI{
            static_cast<float>(acc.x / n), static_cast<float>(acc.y / n),
            static_cast<float>(acc.z / n), static_cast<float>(acc.intensity / n)});
    }
    return Result<PointCloudPtr>::success(std::move(output));
}

Result<PointCloudPtr> transformCloud(PointCloudPtr input,
                                     const RigidTransform& transform) {
    if (!input) {
        return Result<PointCloudPtr>::failure(makeError(
            ErrorCode::InvalidArgument, "null cloud", "transformCloud"));
    }
    auto output = std::make_shared<PointCloud>(*input);
    const auto& R = transform.rotation;
    const auto& t = transform.translation;
    for (auto& p : output->points) {
        const double x = p.x, y = p.y, z = p.z;
        p.x = static_cast<float>(R[0]*x + R[1]*y + R[2]*z + t[0]);
        p.y = static_cast<float>(R[3]*x + R[4]*y + R[5]*z + t[1]);
        p.z = static_cast<float>(R[6]*x + R[7]*y + R[8]*z + t[2]);
    }
    return Result<PointCloudPtr>::success(std::move(output));
}

Result<PlaneFit> fitPlaneLeastSquares(const PointCloud& cloud) {
    if (cloud.size() < 3) {
        return Result<PlaneFit>::failure(makeError(
            ErrorCode::InvalidArgument, "need >= 3 points", "fitPlaneLeastSquares"));
    }
    double sxx = 0, sxy = 0, sx = 0, syy = 0, sy = 0, n = 0;
    double sxz = 0, syz = 0, sz = 0;
    for (const auto& p : cloud.points) {
        const double x = p.x, y = p.y, z = p.z;
        sxx += x*x; sxy += x*y; sx += x;
        syy += y*y; sy += y;   n += 1.0;
        sxz += x*z; syz += y*z; sz += z;
    }
    std::array<std::array<double, 3>, 3> A{{
        {sxx, sxy, sx}, {sxy, syy, sy}, {sx, sy, n}}};
    std::array<double, 3> b{sxz, syz, sz};
    std::array<double, 3> solution{0.0, 0.0, 0.0};
    if (!solve3(A, b, solution)) {
        return Result<PlaneFit>::failure(makeError(
            ErrorCode::ProcessingFailed, "degenerate point distribution",
            "fitPlaneLeastSquares"));
    }
    return Result<PlaneFit>::success(PlaneFit{solution[0], solution[1], solution[2]});
}

Result<ErrorReport> evaluateAgainstPlane(const PointCloud& cloud,
                                          const Plane& truthPlane) {
    if (cloud.empty()) {
        return Result<ErrorReport>::failure(makeError(
            ErrorCode::InvalidArgument, "empty cloud", "evaluateAgainstPlane"));
    }
    const double norm = std::sqrt(truthPlane.a * truthPlane.a +
                                  truthPlane.b * truthPlane.b +
                                  truthPlane.c * truthPlane.c);
    if (norm < 1e-12) {
        return Result<ErrorReport>::failure(makeError(
            ErrorCode::CalibrationInvalid, "degenerate truth plane",
            "evaluateAgainstPlane"));
    }
    ErrorReport report;
    report.points = cloud.size();
    double sumSquared = 0.0, sumAbs = 0.0;
    for (const auto& p : cloud.points) {
        const double distance = std::abs(truthPlane.a * p.x +
                                         truthPlane.b * p.y +
                                         truthPlane.c * p.z +
                                         truthPlane.d) / norm;
        sumSquared += distance * distance;
        sumAbs += distance;
        report.maxAbsMm = std::max(report.maxAbsMm, distance);
    }
    report.rmseMm = std::sqrt(sumSquared / static_cast<double>(cloud.size()));
    report.meanAbsMm = sumAbs / static_cast<double>(cloud.size());

    if (const auto fit = fitPlaneLeastSquares(cloud)) report.fitted = fit.value();

    // Layered attribution hint. This is the teaching payload of the exercise:
    // a large, roughly uniform bias points at calibration; a drifting error
    // along the scan direction points at timing / mechanics.
    if (report.rmseMm < 0.35) {
        report.attribution =
            "误差处于像素量化与噪声量级，链路健康。检查项：曝光/光源稳定性。";
    } else if (std::abs(report.meanAbsMm - report.rmseMm) / report.rmseMm < 0.15) {
        report.attribution =
            "误差近似均匀偏置，优先怀疑标定参数（光平面 d / 外参），"
            "其次检查温漂与机械位移；时间偏移通常不会造成这种形态。";
    } else {
        report.attribution =
            "误差分布不均匀，优先检查触发与时间同步、帧间运动与振动，"
            "再检查畸变校正与配准初值。";
    }
    return Result<ErrorReport>::success(report);
}

Result<ErrorReport> evaluateAgainstCloud(const PointCloud& actual,
                                         const PointCloud& truthCloud) {
    if (actual.empty() || truthCloud.empty()) {
        return Result<ErrorReport>::failure(makeError(
            ErrorCode::InvalidArgument, "empty cloud", "evaluateAgainstCloud"));
    }

    // Stage 1 uses brute force: N*M is fine for a few thousand points and
    // keeps the code dependency-free. Stage 5 replaces this with a KD-tree
    // (Open3D / PCL), but the metric definition must stay identical.
    ErrorReport report;
    report.points = actual.size();
    double sumSquared = 0.0, sumAbs = 0.0;
    for (const auto& p : actual.points) {
        double bestSquared = std::numeric_limits<double>::infinity();
        for (const auto& q : truthCloud.points) {
            const double dx = p.x - q.x;
            const double dy = p.y - q.y;
            const double dz = p.z - q.z;
            const double d2 = dx*dx + dy*dy + dz*dz;
            if (d2 < bestSquared) bestSquared = d2;
        }
        const double distance = std::sqrt(bestSquared);
        sumSquared += distance * distance;
        sumAbs += distance;
        report.maxAbsMm = std::max(report.maxAbsMm, distance);
    }
    report.rmseMm = std::sqrt(sumSquared / static_cast<double>(actual.size()));
    report.meanAbsMm = sumAbs / static_cast<double>(actual.size());
    if (const auto fit = fitPlaneLeastSquares(actual)) report.fitted = fit.value();
    report.attribution =
        "点云到真值点云最近邻距离：对条纹峰值噪声、触发抖动、帧间运动与"
        "畸变敏感；对「整体光平面标定偏置」不敏感，需与平面距离指标一起看。";
    return Result<ErrorReport>::success(report);
}

} // namespace sf
