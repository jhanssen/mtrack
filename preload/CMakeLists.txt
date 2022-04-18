set(SOURCES
    Hooks.cpp
    Recorder.cpp
    Stack.cpp
    )

add_library(mtrack_preload SHARED ${SOURCES})
target_compile_features(mtrack_preload PRIVATE cxx_std_20)
target_link_libraries(mtrack_preload PRIVATE pthread dl unwind::shared)
