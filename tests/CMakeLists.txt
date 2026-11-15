add_executable(scanforge_tests
    test_core.cpp
    test_runtime.cpp
)

target_link_libraries(scanforge_tests PRIVATE sf::core sf_warnings)

if(SCANFORGE_USE_CATCH2)
    find_package(Catch2 3 REQUIRED)
    target_link_libraries(scanforge_tests PRIVATE Catch2::Catch2WithMain)
    target_compile_definitions(scanforge_tests PRIVATE SCANFORGE_USE_CATCH2)
else()
    # Zero-dependency harness: works offline, no FetchContent, no network.
    target_sources(scanforge_tests PRIVATE test_main.cpp)
endif()

add_test(NAME scanforge_unit COMMAND scanforge_tests)
