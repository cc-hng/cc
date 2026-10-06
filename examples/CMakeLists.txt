
CPMAddPackage(
  URI "gh:gabime/spdlog#v1.17.0"
  OPTIONS "SPDLOG_INSTALL ON" "SPDLOG_FMT_EXTERNAL ON")

macro(add_example TARGET)
  set(multiValueArgs SRCS DEPENDS)
  cmake_parse_arguments(EX "" "" "${multiValueArgs}" ${ARGN})
  add_executable(${TARGET} ${TARGET}.cpp)
  target_link_libraries(${TARGET} PRIVATE ${PROJECT_NAME} ${EX_DEPENDS} spdlog)
endmacro()

add_example(demo)
add_example(lsqlite3)
# add_example(stacktrace)
add_example(thread_local)
add_example(signal)

add_example(asio)
set(pch_header "${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h")
target_precompile_headers(asio PRIVATE ${pch_header})

# 独立示例 condvar（位于 asio/ 子目录；add_example macro 不支持子目录 target 名，故手动注册）
add_executable(condvar asio/condvar.cpp)
target_link_libraries(condvar PRIVATE ${PROJECT_NAME} spdlog)
target_precompile_headers(condvar PRIVATE ${pch_header})
target_include_directories(condvar PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 独立示例 sem（位于 asio/ 子目录；注册方式同 condvar）
add_executable(sem asio/sem.cpp)
target_link_libraries(sem PRIVATE ${PROJECT_NAME} spdlog)
target_precompile_headers(sem PRIVATE ${pch_header})
target_include_directories(sem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 独立示例 mpsc（位于 asio/ 子目录；注册方式同 condvar）
add_executable(mpsc asio/mpsc.cpp)
target_link_libraries(mpsc PRIVATE ${PROJECT_NAME} spdlog)
target_precompile_headers(mpsc PRIVATE ${pch_header})
target_include_directories(mpsc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


# add_example(send)
# add_example(recv)
#
# add_example(reply)
# add_example(request)

# add_example(popen)

# add_executable(bridge bridge.c)
# target_link_libraries(bridge PRIVATE Threads::Threads)
# set_target_properties(bridge PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
