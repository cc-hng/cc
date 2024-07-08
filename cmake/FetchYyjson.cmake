message(STATUS "[cc] yyjson fetching ...")

FetchContent_Declare(
  yyjson
  URL https://github.com/ibireme/yyjson/archive/refs/tags/0.8.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS yyjson)
