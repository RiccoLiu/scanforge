# core/ is deliberately Qt-free. No file under core/ may include a Qt header.
# It must be buildable and testable on a machine with only a C++17 compiler.
add_library(scanforge_core STATIC
    src/Calibration.cpp
    src/PlyIO.cpp
    src/Synthetic.cpp
    src/Filters.cpp
)

add_library(sf::core ALIAS scanforge_core)

target_include_directories(scanforge_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(scanforge_core PUBLIC cxx_std_17)
target_link_libraries(scanforge_core PUBLIC sf_warnings)
