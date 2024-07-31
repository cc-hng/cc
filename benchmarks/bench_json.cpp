#ifdef CC_WITH_YYJSON

#    include "cc/json.h"
#    include "common.h"
#    include <fstream>
#    include <string>

static auto get_data(const char* file_name) {
    std::ifstream f(std::string(CC_BENCHMARK_DATA) + "/" + file_name);
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

static auto parse_canada(std::string json) {
    // clang-format off
    struct geometry_t {
        BOOST_HANA_DEFINE_STRUCT(geometry_t,
            (std::string, type),
            (std::vector<std::vector<std::vector<double>>>, coordinates)
        );
    };

    struct feature_t {
        BOOST_HANA_DEFINE_STRUCT(feature_t,
            (std::string, type),
            // (std::map<std::string, std::string>, properties),
            (geometry_t, geometry)
        );
    };

    struct canada_t {
        BOOST_HANA_DEFINE_STRUCT(canada_t,
            (std::string, type),
            (std::vector<feature_t>, features)
        );
    };
    // clang-format on

    auto c = cc::json::parse<canada_t>(json);
    bench::doNotOptimizeAway(c);
    return c;
}

[[maybe_unused]] static void parse_citm_catalog(const std::string& json) {
    // clang-format off
    // clang-format on
}

[[maybe_unused]] static void parse_twitter(const std::string& json) {
}

static void bench_json(bench::Bench& b) {
    auto canada_json  = get_data("canada.json");
    auto citm_json    = get_data("citm_catalog.json");
    auto twitter_json = get_data("twitter.json");
    auto canada       = parse_canada(canada_json);

    b.title("yyjson");
    b.run("canada.json - parse", [&] { parse_canada(canada_json); });
    b.run("canada.json - dump", [&] {
        auto s = cc::json::dump(canada);
        bench::doNotOptimizeAway(s);
    });

    // b.run("citm_catalog.json", [&] { parse_citm_catalog(citm_json); });
    // b.run("twitter.json", [&] { parse_twitter(twitter_json); });
}

BENCHMARK_REGISTE(bench_json);

#endif
