cmake_minimum_required(VERSION 3.13)
include(ExternalProject)

if (${MTRACK_WASM})
else()
    # set(LIBBACKTRACE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR})
    set(LIBBACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
    # set(LIBBACKTRACE_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace_gen)

    set(BACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
    set(BACKTRACE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libbacktrace)
    if (${CMAKE_CURRENT_BINARY_DIR} STREQUAL ${CMAKE_CURRENT_LIST_DIR})
        set(BACKTRACE_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/install/libbacktrace)
    endif ()

    set(BACKTRACE_EXTERNAL_LIB ${BACKTRACE_BINARY_DIR}/prefix/lib/libbacktrace.a)
    set(BACKTRACE_CONFIGURE ${BACKTRACE_DIR}/configure --prefix ${BACKTRACE_BINARY_DIR}/prefix --disable-shared)
    if (${MTRACK_32})
        set(BACKTRACE_CONFIGURE ${BACKTRACE_CONFIGURE} --host=i686-pc-linux-gnu "CFLAGS=-m32 -fPIC" LDFLAGS=-m32)
    elseif (${MTRACK_ARM})
        set(BACKTRACE_CONFIGURE ${BACKTRACE_CONFIGURE} --host=arm-linux-gnueabi)
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

    add_subdirectory(asan_unwind)
endif()

# why do these libraries still have a cmake file from the 70s
set(ZLIB_DIR ${CMAKE_CURRENT_LIST_DIR}/zlib)
set(ZLIB_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/zlib)
set(ZLIB_CONFIGURE ${ZLIB_DIR}/configure --prefix=${ZLIB_BINARY_DIR}/prefix --static)
set(ZLIB_EXTERNAL_LIB ${ZLIB_BINARY_DIR}/prefix/lib/libz.a)
if (${MTRACK_32})
    set(ZLIB_CONFIGURE ${CMAKE_COMMAND} -E env CFLAGS=-m32 ${ZLIB_CONFIGURE})
else ()
    if("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "aarch64")
        set(ZLIB_CONFIGURE ${ZLIB_CONFIGURE})
    else()
        set(ZLIB_CONFIGURE ${ZLIB_CONFIGURE} --64)
    endif()
endif()

ExternalProject_Add(zlib_external
    CONFIGURE_COMMAND ${ZLIB_CONFIGURE}
    SOURCE_DIR ${ZLIB_DIR}
    BINARY_DIR ${ZLIB_BINARY_DIR}
    INSTALL_DIR ${ZLIB_BINARY_DIR}
    BUILD_BYPRODUCTS ${ZLIB_EXTERNAL_LIB}
    )

file(MAKE_DIRECTORY ${ZLIB_BINARY_DIR}/prefix/include)

add_library(zlib_static STATIC IMPORTED GLOBAL)
add_dependencies(zlib_static zlib_external)
target_include_directories(zlib_static INTERFACE ${ZLIB_BINARY_DIR}/prefix/include)
set_target_properties(zlib_static PROPERTIES IMPORTED_LOCATION ${ZLIB_EXTERNAL_LIB})
add_library(zlib::static ALIAS zlib_static)

add_subdirectory(fmt)
