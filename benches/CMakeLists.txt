
CPMAddPackage("gh:martinus/nanobench#v4.6.0")
CPMAddPackage("gh:biojppm/c4core#v0.2.10")

file(GLOB_RECURSE bench_sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")

set(bench_name "${PROJECT_NAME}_bench")

add_executable(${bench_name} ${bench_sources})
target_link_libraries(${bench_name} PRIVATE ${PROJECT_NAME} nanobench c4core)
target_include_directories(${bench_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(${bench_name} PRIVATE <common.h>)
target_compile_features(${bench_name} INTERFACE cxx_std_20)
if (CMAKE_BUILD_TYPE AND CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_time_optimization(${bench_name} REQUIRED)
endif()
