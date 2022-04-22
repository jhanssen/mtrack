cmake_minimum_required(VERSION 3.13)
include(ExternalProject)

set(UNWIND_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libunwind)
if (${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
    set(UNWIND_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/install/libunwind)
endif ()

set(UNWIND_CONFIGURE ${UNWIND_BINARY_DIR}/configure --prefix ${UNWIND_BINARY_DIR}/prefix)
if (${MTRACK_32})
    set(UNWIND_CONFIGURE ${UNWIND_CONFIGURE} "CFLAGS=-m32" LDFLAGS=-m32 --target x86)
    set(UNWIND_EXTERNAL_LIB ${UNWIND_BINARY_DIR}/prefix/lib/libunwind-x86.so)
else ()
    set(UNWIND_EXTERNAL_LIB ${UNWIND_BINARY_DIR}/prefix/lib/libunwind.so)
endif ()

ExternalProject_Add(unwind_external
    URL https://download.savannah.nongnu.org/releases/libunwind/libunwind-1.6.2.tar.gz
    URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
    CONFIGURE_COMMAND ${UNWIND_CONFIGURE}
    DOWNLOAD_DIR ${UNWIND_BINARY_DIR}
    SOURCE_DIR ${UNWIND_BINARY_DIR}
    BINARY_DIR ${UNWIND_BINARY_DIR}
    INSTALL_DIR ${UNWIND_BINARY_DIR}
    BUILD_BYPRODUCTS ${UNWIND_EXTERNAL_LIB}
    )

# patch libbacktrace
find_program(PATCH
    NAMES patch
    PATH_SUFFIXES usr/bin
    )
if (NOT PATCH)
    message(FATAL_ERROR "No patch program")
endif()
find_program(SHELL
    NAMES sh
    PATH_SUFFIXES bin
    )
if (NOT SHELL)
    message(FATAL_ERROR "No shell program")
endif()

file(MAKE_DIRECTORY ${UNWIND_BINARY_DIR}/include)

add_library(unwind_shared SHARED IMPORTED GLOBAL)
add_dependencies(unwind_shared unwind_external)
target_include_directories(unwind_shared INTERFACE ${UNWIND_BINARY_DIR}/include)
set_target_properties(unwind_shared PROPERTIES IMPORTED_LOCATION ${UNWIND_EXTERNAL_LIB})
add_library(unwind::shared ALIAS unwind_shared)

set(LIBBACKTRACE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(LIBBACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
set(LIBBACKTRACE_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace_gen)

# this needs a revisit
file(MD5 ${LIBBACKTRACE_DIR}/internal.h INTERNAL_MD5)
if (NOT ${INTERNAL_MD5} STREQUAL "d9df3726dddb9356f78b10af5d1942c5")
    execute_process(
        COMMAND ${SHELL} -c "${PATCH} -p1 < ../libbacktrace-patch.diff"
        WORKING_DIRECTORY ${LIBBACKTRACE_DIR}
        )
endif()

set(LIBBACKTRACE_SOURCES
    ${LIBBACKTRACE_DIR}/atomic.c
    ${LIBBACKTRACE_DIR}/dwarf.c
    ${LIBBACKTRACE_DIR}/fileline.c
    ${LIBBACKTRACE_DIR}/posix.c
    ${LIBBACKTRACE_DIR}/print.c
    ${LIBBACKTRACE_DIR}/sort.c
    ${LIBBACKTRACE_DIR}/state.c
    ${LIBBACKTRACE_DIR}/backtrace.c
    ${LIBBACKTRACE_DIR}/simple.c
    ${LIBBACKTRACE_DIR}/elf.c
    ${LIBBACKTRACE_DIR}/mmapio.c
    ${LIBBACKTRACE_DIR}/mmap.c)

add_library(backtrace_static OBJECT ${LIBBACKTRACE_SOURCES})
target_include_directories(backtrace_static PUBLIC ${LIBBACKTRACE_INCLUDE_DIR})
target_include_directories(backtrace_static PRIVATE ${LIBBACKTRACE_GEN_DIR})
add_library(backtrace::static ALIAS backtrace_static)
add_library(backtrace_shared SHARED ${LIBBACKTRACE_SOURCES})
target_include_directories(backtrace_shared PUBLIC ${LIBBACKTRACE_INCLUDE_DIR})
target_include_directories(backtrace_shared PRIVATE ${LIBBACKTRACE_GEN_DIR})
add_library(backtrace::shared ALIAS backtrace_shared)

add_subdirectory(json)
