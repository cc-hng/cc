message(STATUS "[cc] sqlite3 fetching ...")

FetchContent_Declare(
  sqlite3
  URL https://git.l2x.top/cc/sqlite3/archive/v3.40.0.tar.gz
  URL_HASH SHA256=5f613d466429daf7f5b163f6a7b2a29efc05ae174bb0f65ef8c8b7be56a95785
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS sqlite3)
