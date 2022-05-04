cmake_minimum_required(VERSION 3.13)

execute_process(COMMAND npm install WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})

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
    OUTPUT ${CMAKE_CURRENT_LIST_DIR}/index.js ${CMAKE_CURRENT_LIST_DIR}/index.js.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS ${SOURCES}
    COMMENT "Building webpage"
    COMMAND npm run build)

add_custom_target(graph ALL DEPENDS ${CMAKE_CURRENT_LIST_DIR}/index.js ${CMAKE_CURRENT_LIST_DIR}/index.js.map)
