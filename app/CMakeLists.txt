add_executable(scanforge_app
    src/main.cpp
    src/MainWindow.hpp
    src/MainWindow.cpp
    src/Docks.hpp
    src/Docks.cpp
    src/PointCloudView.hpp
    src/PointCloudView.cpp
    src/resources.qrc
)
target_include_directories(scanforge_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(scanforge_app PRIVATE
    sf::core sf::device sf::pipeline
    Qt6::Widgets Qt6::OpenGLWidgets)
add_dependencies(scanforge_app mock_camera)
set_target_properties(scanforge_app PROPERTIES OUTPUT_NAME ScanForge)
