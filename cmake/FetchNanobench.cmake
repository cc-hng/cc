message(STATUS "[cc] nanobench fetching ...")

FetchContent_Declare(
  nanobench
  URL https://github.com/martinus/nanobench/archive/refs/tags/v4.3.11.tar.gz
  URL_HASH SHA256=53a5a913fa695c23546661bf2cd22b299e10a3e994d9ed97daf89b5cada0da70
  DOWNLOAD_NO_PROGRESS TRUE
  OVERRIDE_FIND_PACKAGE)

list(APPEND MY_LIBS nanobench)
