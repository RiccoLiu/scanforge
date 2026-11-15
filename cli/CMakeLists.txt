# Headless batch entry point. Deliberately links ONLY sf::core: if this target
# ever needs Qt, the core/ layer has been contaminated.
add_executable(scanforge_cli src/main.cpp)
target_link_libraries(scanforge_cli PRIVATE sf::core sf_warnings)
set_target_properties(scanforge_cli PROPERTIES OUTPUT_NAME scanforge-cli)
