cmake_minimum_required(VERSION 3.13)
project(mtrack)

set(MTRACK_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(3rdparty)
add_subdirectory(parser)
add_subdirectory(preload)
add_subdirectory(samples)
