include(cmake/ProjectGuard.cmake)
cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(
  cc
  VERSION "0.1.0"
  LANGUAGES CXX C)

# https://cmake.org/cmake/help/latest/variable/PROJECT_IS_TOP_LEVEL.html
string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} PROJECT_IS_TOP_LEVEL)

option(CC_ENABLE_COROUTINE "Enable c++2x coroutines" ON)
option(CC_ENABLE_IO_URING "Enable io_uring" OFF)
option(CC_ENABLE_ASAN "Enable address san" OFF)
option(CC_ENABLE_TSAN "Enable thread sanitize" OFF)
option(CC_WITH_SQLITE3 "With sqlite3" ON)
option(CC_ENABLE_INSTALL "Enable install" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_TESTS "Build unittest" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_BENCHES "Build benchmark" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})

#####################################
# compile & link options
#####################################
include(cmake/common.cmake)

# address|thread san
if(CC_ENABLE_ASAN AND CC_ENABLE_TSAN)
  message(FATAL_ERROR "Expect only one sanitizer (address | thread)!")
else()
  if(NOT WIN32)
    if(CC_ENABLE_ASAN)
      set(CMAKE_CXX_FLAGS_DEBUG
          "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
      set(CMAKE_EXE_LINKER_FLAGS_DEBUG
          "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer")
    elseif(CC_ENABLE_TSAN)
      set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=thread")
      set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=thread")
    endif()
  endif()
endif()

# io_uring
if(CC_ENABLE_IO_URING AND LINUX)
  if(CMAKE_CROSSCOMPILING)
    include(CheckIncludeFileCXX)
    check_include_file_cxx("linux/io_uring.h" CC_HAS_IO_URING)
  else()
    include(CheckCXXSourceRuns)
    check_cxx_source_runs(
      "
        #include <linux/io_uring.h>
        #include <sys/syscall.h>
        #include <unistd.h>
        #include <cstdlib>

        int main() {
          io_uring_params a = {};
          return syscall(__NR_io_uring_setup, 1, &a) != -1 ? EXIT_SUCCESS : EXIT_FAILURE;
        }
      "
      CC_HAS_IO_URING)
  endif()

  set(CC_ENABLE_IO_URING ${CC_HAS_IO_URING})
else()
  set(CC_ENABLE_IO_URING OFF)
endif()

# check
include(CheckIncludeFile)
CHECK_INCLUDE_FILE("backtrace.h" CC_HAVE_BACKTRACE_H)

#####################################
# third-party packages
#####################################
_clear_variable(DESTINATION CMAKE_CXX_FLAGS_DEBUG BACKUP CMAKE_CXX_FLAGS_DEBUG_BAK)
_clear_variable(DESTINATION CMAKE_EXE_LINKER_FLAGS_DEBUG BACKUP CMAKE_EXE_LINKER_FLAGS_DEBUG_BAK)

include(cmake/external.cmake)
include(cmake/cpm.cmake)

if(CC_ENABLE_IO_URING)
  find_package(PkgConfig REQUIRED)
  pkg_search_module(LIBURING REQUIRED liburing)
  add_library(liburing INTERFACE)
  target_include_directories(liburing INTERFACE ${LIBURING_INCLUDE_DIRS})
  target_link_directories(liburing INTERFACE ${LIBURING_LIBRARY_DIRS})
  target_link_libraries(liburing INTERFACE ${LIBURING_LINK_LIBRARIES})
endif()

find_package(Threads REQUIRED)

if (LINUX AND CC_HAVE_BACKTRACE_H)
  set(BOOST_STACKTRACE_ENABLE_BACKTRACE ON)
else()
  set(BOOST_STACKTRACE_ENABLE_BASIC ON)
endif()
list(APPEND BOOST_INCLUDE_LIBRARIES "core;asio;beast;url;callable_traits;stacktrace")
CPMAddPackage(
  NAME Boost
  VERSION 1.86.0 # Versions less than 1.85.0 may need patches for installation targets.
  URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz
  URL_HASH SHA256=2c5ec5edcdff47ff55e27ed9560b0a0b94b07bd07ed9928b476150e16b0efc57
  OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
          "BUILD_SHARED_LIBS OFF" "BUILD_TESTING OFF" "BUILD_GMOCK OFF"
)
CPMAddPackage("gh:fmtlib/fmt#11.0.2")
CPMAddPackage("gh:microsoft/GSL#v4.1.0")

# yyjson
CPMAddPackage(
  NAME yyjson
  GIT_TAG 0.10.0
  GITHUB_REPOSITORY ibireme/yyjson
  OPTIONS "YYJSON_ENABLE_FASTMATH ON"
)

CPMAddPackage("gh:Neargye/nameof#v0.10.4")
CPMAddPackage(
  NAME cpp_yyjson
  GIT_TAG v0.6.0
  GITHUB_REPOSITORY yosh-matsuda/cpp-yyjson
  PATCHES ${CMAKE_CURRENT_LIST_DIR}/cmake/cpp-yyjson.patch
)

if(CC_BUILD_TESTS)
  CPMAddPackage("gh:google/googletest#v1.15.2")
endif()

if(CC_BUILD_BENCHES)
  CPMAddPackage("gh:martinus/nanobench#v4.3.11")
endif()

if(CC_WITH_SQLITE3)
  CPMAddPackage("gh:cc-hng/sqlite3#v3.40.0")
endif()

if(CC_BUILD_EXAMPLES)
  CPMAddPackage(
    NAME spdlog
    GIT_TAG v1.15.0
    GITHUB_REPOSITORY gabime/spdlog
    OPTIONS
      "SPDLOG_INSTALL ON"
      "SPDLOG_FMT_EXTERNAL ON"
  )
endif()

if(CC_ENABLE_COROUTINE)
  set(CMAKE_CXX_STANDARD 20)
  CPMAddPackage("gh:cc-hng/std_coroutines#v1.0.3")
endif()

CPMAddPackage("gh:TheLartians/Format.cmake@1.8.3")
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.13.0")
# CPMAddPackage("gh:aminya/project_options@0.36.2")


_restore_variable(DESTINATION CMAKE_CXX_FLAGS_DEBUG BACKUP CMAKE_CXX_FLAGS_DEBUG_BAK)
_restore_variable(DESTINATION CMAKE_EXE_LINKER_FLAGS_DEBUG BACKUP CMAKE_EXE_LINKER_FLAGS_DEBUG_BAK)

#####################################
# target
#####################################
add_library(3rd_dependencies INTERFACE)
target_link_libraries(
  3rd_dependencies INTERFACE
      Microsoft.GSL::GSL fmt::fmt Threads::Threads
      Boost::headers Boost::core Boost::asio
      Boost::url Boost::callable_traits Boost::beast boost_stacktrace
      yyjson cpp_yyjson::cpp_yyjson)
target_compile_definitions(
  3rd_dependencies INTERFACE
      $<$<BOOL:${CC_WITH_SQLITE3}>:CC_WITH_SQLITE3>
      $<$<BOOL:${CC_ENABLE_COROUTINE}>:CC_ENABLE_COROUTINE>)

if(CC_WITH_SQLITE3)
  target_link_libraries(3rd_dependencies INTERFACE sqlite3)
endif()

if(CC_ENABLE_COROUTINE)
  target_link_libraries(3rd_dependencies INTERFACE std::coroutines)
endif()

if(CC_ENABLE_IO_URING)
  target_link_libraries(3rd_dependencies INTERFACE liburing)
  target_compile_definitions(
    3rd_dependencies INTERFACE BOOST_ASIO_HAS_IO_URING=1 BOOST_ASIO_DISABLE_EPOLL=1)
endif()

if(WIN32)
  target_link_libraries(3rd_dependencies INTERFACE wsock32 ws2_32)
  if(MINGW)
    target_compile_options(3rd_dependencies INTERFACE -Wa,-mbig-obj)
  endif()
endif()

#####################################
# third-party dependencies
#####################################
file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/**.h")

add_library(${PROJECT_NAME} INTERFACE)
target_link_libraries(${PROJECT_NAME} INTERFACE 3rd_dependencies)
target_sources(${PROJECT_NAME} INTERFACE ${headers})
target_include_directories(
  ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>)

#####################################
# test
#####################################
if(CC_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()

#####################################
# benchmark
#####################################
if(CC_BUILD_BENCHES)
  add_subdirectory(benchmarks)
endif()

#####################################
# examples
#####################################
if(CC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

#####################################
# install & pack
#####################################
if(CC_ENABLE_INSTALL)
  #   install(PROGRAMS ${PROJECT_BINARY_DIR}/out/nlua DESTINATION bin)
  #   install(DIRECTORY ${PROJECT_BINARY_DIR}/out/modules DESTINATION bin)
  #   install(DIRECTORY ${PROJECT_BINARY_DIR}/out/services DESTINATION bin)
  #   install(DIRECTORY ${PROJECT_SOURCE_DIR}/runtime DESTINATION bin)
  #   install(DIRECTORY ${PROJECT_SOURCE_DIR}/examples DESTINATION bin)
  #   include(CPack)
endif()

#####################################
message(STATUS "------------------ info ------------------")
message(STATUS "c standard: ${CMAKE_C_STANDARD}")
message(STATUS "cxx standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "lto: ${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
message(STATUS "c++20 coroutines: ${CC_ENABLE_COROUTINE}")
message(STATUS "io_uring: ${CC_ENABLE_IO_URING}")
message(STATUS "address san: ${CC_ENABLE_ASAN}")
message(STATUS "thread san: ${CC_ENABLE_TSAN}")
message(STATUS "------------------------------------------")
