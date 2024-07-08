message(STATUS "[cc] sqlite3 fetching ...")

FetchContent_Declare(
  sqlite3
  URL https://github.com/cc-hng/sqlite3/archive/v3.40.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS sqlite3)
