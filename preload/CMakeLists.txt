set(SOURCES
    Hooks.cpp
    Recorder.cpp
    Stack.cpp
    )

add_library(mtrack_preload SHARED ${SOURCES})
target_compile_features(mtrack_preload PRIVATE cxx_std_20)
target_include_directories(mtrack_preload PRIVATE ${MTRACK_INCLUDE_DIR})
target_link_libraries(mtrack_preload PRIVATE pthread dl unwind::shared backtrace::shared)

if (MTRACK_HAS_USER_REGS_STRUCT)
    target_compile_definitions(mtrack_preload PRIVATE -DMTRACK_HAS_USER_REGS_STRUCT)
endif ()
