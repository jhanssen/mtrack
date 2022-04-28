cmake_minimum_required(VERSION 3.13)
include(ExternalProject)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(UNWIND_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libunwind)
    if (${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
        set(UNWIND_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/install/libunwind)
    endif ()

    set(UNWIND_EXTERNAL_LIB ${UNWIND_BINARY_DIR}/prefix/lib/libunwind.a)
    set(UNWIND_CONFIGURE ${UNWIND_BINARY_DIR}/configure --prefix ${UNWIND_BINARY_DIR}/prefix --disable-tests --disable-documentation --disable-coredump --disable-ptrace --disable-setjmp --disable-debug-frame --disable-minidebuginfo --disable-per-thread-cache --disable-cxx-exceptions --disable-debug --disable-weak-backtrace --disable-unwind-header --disable-shared)
    if (${MTRACK_32})
        set(UNWIND_CONFIGURE ${UNWIND_CONFIGURE} --host=i686-pc-linux-gnu "CFLAGS=-m32 -fPIC" LDFLAGS=-m32)
    else ()
        set(UNWIND_CONFIGURE ${UNWIND_CONFIGURE} CFLAGS=-fPIC)
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

    file(MAKE_DIRECTORY ${UNWIND_BINARY_DIR}/include)

    add_library(unwind_static STATIC IMPORTED GLOBAL)
    add_dependencies(unwind_static unwind_external)
    target_include_directories(unwind_static INTERFACE ${UNWIND_BINARY_DIR}/include)
    set_target_properties(unwind_static PROPERTIES IMPORTED_LOCATION ${UNWIND_EXTERNAL_LIB})
    add_library(unwind::static ALIAS unwind_static)
endif ()

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

# set(LIBBACKTRACE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(LIBBACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
# set(LIBBACKTRACE_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace_gen)

# this needs a revisit
file(MD5 ${LIBBACKTRACE_DIR}/internal.h INTERNAL_MD5)
if (NOT ${INTERNAL_MD5} STREQUAL "d9df3726dddb9356f78b10af5d1942c5")
    execute_process(
        COMMAND ${SHELL} -c "${PATCH} -p1 < ../libbacktrace-patch.diff"
        WORKING_DIRECTORY ${LIBBACKTRACE_DIR}
        )
endif()

set(BACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
set(BACKTRACE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libbacktrace)
if (${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
    set(BACKTRACE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/install/libbacktrace)
endif ()

set(BACKTRACE_EXTERNAL_LIB ${BACKTRACE_BINARY_DIR}/prefix/lib/libbacktrace.a)
set(BACKTRACE_CONFIGURE ${BACKTRACE_DIR}/configure --prefix ${BACKTRACE_BINARY_DIR}/prefix --disable-shared)
if (${MTRACK_32})
    set(BACKTRACE_CONFIGURE ${BACKTRACE_CONFIGURE} --host=i686-pc-linux-gnu "CFLAGS=-m32 -fPIC" LDFLAGS=-m32)
else ()
    set(BACKTRACE_CONFIGURE ${BACKTRACE_CONFIGURE} CFLAGS=-fPIC)
endif ()

ExternalProject_Add(backtrace_external
    CONFIGURE_COMMAND ${BACKTRACE_CONFIGURE}
    SOURCE_DIR ${BACKTRACE_DIR}
    BINARY_DIR ${BACKTRACE_BINARY_DIR}
    INSTALL_DIR ${BACKTRACE_BINARY_DIR}
    BUILD_BYPRODUCTS ${BACKTRACE_EXTERNAL_LIB}
    )

file(MAKE_DIRECTORY ${BACKTRACE_BINARY_DIR}/include)

add_library(backtrace_static STATIC IMPORTED GLOBAL)
add_dependencies(backtrace_static backtrace_external)
target_include_directories(backtrace_static INTERFACE ${BACKTRACE_DIR})
set_target_properties(backtrace_static PROPERTIES IMPORTED_LOCATION ${BACKTRACE_EXTERNAL_LIB})
add_library(backtrace::static ALIAS backtrace_static)
