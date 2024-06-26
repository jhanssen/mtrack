cmake_minimum_required(VERSION 3.13)
project(mtrack)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

string(REGEX REPLACE "-std=[^ ]*" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "")
string(REPLACE "-fno-rtti" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "")

set(MTRACK_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

if (${MTRACK_32})
    set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -m32")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -m32")
endif ()

if (${MTRACK_ASAN})
    set(CMAKE_C_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=address")
    set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif ()

add_subdirectory(3rdparty)
add_subdirectory(parser)
add_subdirectory(preload)
add_subdirectory(samples)
add_subdirectory(visualizer)
