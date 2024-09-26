set(SOURCES
    MallocSample.cpp
    )

add_executable(malloc_sample ${SOURCES})
target_compile_features(malloc_sample PRIVATE cxx_std_20)
set_target_properties(malloc_sample PROPERTIES OUTPUT_NAME "malloc_sample.js")
target_compile_options(malloc_sample PRIVATE -g3)
target_link_options(malloc_sample PRIVATE --bind -sWASM=1 -sENVIRONMENT=shell -sSINGLE_FILE -sDEMANGLE_SUPPORT=1 -sUSE_OFFSET_CONVERTER --js-library ../../wasm/library_mtrack.js -gseparate-dwarf=../malloc_sample.js.dwo)
target_link_libraries(malloc_sample mtrack_wasm)
add_dependencies(malloc_sample mtrack_wasm_js)
