include(cmake/ProjectGuard.cmake)
cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(
  cc
  VERSION "0.1.0"
  LANGUAGES CXX C)

# https://cmake.org/cmake/help/latest/variable/PROJECT_IS_TOP_LEVEL.html
string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} PROJECT_IS_TOP_LEVEL)

option(CC_ENABLE_COROUTINE "Enable c++2x coroutines" ON)
option(CC_ENABLE_IO_URING "Enable io_uring" ON)
option(CC_ENABLE_ASAN "Enable asan" ON)
option(CC_WITH_YYJSON "With yyjson" ON)
option(CC_WITH_SQLITE3 "With sqlite3" ON)
option(CC_ENABLE_INSTALL "Enable install" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_TESTS "Build unittest" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_BENCHES "Build benchmark" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})

#####################################
# compile & link options
#####################################
include(cmake/common.cmake)

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

configure_file(${CMAKE_CURRENT_LIST_DIR}/include/cc/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/_gen/cc/config.h @ONLY)

#####################################
# third-party packages
#####################################
_clear_variable(DESTINATION CMAKE_CXX_FLAGS_DEBUG BACKUP CMAKE_CXX_FLAGS_DEBUG_BAK)
_clear_variable(DESTINATION CMAKE_EXE_LINKER_FLAGS_DEBUG BACKUP CMAKE_EXE_LINKER_FLAGS_DEBUG_BAK)

include(cmake/external.cmake)

if(CC_ENABLE_IO_URING)
  find_package(PkgConfig REQUIRED)
  pkg_search_module(LIBURING REQUIRED liburing)
  add_library(liburing INTERFACE)
  target_include_directories(liburing INTERFACE ${LIBURING_INCLUDE_DIRS})
  target_link_directories(liburing INTERFACE ${LIBURING_LIBRARY_DIRS})
  target_link_libraries(liburing INTERFACE ${LIBURING_LINK_LIBRARIES})
endif()

find_package(Threads REQUIRED)

set(MY_LIBS)
include(FetchContent)

include(cmake/FetchFmt.cmake)
include(cmake/FetchGSL.cmake)
include(cmake/FetchSpdlog.cmake)

if(CC_BUILD_TESTS)
  include(cmake/FetchGTest.cmake)
endif()

if(CC_BUILD_BENCHES)
  include(cmake/FetchNanobench.cmake)
endif()

if(CC_WITH_YYJSON)
  include(cmake/FetchYyjson.cmake)
endif()

if(CC_WITH_SQLITE3)
  include(cmake/FetchSqlite3.cmake)
endif()

FetchContent_Declare(
  Format.cmake
  URL https://github.com/TheLartians/Format.cmake/archive/refs/tags/v1.7.3.tar.gz
  URL_HASH SHA256=55632412599faf85adf68ddc580a61f7b094e63a9da6345deeadc5408610246a)
list(APPEND MY_LIBS Format.cmake)

FetchContent_Declare(std_coroutines URL https://github.com/cc-hng/std_coroutines/archive/refs/tags/v1.0.1.tar.gz)
list(APPEND MY_LIBS std_coroutines)

# FetchContent_Declare(
#   PackageProject.cmake
#   URL https://github.com/TheLartians/PackageProject.cmake/archive/refs/tags/v1.11.1.tar.gz
#   URL_HASH SHA256=63f07c6814e9e39e0a21131be0a8093507bd9b0c5d3ff6ca113cb890a26f6ed2)
# list(APPEND MY_LIBS PackageProject.cmake)

FetchContent_MakeAvailable(${MY_LIBS})

_restore_variable(DESTINATION CMAKE_CXX_FLAGS_DEBUG BACKUP CMAKE_CXX_FLAGS_DEBUG_BAK)
_restore_variable(DESTINATION CMAKE_EXE_LINKER_FLAGS_DEBUG BACKUP CMAKE_EXE_LINKER_FLAGS_DEBUG_BAK)

set(Boost_DEBUG OFF)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_NO_WARN_NEW_VERSIONS
    ON
    CACHE INTERNAL "")
find_package(Boost 1.81 REQUIRED COMPONENTS url)

#####################################
# target
#####################################
add_library(3rd_dependencies INTERFACE)
target_link_libraries(
  3rd_dependencies
  INTERFACE Microsoft.GSL::GSL
            fmt::fmt
            spdlog
            std::coroutines
            Threads::Threads
            Boost::headers
            Boost::url
            $<$<BOOL:${CC_WITH_YYJSON}>:yyjson>)

if(CC_WITH_SQLITE3)
  target_link_libraries(3rd_dependencies INTERFACE sqlite3)
endif()

if(CC_ENABLE_COROUTINE)
  target_compile_features(3rd_dependencies INTERFACE cxx_std_20)
endif()

if(CC_ENABLE_IO_URING)
  target_link_libraries(3rd_dependencies INTERFACE liburing)
  target_compile_definitions(3rd_dependencies INTERFACE BOOST_ASIO_HAS_IO_URING=1 BOOST_ASIO_DISABLE_EPOLL=1)
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
add_library(${PROJECT_NAME} INTERFACE)
target_link_libraries(${PROJECT_NAME} INTERFACE 3rd_dependencies)
target_include_directories(
  ${PROJECT_NAME}
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/_gen>
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
