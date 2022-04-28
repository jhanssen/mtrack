set(SOURCES
    Tracker.cpp
    )

add_executable(tracker_sample ${SOURCES})
target_link_libraries(tracker_sample fmt::fmt)
target_compile_features(tracker_sample PRIVATE cxx_std_20)
