message(STATUS "[cc] fmt fetching ...")

set(FMT_INSTALL ON)

FetchContent_Declare(
  fmt
  URL https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS fmt)
