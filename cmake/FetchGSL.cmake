message(STATUS "[cc] gsl fetching ...")
set(GSL_INSTALL ON)

FetchContent_Declare(
  gsl
  URL https://github.com/microsoft/GSL/archive/refs/tags/v4.0.0.tar.gz
  URL_HASH SHA256=f0e32cb10654fea91ad56bde89170d78cfbf4363ee0b01d8f097de2ba49f6ce9
  DOWNLOAD_NO_PROGRESS TRUE)

list(APPEND MY_LIBS gsl)
