#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/async.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

/// @brief Logger builder
/// @note This is a simple wrapper for spdlog::logger
/// @see https://github.com/gabime/spdlog
class LogBuilder {
    inline static std::string default_pattern_ = " %^%L%$ | %Y-%m-%dT%H:%M:%S.%e | %5t | %s:%-3# | %v";

public:
    using level_type = spdlog::level::level_enum;

public:
    LogBuilder& setName(const std::string& name) {
        name_ = name;
        return *this;
    }

    LogBuilder& setAsync(bool enable) {
        async_ = enable;
        return *this;
    }

    LogBuilder& addConsoleLogger(level_type lv, std::string_view pattern = default_pattern_) {
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_pattern(std::string(pattern));
        console->set_level(lv);
        sinks_.push_back(console);
        return *this;
    }

    LogBuilder& addDailyLogger(level_type lv, std::string_view filename, int max_files,
                               std::string_view pattern = default_pattern_) {
        auto daily =
            std::make_shared<spdlog::sinks::daily_file_sink_mt>(std::string(filename), 0, 0, false, max_files);
        daily->set_pattern(std::string(pattern));
        daily->set_level(lv);
        sinks_.push_back(daily);
        return *this;
    }

    void build() {
        logger_ = std::make_shared<spdlog::logger>(name_, sinks_.begin(), sinks_.end());
        logger_->set_level(spdlog::level::trace);
        logger_->set_pattern(default_pattern_);
        logger_->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger_);

        // periodically flush all *registered* loggers every 3 seconds:
        // warning: only use if all your loggers are thread safe ("_mt" loggers)
        spdlog::flush_every(std::chrono::seconds(3));
    }

public:
    static level_type tolevel(std::string_view);

private:
    std::string name_ = "UNKNOWN";
    std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks_;
    std::shared_ptr<spdlog::logger> logger_;
    bool async_ = true;

    static const std::string DEFAULT_PATTERN;
};

#define LOGD(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOGI(...) SPDLOG_INFO(__VA_ARGS__)
#define LOGW(...) SPDLOG_WARN(__VA_ARGS__)
#define LOGE(...) SPDLOG_ERROR(__VA_ARGS__)
