#include "scanforge/PlyIO.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace sf {
namespace {

struct PlyLayout {
    std::size_t vertexCount = 0;
    int xIdx = -1, yIdx = -1, zIdx = -1, iIdx = -1;
    std::size_t propertyCount = 0;
    LengthUnit  unit = LengthUnit::Millimeter;
    std::string frameId = "scanner";
};

Result<PlyLayout> parseHeader(std::istream& in) {
    PlyLayout layout;
    std::string line;
    if (!std::getline(in, line) || line.rfind("ply", 0) != 0) {
        return Result<PlyLayout>::failure(makeError(
            ErrorCode::UnsupportedFormat, "not a PLY file", "PlyIO::read"));
    }
    bool ascii = false;
    bool inVertex = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "end_header") break;
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if (token == "format") {
            std::string format, version;
            iss >> format >> version;
            ascii = (format == "ascii");
        } else if (token == "element") {
            std::string name; std::size_t count = 0;
            iss >> name >> count;
            inVertex = (name == "vertex");
            if (inVertex) layout.vertexCount = count;
        } else if (token == "comment") {
            // Unit and coordinate frame MUST travel with the data. Mixing an
            // STL file in mm with a scanner export in m is the classic
            // CloudCompare support incident, so we persist both in the header.
            std::string keyValue;
            iss >> keyValue;
            const auto equal = keyValue.find('=');
            if (equal != std::string::npos) {
                const std::string key = keyValue.substr(0, equal);
                const std::string val = keyValue.substr(equal + 1);
                if (key == "unit") layout.unit =
                    (val == "m") ? LengthUnit::Meter : LengthUnit::Millimeter;
                else if (key == "frame_id") layout.frameId = val;
            }
        } else if (token == "property" && inVertex) {
            std::string type, name;
            iss >> type >> name;
            const int idx = static_cast<int>(layout.propertyCount++);
            if      (name == "x")         layout.xIdx = idx;
            else if (name == "y")         layout.yIdx = idx;
            else if (name == "z")         layout.zIdx = idx;
            else if (name == "intensity") layout.iIdx = idx;
        }
    }
    if (!ascii || layout.xIdx < 0 || layout.yIdx < 0 || layout.zIdx < 0) {
        return Result<PlyLayout>::failure(makeError(
            ErrorCode::UnsupportedFormat,
            "only ASCII PLY with x/y/z properties is supported", "PlyIO::read"));
    }
    return Result<PlyLayout>::success(layout);
}

} // namespace

Result<PointCloudPtr> readPly(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<PointCloudPtr>::failure(makeError(
            ErrorCode::IoError, "cannot open " + path, "PlyIO::read"));
    }
    const auto header = parseHeader(in);
    if (!header) {
        return Result<PointCloudPtr>::failure(header.error());
    }
    const PlyLayout& layout = header.value();
    auto cloud = std::make_shared<PointCloud>();
    cloud->unit = layout.unit;
    cloud->frameId = layout.frameId;
    cloud->points.reserve(layout.vertexCount);

    std::string line;
    for (std::size_t row = 0; row < layout.vertexCount; ++row) {
        if (!std::getline(in, line)) {
            return Result<PointCloudPtr>::failure(makeError(
                ErrorCode::IoError, "truncated vertex list", "PlyIO::read"));
        }
        std::istringstream iss(line);
        std::vector<double> values;
        double value = 0.0;
        while (iss >> value) values.push_back(value);
        if (values.size() < layout.propertyCount ||
            layout.xIdx >= static_cast<int>(values.size()) ||
            layout.zIdx >= static_cast<int>(values.size())) {
            return Result<PointCloudPtr>::failure(makeError(
                ErrorCode::UnsupportedFormat, "malformed vertex row", "PlyIO::read"));
        }
        PointXYZI p{};
        p.x = static_cast<float>(values[static_cast<std::size_t>(layout.xIdx)]);
        p.y = static_cast<float>(values[static_cast<std::size_t>(layout.yIdx)]);
        p.z = static_cast<float>(values[static_cast<std::size_t>(layout.zIdx)]);
        if (layout.iIdx >= 0 && layout.iIdx < static_cast<int>(values.size()))
            p.intensity = static_cast<float>(values[static_cast<std::size_t>(layout.iIdx)]);
        cloud->points.push_back(p);
    }
    return Result<PointCloudPtr>::success(std::move(cloud));
}

Status writePly(const std::string& path, const PointCloud& cloud) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return Status::failure(makeError(
            ErrorCode::IoError, "cannot create " + path, "PlyIO::write"));
    }
    out << "ply\n"
        << "format ascii 1.0\n"
        << "comment generated by ScanForge\n"
        << "comment frame_id=" << cloud.frameId << "\n"
        << "comment unit=" << (cloud.unit == LengthUnit::Meter ? "m" : "mm") << "\n"
        << "element vertex " << cloud.points.size() << "\n"
        << "property float x\n"
        << "property float y\n"
        << "property float z\n"
        << "property float intensity\n"
        << "end_header\n";
    out.setf(std::ios::fixed);
    for (const auto& p : cloud.points) {
        out << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.intensity << '\n';
    }
    if (!out) {
        return Status::failure(makeError(
            ErrorCode::IoError, "write failed for " + path, "PlyIO::write"));
    }
    return ok();
}

} // namespace sf
