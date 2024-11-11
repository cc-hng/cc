message(STATUS "[cc] gsl fetching ...")
set(GSL_INSTALL ON)

FetchContent_Declare(
  gsl
  URL https://github.com/microsoft/GSL/archive/refs/tags/v4.1.0.tar.gz
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS gsl)
