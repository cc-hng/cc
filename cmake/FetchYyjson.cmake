message(STATUS "[cc] yyjson fetching ...")

find_program(PATCH_CMD patch REQUIRED)

set(YYJSON_ENABLE_FASTMATH ON)

FetchContent_Declare(
  yyjson
  URL https://github.com/ibireme/yyjson/archive/refs/tags/0.10.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

FetchContent_Declare(
  nameof
  URL https://github.com/Neargye/nameof/archive/refs/tags/v0.10.4.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

FetchContent_Declare(
  cpp_yyjson
  URL https://github.com/yosh-matsuda/cpp-yyjson/archive/refs/tags/v0.6.0.tar.gz
  PATCH_COMMAND ${PATCH_CMD} -p1 < ${CMAKE_CURRENT_LIST_DIR}/cpp-yyjson.patch
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS yyjson nameof cpp_yyjson)
