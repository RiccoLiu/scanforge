
set(MVS_LOCAL_INCLUDE   "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(MVS_LOCAL_LIB_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/lib/MvCameraControl.lib")

add_library(hik_camera SHARED
    HikCamera.h
    HikCamera.cpp
    HikCamera.json
    include/MvCamera.h
    include/MvCamera.cpp
)

target_include_directories(hik_camera PRIVATE
    "${MVS_LOCAL_INCLUDE}")
target_link_libraries(hik_camera PRIVATE sf::device sf::core Qt6::Widgets "${MVS_LOCAL_LIB_FILE}")

set_target_properties(hik_camera PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    PREFIX "")

add_library(sf::hik_camera ALIAS hik_camera)
