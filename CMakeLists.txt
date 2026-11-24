cmake_minimum_required(VERSION 3.25)

project(ScanForge
    VERSION 0.1.0
    DESCRIPTION "Open-source reference implementation of an industrial 3D scanning host application"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global toolchain policy (Qt6 official build system is CMake + Ninja)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# AUTOMOC/AUTORCC/AUTOUIC are enabled ONLY when Qt was found, otherwise CMake
# emits "AUTOGEN: No valid Qt version found" warnings for every Qt-free target.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)

# ---------------------------------------------------------------------------
# Feature switches. The default configuration builds EVERYTHING that can be
# built on the current machine: core + cli + tests are Qt-free and always
# available; plugins/pipeline/app are skipped when Qt6 is not installed.
# ---------------------------------------------------------------------------
option(SCANFORGE_BUILD_APP   "Build the Qt6 desktop application" ON)
option(SCANFORGE_BUILD_CLI   "Build the headless batch CLI"      ON)
option(SCANFORGE_BUILD_TESTS "Build unit tests"                  ON)
option(SCANFORGE_USE_CATCH2  "Use Catch2 v3 instead of bundled mini harness" OFF)

include(cmake/CompilerWarnings.cmake)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory(core)

if(SCANFORGE_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(SCANFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SCANFORGE_BUILD_APP)
    find_package(Qt6 6.8 QUIET COMPONENTS Widgets OpenGLWidgets)
    if(Qt6_FOUND)
        message(STATUS "Qt6 ${Qt6_VERSION} found -> building plugins / pipeline / app")
        set(CMAKE_AUTOMOC ON)
        set(CMAKE_AUTORCC ON)
        set(CMAKE_AUTOUIC ON)
        add_subdirectory(plugins)
        add_subdirectory(pipeline)
        add_subdirectory(app)
    else()
        message(WARNING
            "Qt6 >= 6.8 (Widgets, OpenGLWidgets) not found.\n"
            "Skipping plugins/pipeline/app; core + cli + tests still build.\n"
            "Install Qt 6.8 LTS (or newer LTS) and re-run CMake.")
    endif()
endif()

message(STATUS "ScanForge ${PROJECT_VERSION} configuration complete")
