cmake_minimum_required(VERSION 3.13)
set(SOURCES
    main.cpp
    FileEmitter.cpp
    Logger.cpp
    Module.cpp
    Parser.cpp
    ResolverThread.cpp
    )

add_executable(mtrack_parser ${SOURCES})
set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-Wall -g")
target_compile_features(mtrack_parser PRIVATE cxx_std_20)
target_include_directories(mtrack_parser PRIVATE ${MTRACK_BASE_DIR})
target_link_libraries(mtrack_parser PRIVATE backtrace::static fmt::fmt pthread)
