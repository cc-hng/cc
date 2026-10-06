#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory_resource>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <cc/detail/intrusive_list.h>
#include <gsl/gsl>

namespace cc {

namespace detail {

/// Transparent hash for std::string keys: enables C++20 heterogeneous lookup
/// (find by std::string_view / const char* / string literal) without building a
/// temporary std::string. Both the map key and the lookup key are hashed as
/// std::string_view, so the values agree.
class TransparentStringHash {
public:
    using is_transparent = void;
    size_t operator()(std::string_view v) const noexcept { return std::hash<std::string_view>{}(v); }
};

/// See TransparentStringHash.
class TransparentStringEqual {
public:
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
};

template <typename Key>
using default_lru_lookup_hash =
    std::conditional_t<std::same_as<Key, std::string>, TransparentStringHash, std::hash<Key>>;
template <typename Key>
using default_lru_lookup_equal =
    std::conditional_t<std::same_as<Key, std::string>, TransparentStringEqual, std::equal_to<Key>>;

/// A Lookup key is usable if the map's hash can take it (transparent hash: it
/// must convert to std::string_view; else implicit conversion to Key) and the
/// equality functor can compare two of them. This checks callability directly
/// rather than relying on find() overload resolution, which only inspects
/// is_transparent and would let a bad Lookup through until instantiation.
template <typename Key, typename Lookup>
concept lookupable = std::invocable<const default_lru_lookup_hash<Key>&, const Lookup&> &&
                     std::invocable<const default_lru_lookup_equal<Key>&, const Lookup&, const Lookup&>;

/// Constraint for the heterogeneous non-string lookup interface. A std::string
/// Key gets a fixed std::string_view interface instead (see LRUCache).
template <typename Key, typename KeyArg>
concept hetero_lookupable = !std::same_as<Key, std::string> && lookupable<Key, KeyArg>;

}  // namespace detail

/// Fixed-capacity LRU cache with a per-entry TTL (second granularity).
///
/// NOT thread-safe: the node pool and the index are unsynchronized. Guard a
/// shared cache with a mutex in the caller.
///
/// Key and Value need not be default-constructible.
///
/// Lookups are heterogeneous, with a fixed interface for std::string Keys: get,
/// getPtr, getRef, take, contains and remove then take exactly std::string_view
/// (convertible from const char* and string literals), so no temporary string
/// is ever materialized by a lookup. For any other Key the lookup argument K
/// may differ from Key via implicit conversion (e.g. int16_t for int). The
/// hash/equal policy enabling this is internal; it is not exposed. set() always
/// takes the Key type (implicitly convertible arguments are absorbed), since it
/// must materialize an owned Key to store anyway.
template <typename Key, typename Value>
class LRUCache {
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    struct item_t {
        Value value;
        time_point expiry;
        const Key* key_ptr;  // non-owning; points at the index's own key (stable across rehash)
        item_t* prev = nullptr;
        item_t* next = nullptr;
    };
    using list_type = intrusive_list<item_t, &item_t::next, &item_t::prev>;
    // The node does not own a Key: it points at the index's copy. unordered_map
    // keeps pointers/references to elements valid across rehash (only iterators
    // dangle), so this needs no reserve() or rehash invariant. evict() hashes
    // through this pointer before erasing the corresponding map element.
    using map_type = std::unordered_map<Key, item_t*, detail::default_lru_lookup_hash<Key>,
                                        detail::default_lru_lookup_equal<Key>>;  // key stored once, per entry

    const size_t max_items_;
    const size_t ttl_seconds_;
    list_type list_;
    map_type items_;
    std::pmr::unsynchronized_pool_resource pool_;  // per-cache node pool for item_t, freed with the instance

public:
    /// TTL long enough to outlive any realistic process (~68 years).
    static constexpr size_t NO_EXPIRY = std::numeric_limits<int32_t>::max();

    LRUCache(size_t max_items, size_t ttl_seconds) : max_items_(max_items), ttl_seconds_(ttl_seconds) {
        Ensures(max_items > 0);
    }
    ~LRUCache() { clear(); }

    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
    LRUCache(LRUCache&&) = delete;
    LRUCache& operator=(LRUCache&&) = delete;

    template <typename KeyArg, typename ValueArg>
    void set(KeyArg&& k, ValueArg&& v) {
        set(std::forward<KeyArg>(k), std::forward<ValueArg>(v), ttl_seconds_);
    }

    template <typename KeyArg, typename ValueArg>
    void set(KeyArg&& k, ValueArg&& v, size_t ttl_seconds) {
        const auto expiry = clock::now() + std::chrono::seconds(ttl_seconds);
        // Put the key in the index first, then build the node from it. Key and
        // implicitly-convertible KeyArgs (e.g. int16_t, const char*) make
        // try_emplace build the map key in place; std::string_view does not
        // convert implicitly (its string ctor is explicit), so the cache
        // materializes an owned Key first.
        auto insert_result = [&] {
            if constexpr (std::convertible_to<KeyArg&&, Key>) {
                return items_.try_emplace(std::forward<KeyArg>(k), nullptr);
            } else {
                return items_.try_emplace(Key{std::forward<KeyArg>(k)}, nullptr);
            }
        }();
        auto [it, inserted] = insert_result;
        if (!inserted) [[likely]] {
            auto* item = it->second;
            item->value = std::forward<ValueArg>(v);
            item->expiry = expiry;
            moveToEnd(item);
            return;
        }
        try {
            if (size() > max_items_) {
                evict();  // the placeholder is not in the list yet, so it cannot evict itself
            }
            auto* item = createItem(&it->first, std::forward<ValueArg>(v), expiry);
            it->second = item;
            list_.push_back(item);
        } catch (...) {
            items_.erase(it);  // drop the placeholder; no node was created
            throw;
        }
    }

    /// Key == std::string: the lookup interface is fixed to std::string_view
    /// (string literals and const char* convert implicitly).
    const Value* getPtr(std::string_view key)
        requires std::same_as<Key, std::string>
    {
        return getPtrImpl(key);
    }

    /// Other keys: the lookup argument K may differ from Key via implicit
    /// conversion to Key, e.g. int16_t for an int Key.
    template <typename KeyArg>
    const Value* getPtr(const KeyArg& key)
        requires detail::hetero_lookupable<Key, KeyArg>
    {
        return getPtrImpl(key);
    }

    /// Returns a reference to the cached value (zero copy), or nullopt if missing / expired.
    /// Moves the item to the most-recent end (back) of the LRU list.
    ///
    /// WARNING: any later set()/remove()/clear() may evict or replace the item,
    /// invalidating the returned reference. Use it promptly; copy if you must keep it.
    std::optional<std::reference_wrapper<const Value>> getRef(std::string_view key)
        requires std::same_as<Key, std::string>
    {
        auto* p = getPtr(key);
        if (p != nullptr) {
            return *p;
        }
        return std::nullopt;
    }

    template <typename KeyArg>
    std::optional<std::reference_wrapper<const Value>> getRef(const KeyArg& key)
        requires detail::hetero_lookupable<Key, KeyArg>
    {
        auto* p = getPtr(key);
        if (p != nullptr) {
            return *p;
        }
        return std::nullopt;
    }

    /// Returns a copy of the cached value. Requires a copyable Value; use
    /// getRef()/getPtr() for zero-copy access, or take() for move-only values.
    inline std::optional<Value> get(std::string_view key)
        requires std::same_as<Key, std::string> && std::copy_constructible<Value>
    {
        auto p = getPtr(key);
        return p != nullptr ? std::optional<Value>(*p) : std::nullopt;
    }

    template <typename KeyArg>
    inline std::optional<Value> get(const KeyArg& key)
        requires std::copy_constructible<Value> && detail::hetero_lookupable<Key, KeyArg>
    {
        auto p = getPtr(key);
        return p != nullptr ? std::optional<Value>(*p) : std::nullopt;
    }

    /// Moves the value out and removes the entry. Works for move-only Value.
    std::optional<Value> take(std::string_view key)
        requires std::same_as<Key, std::string>
    {
        return takeImpl(key);
    }

    template <typename KeyArg>
    std::optional<Value> take(const KeyArg& key)
        requires detail::hetero_lookupable<Key, KeyArg>
    {
        return takeImpl(key);
    }

    /// Checks whether a key exists in the cache AND is not yet expired.
    /// Does NOT remove expired items (that happens on next get/set).
    inline bool contains(std::string_view key) const
        requires std::same_as<Key, std::string>
    {
        return containsImpl(key);
    }

    template <typename KeyArg>
    inline bool contains(const KeyArg& key) const
        requires detail::hetero_lookupable<Key, KeyArg>
    {
        return containsImpl(key);
    }

    /// Deliberate: eviction is pure LRU and ignores expiry. Expired-but-unaccessed
    /// items are reclaimed lazily: when they age to the front, or on next access.
    /// Reclaiming them earlier would need a time-ordered index or a full scan;
    /// not worth it for a cache.
    void evict() {
        auto front = list_.front();
        if (front != nullptr) [[likely]] {
            auto iter = items_.find(*front->key_ptr);
            list_.remove(front);
            if (iter != items_.end()) {
                items_.erase(iter);
            }
            releaseItem(front);
        }
    }

    void remove(std::string_view key)
        requires std::same_as<Key, std::string>
    {
        removeImpl(key);
    }

    template <typename KeyArg>
    void remove(const KeyArg& key)
        requires detail::hetero_lookupable<Key, KeyArg>
    {
        removeImpl(key);
    }

    void clear() noexcept {
        auto it = list_.front();
        while (it != nullptr) [[likely]] {
            auto next = it->next;
            releaseItem(it);
            it = next;
        }
        list_.clear();
        items_.clear();
    }

    inline size_t size() const noexcept { return items_.size(); }

private:
    LRUCache() = delete;

    // Shared bodies for the dual public interface (fixed string_view for
    // std::string Keys, heterogeneous K otherwise).
    template <typename KeyArg>
    const Value* getPtrImpl(const KeyArg& key) {
        auto iter = items_.find(key);
        if (iter != items_.end()) {
            auto item = iter->second;
            if (item->expiry < clock::now()) {
                list_.remove(item);
                items_.erase(iter);
                releaseItem(item);
                return nullptr;
            }
            moveToEnd(item);
            return &item->value;
        }
        return nullptr;
    }

    template <typename KeyArg>
    std::optional<Value> takeImpl(const KeyArg& key) {
        auto iter = items_.find(key);
        if (iter == items_.end()) {
            return std::nullopt;
        }
        auto* item = iter->second;
        const bool expired = item->expiry < clock::now();
        std::optional<Value> out;
        if (!expired) {
            out.emplace(std::move(item->value));
        }
        list_.remove(item);
        items_.erase(iter);
        releaseItem(item);
        return out;
    }

    template <typename KeyArg>
    inline bool containsImpl(const KeyArg& key) const {
        auto iter = items_.find(key);
        return iter != items_.end() && iter->second->expiry >= clock::now();
    }

    template <typename KeyArg>
    void removeImpl(const KeyArg& key) {
        auto iter = items_.find(key);
        if (iter != items_.end()) [[likely]] {
            auto item = iter->second;
            list_.remove(item);
            items_.erase(iter);
            releaseItem(item);
        }
    }

    template <typename ValueArg>
    item_t* createItem(const Key* key_ptr, ValueArg&& v, time_point expiry) {
        auto* p = static_cast<item_t*>(pool_.allocate(sizeof(item_t), alignof(item_t)));
        try {
            // Aggregate init, so Value needs no default constructor.
            ::new (p) item_t{std::forward<ValueArg>(v), expiry, key_ptr};
        } catch (...) {
            pool_.deallocate(p, sizeof(item_t), alignof(item_t));
            throw;
        }
        return p;
    }

    void releaseItem(item_t* item) noexcept {
        if (item != nullptr) [[likely]] {
            item->~item_t();
            pool_.deallocate(item, sizeof(item_t), alignof(item_t));
        }
    }

    void moveToEnd(item_t* item) noexcept {
        if (list_.back() != item) [[likely]] {
            list_.remove(item);
            list_.push_back(item);
        }
    }
};

}  // namespace cc
