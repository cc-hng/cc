message(STATUS "[cc] spdlog fetching ...")
set(SPDLOG_INSTALL ON)
set(SPDLOG_FMT_EXTERNAL ON)

FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.12.0.tar.gz
  URL_HASH SHA256=4dccf2d10f410c1e2feaff89966bfc49a1abb29ef6f08246335b110e001e09a9
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS spdlog)
