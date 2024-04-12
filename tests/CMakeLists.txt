file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

set(test_name "${PROJECT_NAME}_test")

add_executable(${test_name} ${sources})
target_link_libraries(${test_name} PRIVATE ${PROJECT_NAME} gtest_main)
add_test(NAME ${test_name} COMMAND ${test_name})
