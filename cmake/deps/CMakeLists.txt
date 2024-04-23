cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

project(deps)

include(ExternalProject)

if(CC_ENABLE_IO_URING)
  ExternalProject_Add(
    liburing
    URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.5.tar.gz
    CONFIGURE_COMMAND ./configure --prefix=${CMAKE_INSTALL_PREFIX} --cc=${CMAKE_C_COMPILER} --cxx=${CMAKE_CXX_COMPILER}
    BUILD_COMMAND make
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND make install
    DOWNLOAD_NO_PROGRESS TRUE)
endif()

add_library(${PROJECT_NAME} INTERFACE)
