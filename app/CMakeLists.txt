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

if(WIN32 AND VCPKG_MANIFEST_MODE AND TARGET Qt6::QWindowsIntegrationPlugin)
    # vcpkg copies linked DLLs, but Qt loads its platform plugin at runtime.
    add_custom_command(TARGET scanforge_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:scanforge_app>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
            "$<TARGET_FILE_DIR:scanforge_app>/platforms/"
        VERBATIM)
endif()
