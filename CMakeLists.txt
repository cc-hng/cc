cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

project(cc VERSION 0.2.0 LANGUAGES C CXX)

include(cmake/cpm.cmake)
CPMAddPackage("gh:cc-hng/cmake-scripts#v0.1.1")
include(${cmake-scripts_SOURCE_DIR}/ProjectGuard.cmake)

# https://cmake.org/cmake/help/latest/variable/PROJECT_IS_TOP_LEVEL.html
string(COMPARE EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} PROJECT_IS_TOP_LEVEL)

option(CC_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(CC_BUILD_BENCHES "Build benchmark" ${PROJECT_IS_TOP_LEVEL})
option(CC_USE_MUTEX "Enable mutex" ON)

#####################################
# compile & link options
#####################################
include(${cmake-scripts_SOURCE_DIR}/core.cmake)
cxx_20()

# reflection macro
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>)
endif()

#####################################
# third-party packages
#####################################
find_package(Threads REQUIRED)

CPMAddPackage(
  URI "gh:fmtlib/fmt#12.2.0"
  OPTIONS "FMT_MODULE OFF" "FMT_INSTALL ON")
CPMAddPackage("gh:microsoft/GSL#v4.2.1")
CPMAddPackage("gh:cc-hng/std_coroutines#v1.0.3")
CPMAddPackage("gh:cc-hng/sqlite3#v3.53.2")
CPMAddPackage("gh:Neargye/nameof#v0.10.6")

if (LINUX AND CC_HAVE_BACKTRACE_H)
  set(BOOST_STACKTRACE_ENABLE_BACKTRACE ON)
else()
  set(BOOST_STACKTRACE_ENABLE_BASIC ON)
endif()
list(APPEND BOOST_INCLUDE_LIBRARIES "asio;signals2;callable_traits;stacktrace;container")
CPMAddPackage(
  NAME Boost
  VERSION 1.91.0 # Versions less than 1.91.0 may need patches for installation targets.
  URL https://github.com/boostorg/boost/releases/download/boost-1.91.0-1/boost-1.91.0-1-cmake.tar.xz
  URL_HASH SHA256=cc5dc5006ecbdf0051f90979be31b4eee5987d9ae14ae9fb9c03cfa43fa3cdad
  OPTIONS "BOOST_ENABLE_CMAKE ON" "BOOST_SKIP_INSTALL_RULES ON" # Set `OFF` for installation
          "BUILD_SHARED_LIBS OFF" "BUILD_TESTING OFF" "BUILD_GMOCK OFF")

#####################################
# target
#####################################
add_library(3rd-dependencies INTERFACE)
target_link_libraries(
  3rd-dependencies INTERFACE
    fmt::fmt sqlite3
    Microsoft.GSL::GSL
    nameof
    Boost::headers Boost::signals2 Boost::callable_traits Boost::asio boost_stacktrace
    std::coroutines
    Threads::Threads)

# Boost headers are consumed via non-SYSTEM include dirs (plain -I), so
# warnings inside them (e.g. -Wnontrivial-memcall in boost::container) are not
# silenced by -isystem. Boost::headers is an ALIAS target, so it cannot be
# promoted to SYSTEM directly; silence the one noisy clang diagnostic instead.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-nontrivial-memcall>)
endif()

# GLOB does not recurse: `cc/**.h` is just `cc/*.h` (one level), which silently
# dropped asio/, asio/channel/ and detail/. GLOB_RECURSE is the recursive form.
file(GLOB_RECURSE cxx_sources "${CMAKE_CURRENT_LIST_DIR}/include/cc/*.h")
add_library(${PROJECT_NAME} INTERFACE)
target_sources(${PROJECT_NAME} INTERFACE ${cxx_sources})
target_link_libraries(${PROJECT_NAME} INTERFACE 3rd-dependencies)
target_compile_definitions(
  ${PROJECT_NAME} INTERFACE
    $<$<BOOL:${CC_USE_MUTEX}>:CC_USE_MUTEX>)
target_include_directories(
  ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>)

#####################################
# examples
#####################################
if (CC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

#####################################
# benchmark
#####################################
if(CC_BUILD_BENCHES)
  add_subdirectory(benches)
endif()

#####################################
# test
#####################################
if(CC_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_subdirectory(tests)
endif()

