# re2: https://stackoverflow.com/q/70583395

message(STATUS "[cc] re2 fetching ...")

FetchContent_Declare(
  re2
  URL https://github.com/google/re2/archive/refs/tags/2024-07-02.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS re2)
