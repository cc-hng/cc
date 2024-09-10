#ifdef CC_WITH_YYJSON

#    include "cc/json.h"
#    include "common.h"
#    include <fstream>
#    include <string>

static auto get_data(const char* file_name) {
    std::ifstream f(std::string(CC_BENCHMARK_DATA) + "/" + file_name);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

#    define BENCH_JSON_FILE(name)                                   \
        auto name##_json = get_data(#name ".json");                 \
        auto name##_obj  = cc::json::parse<cc::Value>(name##_json); \
                                                                    \
        b.run(#name " - parse", [&] {                               \
            auto obj = cc::json::parse<cc::Value>(name##_json);     \
            bench::doNotOptimizeAway(obj);                          \
        });                                                         \
                                                                    \
        b.run(#name " - dump", [&] {                                \
            auto s = cc::json::dump(name##_obj);                    \
            bench::doNotOptimizeAway(s);                            \
        })

static void bench_json(bench::Bench& b) {
    b.title("yyjson");
    auto old = b.epochIterations();
    b.minEpochIterations(15);
    BENCH_JSON_FILE(twitter);
    BENCH_JSON_FILE(canada);
    BENCH_JSON_FILE(citm_catalog);
    b.minEpochIterations(old);
}

BENCHMARK_REGISTE(bench_json);

#endif
