#include "cc/json.h"
#include "common.h"
#include <fstream>
#include <string>

static auto get_data(const char* file_name) {
    std::ifstream f(std::string(CC_BENCHMARK_DATA) + "/" + file_name);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

#include <nlohmann/json.hpp>

#define BENCH_JSON_FILE_BY_NLOHMANN(name)                  \
    auto name##_json = get_data(#name ".json");            \
    auto name##_obj  = nlohmann::json::parse(name##_json); \
                                                           \
    b.run(#name " - parse", [&] {                          \
        auto obj = nlohmann::json::parse(name##_json);     \
        bench::doNotOptimizeAway(obj);                     \
    });                                                    \
                                                           \
    b.run(#name " - dump", [&] {                           \
        auto s = (name##_obj).dump();                      \
        bench::doNotOptimizeAway(s);                       \
    })

static void bench_nlohmann(bench::Bench& b) {
    b.title("nlohmann");
    BENCH_JSON_FILE_BY_NLOHMANN(twitter);
    BENCH_JSON_FILE_BY_NLOHMANN(canada);
    BENCH_JSON_FILE_BY_NLOHMANN(citm_catalog);
}
BENCHMARK_REGISTE(bench_nlohmann);

#include <cpp_yyjson.hpp>

#define BENCH_CPP_YYJSON_FILE(name)               \
    auto name##_json = get_data(#name ".json");   \
    auto name##_obj  = yyjson::read(name##_json); \
                                                  \
    b.run(#name " - parse", [&] {                 \
        auto obj = yyjson::read(name##_json);     \
        bench::doNotOptimizeAway(obj);            \
    });                                           \
                                                  \
    b.run(#name " - dump", [&] {                  \
        auto s = (name##_obj).write();            \
        bench::doNotOptimizeAway(s);              \
    })

static void bench_cpp_yyjson(bench::Bench& b) {
    b.title("cpp-yyjson");
    BENCH_CPP_YYJSON_FILE(twitter);
    BENCH_CPP_YYJSON_FILE(canada);
    BENCH_CPP_YYJSON_FILE(citm_catalog);
}

BENCHMARK_REGISTE(bench_cpp_yyjson);
