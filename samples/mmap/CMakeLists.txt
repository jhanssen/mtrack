set(SOURCES
    MmapSample.cpp
    )

add_executable(mmap_sample ${SOURCES})
target_link_libraries(mmap_sample mtrack_preload)
target_compile_features(mmap_sample PRIVATE cxx_std_20)
