cmake_minimum_required(VERSION 3.13)

set(SOURCES
    Wasm.cpp
    Stack.cpp
    )

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-fno-omit-frame-pointer -Wall")

add_library(mtrack_wasm STATIC ${SOURCES})
target_compile_features(mtrack_wasm PRIVATE cxx_std_20)
target_include_directories(mtrack_wasm PRIVATE ${MTRACK_BASE_DIR})
target_include_directories(mtrack_wasm INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_options(mtrack_wasm PRIVATE --bind --tracing -sWASM=1 -sENVIRONMENT=shell -sDEMANGLE_SUPPORT=1 -sUSE_OFFSET_CONVERTER)
target_compile_options(mtrack_wasm PRIVATE --tracing)

add_custom_target(mtrack_wasm_js ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/library_mtrack.js)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/library_mtrack.js DEPENDS ${CMAKE_SOURCE_DIR}/wasm/library_mtrack.js
                   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/wasm/library_mtrack.js ${CMAKE_CURRENT_BINARY_DIR}/library_mtrack.js)
