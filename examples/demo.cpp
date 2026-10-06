
#include "log.h"

void init_logger() {
    LogBuilder()                                 //
        .addConsoleLogger(spdlog::level::debug)  //
        .build();
}

int main() {
    init_logger();
    LOGI("Hello world!");
    return 0;
}
