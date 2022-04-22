cmake_minimum_required(VERSION 3.13)
set(SOURCES
    main.cpp
    Module.cpp
    Parser.cpp
    )

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-O3")

add_executable(mtrack_parser ${SOURCES})
target_compile_features(mtrack_parser PRIVATE cxx_std_20)
target_include_directories(mtrack_parser PRIVATE ${MTRACK_BASE_DIR})
target_link_libraries(mtrack_parser PRIVATE backtrace::static nlohmann_json::nlohmann_json)
