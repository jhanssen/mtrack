cmake_minimum_required(VERSION 3.13)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

if (${MTRACK_WASM})
    add_subdirectory(wasm)
else()
    add_subdirectory(malloc)
    add_subdirectory(mmap)
    add_subdirectory(tracker)
endif()
