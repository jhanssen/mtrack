cmake_minimum_required(VERSION 3.13)
execute_process(COMMAND yarn install WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
add_custom_target(graph ALL COMMAND cd ${CMAKE_CURRENT_LIST_DIR} && yarn build)
