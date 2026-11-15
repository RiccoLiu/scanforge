# Three-lane runtime: GUI thread | acquisition QThread | processing QThread.
# All QObject workers are parentless so that moveToThread() is legal.
add_library(scanforge_pipeline STATIC
    include/scanforge/AcquisitionWorker.hpp
    include/scanforge/ProcessingWorker.hpp
    include/scanforge/PipelineController.hpp
    src/AcquisitionWorker.cpp
    src/ProcessingWorker.cpp
    src/PipelineController.cpp
)
add_library(sf::pipeline ALIAS scanforge_pipeline)

target_include_directories(scanforge_pipeline
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(scanforge_pipeline PUBLIC sf::core sf::device Qt6::Widgets)
