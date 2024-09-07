message(STATUS "[cc] mimalloc fetching ...")
# set(GSL_INSTALL ON)
set(MI_BUILD_STATIC ON)

FetchContent_Declare(
  mimalloc
  URL https://github.com/microsoft/mimalloc/archive/refs/tags/v2.1.7.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS mimalloc)
