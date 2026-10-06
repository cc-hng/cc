
CPMAddPackage("gh:google/googletest#v1.15.2")

# gtest's own TU compilation hits -Wcharacter-conversion (char8_t->char32_t in
# gtest-printers.h) under zig clang; silence it only on the library targets,
# consumer TUs already get suppression via -isystem.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(gtest PRIVATE -Wno-character-conversion)
  target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
endif()

file(GLOB_RECURSE test_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

set(test_name "${PROJECT_NAME}_test")

add_executable(${test_name} ${test_sources})
target_link_libraries(${test_name} PRIVATE ${PROJECT_NAME} gtest_main)
add_test(NAME ${test_name} COMMAND ${test_name})
