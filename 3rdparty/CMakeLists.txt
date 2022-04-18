include(ExternalProject)

ExternalProject_Add(unwind_external
    URL https://download.savannah.nongnu.org/releases/libunwind/libunwind-1.6.2.tar.gz
    URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
    CONFIGURE_COMMAND ${CMAKE_CURRENT_LIST_DIR}/libunwind/configure --prefix ${CMAKE_CURRENT_BINARY_DIR}/libunwind
    DOWNLOAD_DIR ${CMAKE_CURRENT_LIST_DIR}/libunwind
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/libunwind
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libunwind-build
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/libunwind
    )

add_library(unwind_shared SHARED IMPORTED GLOBAL)
add_dependencies(unwind_shared unwind_external)
target_include_directories(unwind_shared INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/libunwind/include)
set_target_properties(unwind_shared PROPERTIES IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/libunwind/lib/libunwind.so)
add_library(unwind::shared ALIAS unwind_shared)

set(LIBBACKTRACE_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace)
set(LIBBACKTRACE_GEN_DIR ${CMAKE_CURRENT_LIST_DIR}/libbacktrace_gen)

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
target_include_directories(backtrace_static PUBLIC ${LIBBACKTRACE_DIR})
target_include_directories(backtrace_static PRIVATE ${LIBBACKTRACE_GEN_DIR})
add_library(backtrace::static ALIAS backtrace_static)
add_library(backtrace_shared SHARED ${LIBBACKTRACE_SOURCES})
target_include_directories(backtrace_shared PUBLIC ${LIBBACKTRACE_DIR})
target_include_directories(backtrace_shared PRIVATE ${LIBBACKTRACE_GEN_DIR})
add_library(backtrace::shared ALIAS backtrace_shared)
