#include "mini_test.hpp"

#include "scanforge/Calibration.hpp"
#include "scanforge/Contracts.hpp"
#include "scanforge/Filters.hpp"
#include "scanforge/PlyIO.hpp"
#include "scanforge/Result.hpp"
#include "scanforge/Synthetic.hpp"
#include "scanforge/Types.hpp"

#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace sf;

namespace {

std::shared_ptr<PointCloud> reconstructAll(const SynthScene& scene, bool withVoxel) {
    ReconstructionChain chain;
    chain.setReconstructor(std::make_shared<LaserReconstructor>());
    if (withVoxel) chain.addOperator(std::make_shared<VoxelFilterOperator>(1.5));

    auto merged = std::make_shared<PointCloud>();
    merged->unit = scene.truth.unit;
    merged->frameId = scene.truth.coordinateFrame;
    const CancellationToken token;
    for (const Frame& frame : generateFrames(scene, token)) {
        auto result = chain.run(frame, scene.truth, token);
        if (!result) return nullptr;
        for (const auto& p : result.value()->points) merged->points.push_back(p);
    }
    return merged;
}

} // namespace

SF_TEST("Result separates the success and error channels") {
    auto good = Result<int>::success(42);
    SF_CHECK(good.hasValue());
    SF_CHECK(good.value() == 42);

    auto bad = Result<int>::failure(makeError(ErrorCode::IoError, "disk full", "test"));
    SF_CHECK(!bad.hasValue());
    SF_CHECK(bad.error().code == ErrorCode::IoError);
    SF_CHECK(bad.error().toString().find("disk full") != std::string::npos);
}

SF_TEST("Image validity is derived from the actual buffer size") {
    Image image;
    image.width = 4; image.height = 3; image.format = PixelFormat::Mono8;
    SF_CHECK(!image.valid());
    image.data.assign(12, 0);
    SF_CHECK(image.valid());
    image.format = PixelFormat::RGB8;
    SF_CHECK(!image.valid());
}

SF_TEST("Stage 1 reconstructs Mono16 and RGB8 frames like Mono8") {
    SynthScene scene = makeDefaultScene();
    const CancellationToken token;
    const auto frames = generateFrames(scene, token);
    SF_CHECK(!frames.empty());
    const Frame& mono = frames.front();
    SF_CHECK(mono.image.format == PixelFormat::Mono8);
    const auto base = triangulateLaserLine(mono, scene.truth);
    SF_CHECK(base.hasValue());

    Frame mono16 = mono;
    mono16.image.format = PixelFormat::Mono16;
    mono16.image.data.clear();
    for (std::uint8_t v : mono.image.data) {
        const auto word = static_cast<std::uint16_t>(v) << 8;   // little endian
        mono16.image.data.push_back(static_cast<std::uint8_t>(word & 0xff));
        mono16.image.data.push_back(static_cast<std::uint8_t>(word >> 8));
    }
    const auto r16 = triangulateLaserLine(mono16, scene.truth);
    SF_CHECK(r16.hasValue());
    SF_CHECK(r16.value()->size() == base.value()->size());

    Frame rgb = mono;
    rgb.image.format = PixelFormat::RGB8;
    rgb.image.data.clear();
    for (std::uint8_t v : mono.image.data) {
        rgb.image.data.push_back(v);
        rgb.image.data.push_back(v);
        rgb.image.data.push_back(v);
    }
    const auto rrgb = triangulateLaserLine(rgb, scene.truth);
    SF_CHECK(rrgb.hasValue());
    SF_CHECK(rrgb.value()->size() == base.value()->size());
}

SF_TEST("PLY round-trip preserves geometry, intensity and metadata") {
    PointCloud cloud;
    cloud.unit = LengthUnit::Millimeter;
    cloud.frameId = "camera";
    cloud.points.push_back(PointXYZI{1.0f, 2.0f, 3.0f, 0.5f});
    cloud.points.push_back(PointXYZI{-4.0f, 5.5f, 300.25f, 1.0f});

    const auto path = (std::filesystem::temp_directory_path() / "scanforge_test.ply").string();
    const auto write = writePly(path, cloud);
    SF_CHECK(write.hasValue());

    auto read = readPly(path);
    SF_CHECK(read.hasValue());
    SF_CHECK(read.value()->size() == 2);
    SF_NEAR(read.value()->points[1].z, 300.25, 0.01);
    SF_NEAR(read.value()->points[0].intensity, 0.5, 0.001);
    SF_CHECK(read.value()->unit == LengthUnit::Millimeter);
    SF_CHECK(read.value()->frameId == "camera");
    std::filesystem::remove(path);
}

SF_TEST("Synthetic source injects dropped frames and time offset") {
    SynthScene scene = makeDefaultScene();
    scene.config.frames = 12;
    scene.config.dropEvery = 3;
    scene = withTimeOffset(scene, std::chrono::milliseconds(7));

    SyntheticSource source(scene);
    SF_CHECK(source.open("").hasValue());
    SF_CHECK(source.capabilities().supportsHardTrigger == false);
    const auto frames = generateFrames(scene);
    SF_CHECK(frames.size() == 8);   // 12 frames minus every 3rd
    SF_CHECK(frames.front().injectedTimeOffset == std::chrono::milliseconds(7));
    SF_CHECK(frames.front().timestamp >= std::chrono::milliseconds(7));
    SF_CHECK(frames.front().image.valid());
    source.close();
}

SF_TEST("Zero / negative / NaN sigma means 'noise off' and never asserts") {
    // Regression: sigma == 0 used to construct std::normal_distribution(0, 0),
    // which is UB and aborts in MSVC Debug (<random>:3489 _STL_ASSERT).
    // With a debugger attached it presents as a frozen program.
    SynthScene scene = makeDefaultScene();
    scene.config.frames = 4;
    const CancellationToken token;

    scene.config.noiseSigmaPx = 0.0;
    const auto zero = generateFrames(scene, token);
    SF_CHECK(zero.size() == 4);
    SF_CHECK(zero.front().image.valid());

    scene.config.noiseSigmaPx = -1.0;
    SF_CHECK(generateFrames(scene, token).size() == 4);

    scene.config.noiseSigmaPx = std::numeric_limits<double>::quiet_NaN();
    SF_CHECK(generateFrames(scene, token).size() == 4);

    // A positive sigma must still produce noise (and must be constructible).
    scene.config.noiseSigmaPx = 3.0;
    SF_CHECK(generateFrames(scene, token).size() == 4);
}

SF_TEST("Triangulated points lie on the CALIBRATION plane (geometric invariant)") {
    SynthScene scene = makeDefaultScene();
    scene.config.frames = 6;
    scene.config.noiseSigmaPx = 3.0;   // heavy noise must NOT move them off the plane
    const auto cloud = reconstructAll(scene, false);
    SF_CHECK(cloud != nullptr);
    SF_CHECK(cloud->size() > 1000);
    const auto report = evaluateAgainstPlane(*cloud, scene.truthPlane);
    SF_CHECK(report.hasValue());
    SF_CHECK(report.value().rmseMm < 0.01);
}

SF_TEST("A stale light-plane calibration is caught by the plane metric") {
    SynthScene biased = withPlaneBias(makeDefaultScene(), 2.0);
    biased.config.frames = 6;
    biased.config.noiseSigmaPx = 0.0;
    const auto cloud = reconstructAll(biased, false);
    SF_CHECK(cloud != nullptr);
    const auto report = evaluateAgainstPlane(*cloud, biased.truthPlane);
    SF_CHECK(report.hasValue());
    SF_CHECK(report.value().rmseMm > 1.5);
    SF_CHECK(report.value().rmseMm < 2.5);
}

SF_TEST("Pixel noise is caught by the cloud-to-cloud metric, not the plane metric") {
    SynthScene clean = makeDefaultScene();
    clean.config.frames = 6;
    clean.config.noiseSigmaPx = 0.0;
    SynthScene noisy = clean;
    noisy.config.noiseSigmaPx = 3.0;

    const auto cleanCloud = reconstructAll(clean, false);
    const auto noisyCloud = reconstructAll(noisy, false);
    SF_CHECK(cleanCloud != nullptr && noisyCloud != nullptr);
    const PointCloudPtr truth = generateGroundTruthCloud(clean);

    const auto cleanReport = evaluateAgainstCloud(*cleanCloud, *truth);
    const auto noisyReport = evaluateAgainstCloud(*noisyCloud, *truth);
    SF_CHECK(cleanReport.hasValue() && noisyReport.hasValue());
    SF_CHECK(cleanReport.value().rmseMm < 0.05);
    SF_CHECK(noisyReport.value().rmseMm > cleanReport.value().rmseMm + 0.2);
}

SF_TEST("Least-squares plane fit recovers the known plane parameters") {
    SynthScene scene = makeDefaultScene();
    scene.config.frames = 8;
    scene.config.noiseSigmaPx = 0.0;
    const auto cloud = reconstructAll(scene, false);
    SF_CHECK(cloud != nullptr);
    const auto fit = fitPlaneLeastSquares(*cloud);
    SF_CHECK(fit.hasValue());
    SF_NEAR(fit.value().a, scene.config.alpha, 0.01);
    SF_NEAR(fit.value().b, scene.config.beta, 0.01);
    SF_NEAR(fit.value().c, scene.config.z0, 0.5);
}

SF_TEST("Voxel downsampling reduces size but keeps the coordinate contract") {
    SynthScene scene = makeDefaultScene();
    scene.config.frames = 6;
    const auto full = reconstructAll(scene, false);
    const auto reduced = reconstructAll(scene, true);
    SF_CHECK(full != nullptr && reduced != nullptr);
    SF_CHECK(reduced->size() < full->size());
    SF_CHECK(reduced->unit == full->unit);
    SF_CHECK(reduced->frameId == full->frameId);
}

SF_TEST("Reprojection RMS is zero for exact correspondences") {
    SynthScene scene = makeDefaultScene();
    std::vector<Correspondence> samples;
    for (double z : {250.0, 300.0, 350.0}) {
        const double x = 10.0, y = -5.0;
        samples.push_back(Correspondence{
            x, y, z,
            scene.truth.camera.fx * (x / z) + scene.truth.camera.cx,
            scene.truth.camera.fy * (y / z) + scene.truth.camera.cy});
    }
    const auto rms = reprojectionRms(samples, scene.truth.camera);
    SF_CHECK(rms.hasValue());
    SF_CHECK(rms.value() < 1e-9);
}
