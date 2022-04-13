set(SOURCES
    MmapSample.cpp
    )

add_executable(mmap_sample ${SOURCES})
target_compile_features(mmap_sample PRIVATE cxx_std_20)
