# Fetch benchmark data.
message(STATUS "Fetching json benchmark data")
function(fetch_data file url)
  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/data/${file})
    file(DOWNLOAD "${url}" "${CMAKE_CURRENT_BINARY_DIR}/data/${file}" STATUS status)
    list(GET status 0 status_code)
    list(GET status 1 status_string)

    if(NOT status_code EQUAL 0)
      message(FATAL_ERROR "error downloading ${file}: ${status_string}")
    endif()
  endif()
endfunction()

fetch_data(canada.json https://github.com/miloyip/nativejson-benchmark/raw/master/data/canada.json)
fetch_data(citm_catalog.json https://github.com/miloyip/nativejson-benchmark/raw/master/data/citm_catalog.json)
fetch_data(twitter.json https://raw.githubusercontent.com/miloyip/nativejson-benchmark/master/data/twitter.json)

cpm_add_package("gh:nlohmann/json#v3.11.3")

# Add executable
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
set(bench_name "${PROJECT_NAME}_bench")
add_executable(${bench_name} ${sources})
target_link_libraries(${bench_name} PRIVATE ${PROJECT_NAME} nanobench nlohmann_json::nlohmann_json)
target_compile_definitions(${bench_name} PRIVATE CC_BENCHMARK_DATA="${CMAKE_CURRENT_BINARY_DIR}/data/")
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
