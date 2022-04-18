set(SOURCES
    main.cpp
    )

add_executable(mtrack_parser ${SOURCES})
target_compile_features(mtrack_parser PRIVATE cxx_std_20)
target_link_libraries(mtrack_parser PRIVATE backtrace::shared)
