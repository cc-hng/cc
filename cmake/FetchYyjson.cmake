message(STATUS "[cc] yyjson fetching ...")

FetchContent_Declare(
  yyjson
  URL https://github.com/ibireme/yyjson/archive/refs/tags/0.8.0.tar.gz
  URL_HASH SHA256=b2e39ac4c65f9050820c6779e6f7dd3c0d3fed9c6667f91caec0badbedce00f3
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS yyjson)
