cmake_minimum_required(VERSION 3.13)

set(SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/Assert.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Frame.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Graph-template.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Model2.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/PageFault2.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/ParseResult.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/RecordType.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Snapshot.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Stack.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/Until.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/webpage.ts)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/src)
foreach(SOURCE_FILE ${SOURCES})
    get_filename_component(FILENAME ${SOURCE_FILE} NAME)
    file(CREATE_LINK ${SOURCE_FILE} ${CMAKE_CURRENT_BINARY_DIR}/src/${FILENAME})
endforeach()

set(BUILD_FILES
    ${CMAKE_CURRENT_LIST_DIR}/rollup.config.ts
    ${CMAKE_CURRENT_LIST_DIR}/package.json
    ${CMAKE_CURRENT_LIST_DIR}/package-lock.json
    ${CMAKE_CURRENT_LIST_DIR}/.eslintrc
    ${CMAKE_CURRENT_LIST_DIR}/index.html
    ${CMAKE_CURRENT_LIST_DIR}/tsconfig.json)

foreach(BUILD_FILE ${BUILD_FILES})
    get_filename_component(FILENAME ${BUILD_FILE} NAME)
    file(CREATE_LINK ${BUILD_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${FILENAME})
endforeach()

execute_process(COMMAND npm install WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/index.js ${CMAKE_CURRENT_BINARY_DIR}/index.js.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${SOURCES}
    COMMENT "Building webpage"
    COMMAND npm run build)

add_custom_target(graph ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/index.js ${CMAKE_CURRENT_BINARY_DIR}/index.js.map)
