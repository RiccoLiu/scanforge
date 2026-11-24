# A real Qt plugin target (shared library) so that QPluginLoader can be
# exercised without any hardware. Output goes to build/plugins/.
add_library(mock_camera SHARED
    MockCamera.hpp
    MockCamera.cpp
    MockCamera.json
)
target_link_libraries(mock_camera PRIVATE sf::device sf::core Qt6::Widgets)

set_target_properties(mock_camera PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>/plugins"
    PREFIX "")

add_library(sf::mock_camera ALIAS mock_camera)
