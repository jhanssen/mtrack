set(SOURCES
    main.cpp
    Module.cpp
    Parser.cpp
    )

add_executable(mtrack_parser ${SOURCES})
target_compile_features(mtrack_parser PRIVATE cxx_std_20)
target_include_directories(mtrack_parser PRIVATE ${MTRACK_BASE_DIR})
target_link_libraries(mtrack_parser PRIVATE backtrace::shared)
