#include "common.h"
#include <charconv>
#include <cstdlib>
#include <format>
#include <c4/format.hpp>
#include <fmt/core.h>
#include <fmt/format.h>

// <charconv>
// usage: std::from_chars | std::to_chars

static uint64_t to_unsigned_integer(std::string_view str) {
    /* We assume at least 64-bit integer giving us safely 999999999999999999 (18 number of 9s) */
    if (str.length() > 18) {
        return UINT64_MAX;
    }

    uint64_t unsigned_integer_value = 0;
    for (char c : str) {
        /* As long as the letter is 0-9 we cannot overflow. */
        if (c < '0' || c > '9') {
            return UINT64_MAX;
        }
        unsigned_integer_value = unsigned_integer_value * 10ull + ((unsigned int)c - (unsigned int)'0');
    }
    return unsigned_integer_value;
}

static void c4_fromchars(bench::Bench& b) {
    c4::csubstr s1 = "1234567890";
    std::string s2 = "1234567890";
    b.run("c4::from_chars", [&] {
        uint64_t v;
        c4::from_chars(s1, &v);
        bench::doNotOptimizeAway(v);
    });
    b.run("std::from_chars", [&] {
        uint64_t v;
        const char* p = s2.data();
        std::from_chars(p, p + s2.size(), v);
        bench::doNotOptimizeAway(v);
    });
    b.run("custom", [&] {
        uint64_t v = to_unsigned_integer(s2);
        bench::doNotOptimizeAway(v);
    });
    b.run("atoi", [&] { bench::doNotOptimizeAway(std::atol(s2.c_str())); });
}

BENCHMARK_REGISTE("c4_fromchars", c4_fromchars);

static void c4_tochars(bench::Bench& b) {
    uint64_t v = 1234567890;
    b.run("c4::to_chars", [&] {
        c4::substr s;
        c4::to_chars(s, v);
        bench::doNotOptimizeAway(s);
    });

    b.run("std::to_chars", [&] {
        char buf[32];
        std::to_chars(buf, buf + 32, v);
    });

    b.run("itoa", [&] { bench::doNotOptimizeAway(std::to_string(v)); });

    b.run("std::format", [&] { bench::doNotOptimizeAway(std::format("{}", v)); });
    b.run("fmt::format", [&] { bench::doNotOptimizeAway(fmt::format("{}", v)); });
    b.run("sprintf", [&] {
        char buf[32];
        sprintf(buf, "%lu", v);
    });
}
BENCHMARK_REGISTE("c4_tochars", c4_tochars);
