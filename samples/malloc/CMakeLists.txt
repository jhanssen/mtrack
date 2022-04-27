set(SOURCES
    MallocSample.cpp
    )

add_executable(malloc_sample ${SOURCES})
target_link_libraries(malloc_sample mtrack_preload)
target_compile_features(malloc_sample PRIVATE cxx_std_20)
