message(STATUS "[cc] nanobench fetching ...")

FetchContent_Declare(
  nanobench
  URL https://github.com/martinus/nanobench/archive/refs/tags/v4.3.11.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE
  OVERRIDE_FIND_PACKAGE)

list(APPEND MY_LIBS nanobench)
