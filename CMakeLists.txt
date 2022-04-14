cmake_minimum_required(VERSION 3.13)
project(mtrack)

include(ExternalProject)
include(CheckCSourceCompiles)

set(MTRACK_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)


add_subdirectory(3rdparty)
add_subdirectory(preload)
add_subdirectory(samples)
