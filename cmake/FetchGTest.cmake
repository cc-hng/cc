message(STATUS "[cc] googletest fetching ...")

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE
  OVERRIDE_FIND_PACKAGE)

list(APPEND MY_LIBS googletest)
