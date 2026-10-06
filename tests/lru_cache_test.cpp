#include <memory>
#include <thread>
#include <cc/lru_cache.h>
#include <gtest/gtest.h>

using cc::LRUCache;

namespace {
class NoDefault {
public:
    int x;
    explicit NoDefault(int v) : x(v) {}
};
}  // namespace

TEST(LRUCacheTest, SetAndGet) {
    LRUCache<int, int> cache(3, 10);

    cache.set(1, 100);
    cache.set(2, 200);

    EXPECT_EQ(100, cache.get(1).value());
    EXPECT_EQ(200, cache.get(2).value());
    EXPECT_FALSE(cache.get(3).has_value());
}

TEST(LRUCacheTest, GetPtrNoCopy) {
    LRUCache<int, std::string> cache(3, 10);
    cache.set(1, "hello");

    const auto* p = cache.getPtr(1);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ("hello", *p);
}

TEST(LRUCacheTest, GetRefZeroCopy) {
    LRUCache<int, std::string> cache(3, 10);
    cache.set(1, "world");

    auto ref = cache.getRef(1);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ("world", ref->get());
}

TEST(LRUCacheTest, ContainsRespectsExpiry) {
    LRUCache<int, int> cache(3, 1);  // 1 second TTL
    cache.set(1, 100);

    EXPECT_TRUE(cache.contains(1));

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // contains should return false after TTL
    EXPECT_FALSE(cache.contains(1));
}

TEST(LRUCacheTest, EvictWhenFull) {
    LRUCache<int, int> cache(2, 10);

    cache.set(1, 100);
    cache.set(2, 200);
    EXPECT_EQ(2, cache.size());

    // This should evict key 1 (oldest)
    cache.set(3, 300);
    EXPECT_EQ(2, cache.size());
    EXPECT_FALSE(cache.contains(1));  // evicted
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
}

TEST(LRUCacheTest, LRUOrderUpdateOnGet) {
    LRUCache<int, int> cache(3, 10);

    cache.set(1, 100);
    cache.set(2, 200);
    cache.set(3, 300);

    // Access key 1 -> it becomes most recent
    cache.get(1);

    // Evict the oldest (should be key 2, not 1)
    cache.set(4, 400);
    EXPECT_FALSE(cache.contains(2));  // evicted
    EXPECT_TRUE(cache.contains(1));   // still there (bumped)
    EXPECT_TRUE(cache.contains(3));
    EXPECT_TRUE(cache.contains(4));
}

TEST(LRUCacheTest, UpdateExistingKey) {
    LRUCache<int, int> cache(3, 10);

    cache.set(1, 100);
    cache.set(1, 999);  // update
    cache.set(2, 200);
    cache.set(3, 300);

    EXPECT_EQ(999, cache.get(1).value());
    EXPECT_EQ(3, cache.size());
}

TEST(LRUCacheTest, Clear) {
    LRUCache<int, int> cache(3, 10);
    cache.set(1, 100);
    cache.set(2, 200);
    cache.set(3, 300);

    cache.clear();
    EXPECT_EQ(0, cache.size());
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    EXPECT_FALSE(cache.contains(3));
}

TEST(LRUCacheTest, Remove) {
    LRUCache<int, int> cache(3, 10);
    cache.set(1, 100);
    cache.set(2, 200);

    cache.remove(1);
    EXPECT_EQ(1, cache.size());
    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
}

TEST(LRUCacheTest, CustomTTL) {
    LRUCache<int, int> cache(3, 10);

    // Insert with 0-second TTL (immediate expiry)
    cache.set(1, 100, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(cache.contains(1));
    EXPECT_FALSE(cache.get(1).has_value());
}

TEST(LRUCacheTest, EvictOnSingleSlot) {
    LRUCache<int, int> cache(1, 10);

    cache.set(1, 100);
    EXPECT_EQ(1, cache.size());

    cache.set(2, 200);
    EXPECT_EQ(1, cache.size());
    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
}

TEST(LRUCacheTest, TakeMovesValueOut) {
    LRUCache<int, std::unique_ptr<int>> cache(3, 10);
    cache.set(1, std::make_unique<int>(7));

    auto taken = cache.take(1);
    ASSERT_TRUE(taken.has_value());
    EXPECT_EQ(7, **taken);
    EXPECT_EQ(0, cache.size());
    EXPECT_FALSE(cache.take(1).has_value());
}

TEST(LRUCacheTest, NonDefaultConstructibleValue) {
    LRUCache<int, NoDefault> cache(2, 10);
    cache.set(1, NoDefault(5));

    const auto* p = cache.getPtr(1);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(5, p->x);
}

// Stress test: thousands of insert/evict/remove/take cycles must keep the
// index, the list and the node pool in agreement. Capacities include primes
// where unordered_map has no bucket slack, i.e. the tightest rehash case.
TEST(LRUCacheTest, ManyInsertEvictCycles) {
    for (const size_t cap : {67u, 101u, 103u, 1009u}) {
        LRUCache<int, int> cache(cap, 10);
        for (int i = 0; i < 20000; ++i) {
            cache.set(i, i);
            ASSERT_LE(cache.size(), cap);
        }
        for (int i = 20000 - static_cast<int>(cap); i < 20000; ++i) {
            ASSERT_TRUE(cache.contains(i)) << "cap=" << cap << " i=" << i;
        }
    }
}

TEST(LRUCacheTest, KeyPointerSurvivesRehash) {
    constexpr size_t CAP = 67;
    LRUCache<std::string, int> cache(CAP, 10);

    for (int i = 0; i < 2000; ++i) {
        cache.set("key-" + std::to_string(i), i);
    }

    EXPECT_EQ(CAP, cache.size());
    for (int i = 2000 - static_cast<int>(CAP); i < 2000; ++i) {
        const auto key = "key-" + std::to_string(i);
        ASSERT_TRUE(cache.contains(key)) << key;
        EXPECT_EQ(i, cache.get(key).value());
    }
}

// std::string key: lookups by string_view / const char* / literal, with no
// temporary std::string (heterogeneous lookup, enabled by default for Key==std::string).
TEST(LRUCacheTest, HeterogeneousStringLookup) {
    LRUCache<std::string, int> cache(4, 10);
    cache.set("alpha", 1);

    std::string_view sv = "alpha";
    EXPECT_EQ(1, cache.get(sv).value());
    EXPECT_EQ(1, *cache.getPtr(sv));
    EXPECT_EQ(1, cache.getRef(sv)->get());
    EXPECT_EQ(1, cache.getRef("alpha")->get());
    EXPECT_TRUE(cache.contains(sv));
    EXPECT_TRUE(cache.contains("alpha"));  // literal

    cache.remove(sv);
    EXPECT_FALSE(cache.contains("alpha"));
}

TEST(LRUCacheTest, HeterogeneousStringSetView) {
    LRUCache<std::string, std::string> cache(3, 10);

    std::string_view key = "beta";
    cache.set(key, "value");  // set(std::string_view) must not need a Key arg
    EXPECT_EQ("value", cache.get(key).value());
    EXPECT_EQ("value", cache.get("beta").value());
}

TEST(LRUCacheTest, HeterogeneousIntegerKey) {
    LRUCache<int, int> cache(3, 10);
    cache.set(static_cast<int16_t>(1), 100);  // int16_t -> int via implicit conversion

    EXPECT_EQ(100, cache.get(static_cast<int16_t>(1)).value());
    EXPECT_TRUE(cache.contains(static_cast<int32_t>(1)));
    ASSERT_EQ(1, cache.size());
}

TEST(LRUCacheTest, HeterogeneousTake) {
    LRUCache<std::string, int> cache(2, 10);
    cache.set("k", 5);

    auto t = cache.take(std::string_view("k"));
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(5, t.value());
    EXPECT_FALSE(cache.contains("k"));
}

// Heterogeneous set() under eviction: the evictee's key pointer must stay valid
// when the index has been rehashing repeatedly.
TEST(LRUCacheTest, HeterogeneousSetEvictMany) {
    LRUCache<std::string, int> cache(67, 10);

    for (int i = 0; i < 2000; ++i) {
        const std::string key = "key-" + std::to_string(i);
        cache.set(std::string_view(key), i);
    }

    EXPECT_EQ(67, cache.size());
    for (int i = 2000 - 67; i < 2000; ++i) {
        const auto key = "key-" + std::to_string(i);
        std::string_view kv = key;
        ASSERT_TRUE(cache.contains(kv)) << key;
        EXPECT_EQ(i, cache.get(kv).value());
    }
}

// A std::string key only accepts lookups that convert to string_view: an int
// must not compile (rejected by the constraint). Wrapped in concepts because
// both gcc and clang treat a constrained-only candidate inside a bare requires
// as a hard error instead of a soft failure.
template <typename T, typename KeyArg>
concept cache_getable = requires(T& c, KeyArg k) { c.get(k); };
template <typename T, typename KeyArg>
concept cache_containable = requires(T& c, KeyArg k) { c.contains(k); };
static_assert(cache_getable<cc::LRUCache<std::string, int>, std::string_view>);
static_assert(cache_getable<cc::LRUCache<std::string, int>, const char*>);
static_assert(!cache_getable<cc::LRUCache<std::string, int>, int>);
static_assert(!cache_containable<cc::LRUCache<std::string, int>, int>);
static_assert(cache_getable<cc::LRUCache<int, int>, int16_t>);  // non-string: implicit conversion is fine

// The std::string interface must stay a fixed, non-template string_view
// signature: casting &get to a plain member pointer only compiles while the
// interface is not a heterogeneous template.
using string_cache = cc::LRUCache<std::string, int>;
using sv_get_type = std::optional<int> (string_cache::*)(std::string_view);
using sv_getptr_type = const int* (string_cache::*)(std::string_view);
using sv_contains_type = bool (string_cache::*)(std::string_view) const;
using sv_remove_type = void (string_cache::*)(std::string_view);
using sv_take_type = std::optional<int> (string_cache::*)(std::string_view);
static_assert(requires { static_cast<sv_get_type>(&string_cache::get); });
static_assert(requires { static_cast<sv_getptr_type>(&string_cache::getPtr); });
static_assert(requires { static_cast<sv_contains_type>(&string_cache::contains); });
static_assert(requires { static_cast<sv_remove_type>(&string_cache::remove); });
static_assert(requires { static_cast<sv_take_type>(&string_cache::take); });
