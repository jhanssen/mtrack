cmake_minimum_required(VERSION 3.13)

set(MTRACK_GRAPH ${CMAKE_CURRENT_LIST_DIR})
set(MTRACK_GRAPH_BUILD ${CMAKE_CURRENT_BINARY_DIR})
set(VISUALIZER_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/Assert.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/cli/cli.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/graph/Graph.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/graph/webpage.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/model/Frame.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/model/Model.ts
    ${CMAKE_CURRENT_LIST_DIR}/src/model/RecordType.ts)

configure_file(rollup.config.js.in ${CMAKE_CURRENT_BINARY_DIR}/rollup.config.js)
configure_file(package.json.in ${CMAKE_CURRENT_BINARY_DIR}/package.json)
configure_file(yarn.lock.in ${CMAKE_CURRENT_BINARY_DIR}/yarn.lock)
configure_file(.eslintrc.in ${CMAKE_CURRENT_BINARY_DIR}/.eslintrc)
configure_file(tsconfig.json.in ${CMAKE_CURRENT_BINARY_DIR}/tsconfig.json)
configure_file(index.html.in ${CMAKE_CURRENT_BINARY_DIR}/index.html)

set(BUILD_FILES
    ${CMAKE_CURRENT_LIST_DIR}/rollup.config.ts.in
    ${CMAKE_CURRENT_LIST_DIR}/package.json.in
    ${CMAKE_CURRENT_LIST_DIR}/yarn.lock.in
    ${CMAKE_CURRENT_LIST_DIR}/.eslintrc.in
    ${CMAKE_CURRENT_LIST_DIR}/index.html.in
    ${CMAKE_CURRENT_LIST_DIR}/tsconfig.json.in)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/node_modules
    COMMAND yarn install
    COMMENT "yarn install"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
add_custom_target(yarn_install DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/node_modules ${CMAKE_CURRENT_LIST_DIR}/package.json.in)

# execute_process(COMMAND yarn install --modules-folder ${CMAKE_CURRENT_BINARY_DIR}/node_modules)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/graph.js ${CMAKE_CURRENT_BINARY_DIR}/graph.js.map ${CMAKE_CURRENT_BINARY_DIR}/cli.js ${CMAKE_CURRENT_BINARY_DIR}/cli.js.map
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${VISUALIZER_SOURCES} yarn_install
    COMMENT "Building visualizer"
    COMMAND yarn build)

add_custom_target(graph ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/graph.js ${CMAKE_CURRENT_BINARY_DIR}/graph.js.map ${CMAKE_CURRENT_BINARY_DIR}/cli.js ${CMAKE_CURRENT_BINARY_DIR}/cli.js.map)
