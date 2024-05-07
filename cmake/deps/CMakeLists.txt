cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(deps LANGUAGES CXX C)

include(ExternalProject)

if(CMAKE_SYSROOT)
  set(CMAKE_C_FLAGS "--sysroot=${CMAKE_SYSROOT} ${CMAKE_C_FLAGS}")
  set(CMAKE_CXX_FLAGS "--sysroot=${CMAKE_SYSROOT} ${CMAKE_CXX_FLAGS}")
endif()

if(CMAKE_C_COMPILER_TARGET)
  set(CMAKE_C_FLAGS "--target=${CMAKE_C_COMPILER_TARGET} ${CMAKE_C_FLAGS}")
endif()

if(CMAKE_CXX_COMPILER_TARGET)
  set(CMAKE_CXX_FLAGS "--target=${CMAKE_C_COMPILER_TARGET} ${CMAKE_CXX_FLAGS}")
endif()

if(CC_ENABLE_IO_URING)
  ExternalProject_Add(
    liburing
    URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.5.tar.gz
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_INSTALL_PREFIX} --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
                      --use-libc
    BUILD_COMMAND make
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
    DOWNLOAD_NO_PROGRESS TRUE)
endif()

add_library(${PROJECT_NAME} INTERFACE)
