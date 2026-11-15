#pragma once

// Minimal ASCII PLY reader/writer. Only what the pipeline needs: x y z intensity.
// Reference: PCL pcl/io/ply_io.h (BSD-3), Open3D io/PointCloudIO.cpp (MIT),
// PDAL io/PlyReader.cpp (BSD) for header parsing conventions.

#include "Types.hpp"
#include "Result.hpp"

#include <string>

namespace sf {

[[nodiscard]] Result<PointCloudPtr> readPly(const std::string& path);
[[nodiscard]] Status                writePly(const std::string& path,
                                             const PointCloud& cloud);

} // namespace sf
