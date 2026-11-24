# Qt plugin contract + plugin loader. This layer is the FIRST place where Qt
# appears; nothing below core/ is allowed to depend on it.
add_library(scanforge_device STATIC
    include/scanforge/IDevice.hpp
    include/scanforge/DeviceManager.hpp
    src/DeviceManager.cpp
)
add_library(sf::device ALIAS scanforge_device)

target_include_directories(scanforge_device
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(scanforge_device PUBLIC sf::core Qt6::Widgets)

add_subdirectory(mock_camera)
add_subdirectory(hik_camera)
