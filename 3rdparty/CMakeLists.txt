include(ExternalProject)

ExternalProject_Add(libunwind
    URL https://download.savannah.nongnu.org/releases/libunwind/libunwind-1.6.2.tar.gz
    URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
    CONFIGURE_COMMAND ../libunwind/configure --prefix ${CMAKE_CURRENT_BINARY_DIR}/libunwind
    )

set(THIRDPARTY_LIST_DIR ${CMAKE_CURRENT_LIST_DIR} PARENT_SCOPE)
set(THIRDPARTY_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
