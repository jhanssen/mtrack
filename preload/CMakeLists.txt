cmake_minimum_required(VERSION 3.13)

set(SOURCES
    Preload.cpp
    Stack.cpp
    )

if (${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    list(APPEND SOURCES Stack_x86_64.S)
    enable_language(ASM)
endif()

set_source_files_properties(${SOURCES} PROPERTIES COMPILE_FLAGS "-fno-omit-frame-pointer -Wall")

add_library(mtrack_preload SHARED ${SOURCES})
target_compile_features(mtrack_preload PRIVATE cxx_std_20)
target_include_directories(mtrack_preload PRIVATE ${MTRACK_BASE_DIR})
target_include_directories(mtrack_preload INTERFACE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(mtrack_preload PRIVATE pthread dl unwind::static)
