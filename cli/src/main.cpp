// scanforge-cli: the SAME core pipeline without a GUI.
// This target is the strongest proof that core/ is genuinely decoupled, and it
// is what CI runs as a regression check.
//
//   scanforge-cli --frames 12 --bias-mm 0 --noise 0.8 \
//                 --output scan.ply --report report.md
//
// Try --bias-mm 2.0 and watch the RMSE jump: that is layered error attribution
// against known ground truth, which real hardware can never give you.

#include "scanforge/Calibration.hpp"
#include "scanforge/Contracts.hpp"
#include "scanforge/Filters.hpp"
#include "scanforge/PlyIO.hpp"
#include "scanforge/Synthetic.hpp"
#include "scanforge/Types.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace sf;

namespace {

struct Options {
    std::uint32_t frames = 12;
    double biasMm = 0.0;
    double noisePx = 0.8;
    double leafMm = 1.5;
    long long offsetMs = 0;
    std::string output = "scan.ply";
    std::string report = "scan-report.md";
    bool help = false;
};

Options parseOptions(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto value = [&](std::string& target) {
            if (i + 1 < argc) target = argv[++i];
        };
        auto number = [&](double& target) {
            if (i + 1 < argc) { try { target = std::stod(argv[++i]); } catch (...) {} }
        };
        if      (key == "--help" || key == "-h") o.help = true;
        else if (key == "--frames")   { double n = 0; number(n); o.frames = static_cast<std::uint32_t>(n); }
        else if (key == "--bias-mm")  number(o.biasMm);
        else if (key == "--noise")    number(o.noisePx);
        else if (key == "--leaf-mm")  number(o.leafMm);
        else if (key == "--offset-ms"){ double n = 0; number(n); o.offsetMs = static_cast<long long>(n); }
        else if (key == "--output")   value(o.output);
        else if (key == "--report")   value(o.report);
    }
    return o;
}

void printUsage() {
    std::cout
        << "scanforge-cli - headless ScanForge reference pipeline\n"
        << "Usage: scanforge-cli [options]\n"
        << "  --frames N        number of synthetic scan lines (default 12)\n"
        << "  --bias-mm X       inject a calibration plane bias in mm (default 0)\n"
        << "  --noise X         laser-line pixel noise sigma (default 0.8)\n"
        << "  --leaf-mm X       voxel downsample leaf in mm (default 1.5)\n"
        << "  --offset-ms X     inject camera/platform time offset (default 0)\n"
        << "  --output FILE     output PLY (default scan.ply)\n"
        << "  --report FILE     Markdown error report (default scan-report.md)\n"
        << "  -h, --help        show this message\n\n"
        << "Note: stage 1 reconstructs single frames without motion compensation,\n"
        << "so --offset-ms is recorded in metadata but does NOT yet change RMSE.\n"
        << "Adding motion compensation is the stage-5 exercise.\n";
}

PointCloudPtr mergeClouds(const std::vector<PointCloudPtr>& clouds) {
    auto merged = std::make_shared<PointCloud>();
    if (clouds.empty()) return merged;
    merged->unit = clouds.front()->unit;
    merged->frameId = clouds.front()->frameId;
    for (const auto& c : clouds)
        merged->points.insert(merged->points.end(), c->points.begin(), c->points.end());
    return merged;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parseOptions(argc, argv);
    if (opt.help) { printUsage(); return 0; }

    SynthScene scene = makeDefaultScene();
    scene.config.frames = opt.frames;
    scene.config.noiseSigmaPx = opt.noisePx;
    if (opt.biasMm != 0.0) scene = withPlaneBias(scene, opt.biasMm);
    if (opt.offsetMs != 0)
        scene = withTimeOffset(scene, std::chrono::milliseconds(opt.offsetMs));

    // Wire the same contracts the GUI uses.
    ReconstructionChain chain;
    chain.setReconstructor(std::make_shared<LaserReconstructor>());
    chain.addOperator(std::make_shared<VoxelFilterOperator>(opt.leafMm));

    const CancellationToken token;
    const std::vector<Frame> frames = generateFrames(scene, token);
    std::vector<PointCloudPtr> clouds;
    clouds.reserve(frames.size());

    for (const Frame& frame : frames) {
        auto result = chain.run(frame, scene.truth, token);
        if (!result) {
            std::cerr << "frame " << frame.sequence << ": "
                      << result.error().toString() << '\n';
            return 1;
        }
        clouds.push_back(result.value());
    }

    const PointCloudPtr merged = mergeClouds(clouds);
    auto evaluation = evaluateAgainstPlane(*merged, scene.truthPlane);
    if (!evaluation) {
        std::cerr << evaluation.error().toString() << '\n';
        return 1;
    }
    const ErrorReport& report = evaluation.value();

    const PointCloudPtr truthCloud = generateGroundTruthCloud(scene);
    auto cloudEvaluation = evaluateAgainstCloud(*merged, *truthCloud);
    if (!cloudEvaluation) {
        std::cerr << cloudEvaluation.error().toString() << '\n';
        return 1;
    }
    const ErrorReport& cloudReport = cloudEvaluation.value();

    if (const auto status = writePly(opt.output, *merged); !status) {
        std::cerr << status.error().toString() << '\n';
        return 1;
    }

    std::ofstream md(opt.report);
    md << "# ScanForge 标定与误差归因报告\n\n"
       << "| 项目 | 值 |\n|---|---|\n"
       << "| 采集帧数 | " << frames.size() << " |\n"
       << "| 重建点数（降采样后） | " << report.points << " |\n"
       << "| 注入光平面偏置 | " << opt.biasMm << " mm |\n"
       << "| 注入时间偏移 | " << opt.offsetMs << " ms |\n"
       << "| 像素噪声 σ | " << opt.noisePx << " px |\n"
       << "| 真值平面 | a=" << scene.truthPlane.a << " b=" << scene.truthPlane.b
       << " c=" << scene.truthPlane.c << " d=" << scene.truthPlane.d << " |\n"
       << "| 拟合平面 | z = " << report.fitted.a << "·x + "
       << report.fitted.b << "·y + " << report.fitted.c << " |\n"
       << "| 点到真值平面 RMSE | " << report.rmseMm << " mm |\n"
       << "| 点到真值平面平均绝对误差 | " << report.meanAbsMm << " mm |\n"
       << "| 点到真值平面最大绝对误差 | " << report.maxAbsMm << " mm |\n"
       << "| 点云最近邻 RMSE | " << cloudReport.rmseMm << " mm |\n"
       << "| 点云最近邻最大误差 | " << cloudReport.maxAbsMm << " mm |\n\n"
       << "## 误差归因\n\n"
       << "**平面距离指标**：" << report.attribution << "\n\n"
       << "**点云最近邻指标**：" << cloudReport.attribution << "\n\n"
       << "> 几何事实：三角化的交点是「射线 ∩ 标定平面」，因此无论像素噪声多大，"
       << "重建点都严格落在标定平面上。所以平面距离指标抓的是标定偏差，"
       << "噪声必须靠最近邻指标观察。两类指标分开，才能做分层归因。\n\n"
       << "## 分层排查顺序\n\n"
       << "1. 曝光与光源：条纹峰值是否饱和、是否有拖影\n"
       << "2. 触发与同步：帧时间戳基准、时间偏移\n"
       << "3. 标定参数：内参、畸变、光平面 d、外参\n"
       << "4. 配准初值与运动平台：振动、温漂、来料变化\n\n"
       << "> 数据为合成真值，不含任何真实设备参数或客户数据。\n";
    md.close();

    std::cout << "frames=" << frames.size()
              << " points=" << report.points
              << " plane_rmse(mm)=" << report.rmseMm
              << " cloud_rmse(mm)=" << cloudReport.rmseMm
              << " -> " << std::filesystem::absolute(opt.output).string() << '\n'
              << "report -> " << std::filesystem::absolute(opt.report).string() << '\n';
    return 0;
}
