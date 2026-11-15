# One shared warning profile for every target. Keeping this in a single
# INTERFACE library avoids copy-pasting flags into each subdirectory.
add_library(sf_warnings INTERFACE)

if(MSVC)
    # /utf-8 is MANDATORY here: MSVC otherwise treats UTF-8 source files as
    # the local code page (936 on zh-CN Windows) and breaks Chinese string
    # literals with C4819 / C2001 "newline in constant".
    target_compile_options(sf_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
    target_compile_definitions(sf_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(sf_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor)
endif()
