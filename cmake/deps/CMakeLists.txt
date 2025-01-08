cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(deps LANGUAGES CXX C)

include(../common.cmake)
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
  string(REPLACE "/" "\\/" CFLAGS_MODIFIED "${CMAKE_C_FLAGS}")

  ExternalProject_Add(
    liburing
    URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.6.tar.gz
    CONFIGURE_COMMAND
      sed -i "s/^\\(CFLAGS=\"[^\"]*\\)/\\1 ${CFLAGS_MODIFIED}/" configure && ./configure
      --prefix=${CMAKE_INSTALL_PREFIX} --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER} --use-libc && sed -i
      "/ -C examples$/d" Makefile && sed -i "/ -C test$/d" Makefile
    BUILD_COMMAND ${CMAKE_COMMAND} -E env CFLAGS=${CMAKE_C_FLAGS} make ENABLE_SHARED=0
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install ENABLE_SHARED=0
    DOWNLOAD_NO_PROGRESS TRUE)
endif()

add_library(${PROJECT_NAME} INTERFACE)
