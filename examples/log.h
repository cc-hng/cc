#pragma once

#include <spdlog/spdlog.h>

inline void init_logger() {
    spdlog::set_pattern(R"(%^%L%$ | %Y-%m-%dT%H:%M:%S.%e | %t | %s:%# | %v)");
}

#define LOGD(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOGI(...) SPDLOG_INFO(__VA_ARGS__)
#define LOGW(...) SPDLOG_WARN(__VA_ARGS__)
#define LOGE(...) SPDLOG_ERROR(__VA_ARGS__)
