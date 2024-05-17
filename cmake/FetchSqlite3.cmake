message(STATUS "[cc] sqlite3 fetching ...")

FetchContent_Declare(
  sqlite3
  URL https://github.com/cc-hng/sqlite3/archive/v3.40.0.tar.gz
  URL_HASH SHA256=bb9b924088400f678ab83ea5ba72f68eafaba56b9ec7f2d7c5b93cbdcd4b674f
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS sqlite3)
