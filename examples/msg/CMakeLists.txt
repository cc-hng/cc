# process msg

include(${CMAKE_SOURCE_DIR}/cmake/process_msg.cmake)

process_msg_directory(MSGS ${CMAKE_CURRENT_LIST_DIR} "msg")

find_package(Boost REQUIRED)

add_library(msg INTERFACE ${MSGS})
target_include_directories(msg INTERFACE "${CMAKE_BINARY_DIR}/_gen/msg/examples")
target_link_libraries(msg INTERFACE Boost::headers)
