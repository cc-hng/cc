message(STATUS "[cc] googletest fetching ...")

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
  DOWNLOAD_NO_PROGRESS TRUE
  OVERRIDE_FIND_PACKAGE)

list(APPEND MY_LIBS googletest)
