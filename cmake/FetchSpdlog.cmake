message(STATUS "[cc] spdlog fetching ...")
set(SPDLOG_INSTALL ON)
set(SPDLOG_FMT_EXTERNAL ON)

FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS spdlog)
