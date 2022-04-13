cmake_minimum_required(VERSION 3.13)
project(mtrack)

include(ExternalProject)
include(CheckCSourceCompiles)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

if (MTRACK_HAS_USER_REGS_STRUCT)
    add_definitions(-DMTRACK_HAS_USER_REGS_STRUCT)
endif ()

ExternalProject_Add(libunwind
    URL https://download.savannah.nongnu.org/releases/libunwind/libunwind-1.6.2.tar.gz
    URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
    CONFIGURE_COMMAND ../libunwind/configure --prefix ${CMAKE_CURRENT_BINARY_DIR}/3rdparty/libunwind
    )

add_subdirectory(3rdparty)
add_subdirectory(preload)
add_subdirectory(samples)
