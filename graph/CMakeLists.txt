cmake_minimum_required(VERSION 3.13)

# Create a configure file only if it's going to be different
macro(CONFIGURE_FILE_IF_DIFFERENT input output)
    set(has_atonly 0)
    set(extra_macro_args ${ARGN})
    list(LENGTH extra_macro_args num_extra_args)
    if (${num_extra_args} GREATER 0)
        list(GET extra_macro_args 0 optional_arg)
        if ("${optional_arg}" STREQUAL "@ONLY")
            set(has_atonly 1)
        endif()
    endif ()

    if (${has_atonly})
        configure_file("${input}" "${output}.gen" @ONLY)
    else()
        configure_file("${input}" "${output}.gen")
    endif()
    if(EXISTS "${output}")
        file(READ "${output}" cfif_output)
    else()
        set(cfif_output "")
    endif()
    file(READ "${output}.gen" cfif_output_gen)
    # file(REMOVE "${output}.gen")

    if(NOT "${cfif_output}" STREQUAL "${cfif_output_gen}")
        if (${has_atonly})
            configure_file("${input}" "${output}" @ONLY)
        else()
            configure_file("${input}" "${output}")
        endif()
    endif()
endmacro()

set(MTRACK_GRAPH_SOURCE ${CMAKE_CURRENT_LIST_DIR})
execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_CURRENT_LIST_DIR}/src ${CMAKE_CURRENT_BINARY_DIR}/src)
configure_file_if_different(${CMAKE_CURRENT_LIST_DIR}/index.html ${CMAKE_CURRENT_BINARY_DIR}/index.html)
configure_file_if_different(${CMAKE_CURRENT_LIST_DIR}/package.json.in ${CMAKE_CURRENT_BINARY_DIR}/package.json)
configure_file_if_different(${CMAKE_CURRENT_LIST_DIR}/tsconfig.json.in ${CMAKE_CURRENT_BINARY_DIR}/tsconfig.json)
configure_file_if_different(${CMAKE_CURRENT_LIST_DIR}/rollup.config.ts.in ${CMAKE_CURRENT_BINARY_DIR}/rollup.config.ts)
configure_file_if_different(${CMAKE_CURRENT_LIST_DIR}/.eslintrc.in ${CMAKE_CURRENT_BINARY_DIR}/.eslintrc)
execute_process(COMMAND npm install WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/Assert.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Graph-template.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Model2.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/PageFault2.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/ParseResult.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/RecordType.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Snapshot.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Stack.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Until.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/stacks.json
    ${CMAKE_CURRENT_LIST_DIR}/src/webpage.ts)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/index.js ${CMAKE_CURRENT_BINARY_DIR}/index.js.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${SOURCES}
    COMMENT "Building webpage"
    COMMAND npm run build)

add_custom_target(graph ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/index.js ${CMAKE_CURRENT_BINARY_DIR}/index.js.map)
