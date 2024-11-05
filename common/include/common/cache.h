#pragma once

#include <cassert>
#include <chrono>
#include <list>
#include <map>
#include <mutex>
#include <unordered_map>
#include <functional>

#include "common/clock.h"

class LruTimeoutCache_DoesNotLeak_Test;

namespace ag {

/**
 * Generic cache with least-recently-used eviction policy
 */
template <typename Key, typename Val>
class LruCache {
public:
    using Node = std::pair<const Key, Val>;

private:
    /** Cache capacity */
    size_t m_capacity = 0;

    /** MRU gravitate to the front, LRU gravitate to the back */
    // This is guarded with its own mutex to allow clients to share access to the
    // "const" (from their point of view) functions, which actually modify this list
    mutable std::mutex m_guard;
    mutable std::list<Node> m_key_values;

    /** The main map */
    using Map = std::unordered_map<Key, typename std::list<Node>::iterator>;
    mutable Map m_mapped_values;

public:
    /** A pointer-like object for accessing the cached value */
    class Accessor {
    private:
        using Iter = typename Map::iterator;
        Iter m_iter{};

    public:
        friend class LruCache;

        Accessor() = default;

        explicit Accessor(Iter it)
                : m_iter{it} {
        }

        explicit operator bool() const {
            return m_iter != Iter{};
        }

        const Val &operator*() const {
            return m_iter->second->second;
        }

        const Val *operator->() const {
            return &m_iter->second->second;
        }

        bool operator==(std::nullptr_t) const {
            return !(*this);
        }

        bool operator!=(std::nullptr_t) const {
            return bool(*this);
        }
    };

    static constexpr size_t DEFAULT_CAPACITY = 128;

    /**
     * Initialize a new cache
     * @param max_size cache capacity
     */
    explicit LruCache(size_t max_size = DEFAULT_CAPACITY) {
        set_capacity(max_size);
    }

    virtual ~LruCache() = default;

    LruCache(const LruCache &) = delete;
    LruCache &operator=(const LruCache &) = delete;
    LruCache(LruCache &&) = delete;
    LruCache &operator=(LruCache &&) = delete;

    /**
     * Insert a new key-value pair or update an existing one.
     * The new or updated entry will become most-recently-used.
     * @param k key
     * @param v value
     * @return false if an entry with this key already exists and was updated, or
     *         true if an entry with this key didn't exist.
     */
    virtual bool insert(Key k, Val v) {
        auto i = m_mapped_values.find(k);
        if (i != m_mapped_values.end()) {
            m_guard.lock();
            m_key_values.splice(m_key_values.begin(), m_key_values, i->second);
            i->second = m_key_values.begin();
            m_guard.unlock();
            i->second->second = std::move(v);
            return false;
        }

        assert(m_capacity > 0);
        std::scoped_lock l(m_guard);
        if (m_key_values.size() == m_capacity) {
            this->on_key_evicted(m_key_values.back().first);
            m_mapped_values.erase(m_key_values.back().first);
            m_key_values.pop_back();
        }
        m_key_values.push_front(std::make_pair(k, std::move(v)));
        m_mapped_values.emplace(std::make_pair(std::move(k), m_key_values.begin()));
        return true;
    }

    /**
     * Get the value associated with the given key.
     * The corresponding entry will become most-recently-used.
     * The returned pointer will only be valid until the next modification of the cache!
     * @param k the key
     * @return pointer to the found value, or
     *         nullptr if nothing was found
     */
    virtual Accessor get(const Key &k) const {
        auto i = m_mapped_values.find(k);
        if (i == m_mapped_values.end()) {
            return {};
        }

        std::scoped_lock l(m_guard);
        m_key_values.splice(m_key_values.begin(), m_key_values, i->second);
        return Accessor(i);
    }

    /**
     * Forcibly make the specified cache entry least-recently-used
     * @param acc the Accessor for the cache entry to become LRU
     */
    void make_lru(Accessor acc) {
        std::scoped_lock l(m_guard);
        m_key_values.splice(m_key_values.end(), m_key_values, acc.m_iter->second);
    }

    /**
     * Iterate over values in cache
     * @param f Callback to be called with key and value of each element in cache.
     *          If callback returns false, iteration will be terminated.
     */
    void iterate_values(const std::function<bool(const Key &k, const Val &v)> &f) {
        std::scoped_lock l(m_guard);
        for (const auto &i : m_key_values) {
            if (!f(i.first, i.second)) {
                return;
            }
        }
    }

    /**
     * Delete the value with the given key from the cache
     * @param k the key
     */
    virtual void erase(const Key &k) {
        auto i = m_mapped_values.find(k);
        if (i != m_mapped_values.end()) {
            std::scoped_lock l(m_guard);
            m_key_values.erase(i->second);
            m_mapped_values.erase(i);
        }
    }

    /**
     * Clear the cache
     */
    virtual void clear() {
        std::scoped_lock l(m_guard);
        m_key_values.clear();
        m_mapped_values.clear();
    }

    /**
     * @return current cache size
     */
    size_t size() const {
        return m_mapped_values.size();
    }

    /**
     * @return maximum cache size
     */
    size_t max_size() const {
        return m_capacity;
    }

    /**
     * Set cache capacity. If the new capacity is less than the current,
     * the least recenlty used entries are removed from the cache.
     * @param max_size new capacity, 0 means default capacity
     */
    void set_capacity(size_t max_size) {
        if (max_size < size()) {
            size_t diff = size() - max_size;
            std::scoped_lock l(m_guard);
            for (size_t i = 0; i < diff; i++) {
                m_mapped_values.erase(m_key_values.back().first);
                m_key_values.pop_back();
            }
        }
        m_capacity = max_size;
    }

protected:
    virtual void on_key_evicted(const Key &) {
        // noop
    }
};

// Least recently used cache with expiring entries
template <typename Key, typename Val>
class LruTimeoutCache : public LruCache<Key, Val> {
public:
    using Duration = ag::SteadyClock::duration;

private:
    using Clock = ag::SteadyClock;
    using TimePoint = ag::SteadyClock::time_point;
    struct TimeoutKey {
        Duration to;
        Key k;
    };

    /** Entries time out */
    const Duration TIMEOUT;
    /** If set, update will be performed on every cache access */
    const bool AUTO_UPDATE;

public:
    LruTimeoutCache(size_t s, Duration to, bool up = true)
            : LruCache<Key, Val>(s)
            , TIMEOUT(to)
            , AUTO_UPDATE(up) {
    }

    ~LruTimeoutCache() override = default;

    LruTimeoutCache(const LruTimeoutCache &) = delete;
    LruTimeoutCache &operator=(const LruTimeoutCache &) = delete;
    LruTimeoutCache(LruTimeoutCache &&) = delete;
    LruTimeoutCache &operator=(LruTimeoutCache &&) = delete;

    bool insert(Key k, Val v) override {
        return this->insert(k, std::move(v), TIMEOUT);
    }

    bool insert(Key k, Val v, Duration to) {
        if (this->AUTO_UPDATE) {
            this->update();
        }

        auto key_timeout = std::chrono::time_point_cast<Duration>(Clock::now() + to);
        auto i = m_timeout_keys.emplace(std::make_pair(key_timeout, TimeoutKey{to, k}));
        m_keys_timeout_iters.emplace(std::make_pair(k, std::move(i)));
        return LruCache<Key, Val>::insert(std::move(k), std::move(v));
    }

    typename LruCache<Key, Val>::Accessor get(const Key &k) const override {
        if (this->AUTO_UPDATE) {
            // Valid const cast: update() uses only mutable variables
            ((LruTimeoutCache *) this)->update();
        }

        typename LruCache<Key, Val>::Accessor v = LruCache<Key, Val>::get(k);
        if (v) {
            auto keyi = m_keys_timeout_iters.find(k);
            assert(keyi != m_keys_timeout_iters.end());
            auto key_timeout = std::chrono::time_point_cast<Duration>(Clock::now() + keyi->second->second.to);
            auto toi = m_timeout_keys.emplace(std::make_pair(key_timeout, std::move(keyi->second->second)));
            m_timeout_keys.erase(keyi->second);
            keyi->second = std::move(toi);
        }
        return v;
    }

    void clear() override {
        m_timeout_keys.clear();
        m_keys_timeout_iters.clear();
        LruCache<Key, Val>::clear();
    }

    void erase(const Key &k) override {
        if (this->AUTO_UPDATE) {
            this->update();
        }

        auto i = m_keys_timeout_iters.find(k);
        if (i != m_keys_timeout_iters.end()) {
            m_timeout_keys.erase(i->second);
            m_keys_timeout_iters.erase(i);
        }
        LruCache<Key, Val>::erase(k);
    }

    /**
     * @brief      Cleans timed out entries from the cache
     */
    void update() {
        auto current_time = std::chrono::time_point_cast<Duration>(Clock::now());
        auto first_up_to_date = m_timeout_keys.lower_bound(current_time);
        for (auto i = m_timeout_keys.begin(); i != first_up_to_date; i = m_timeout_keys.erase(i)) {
            LruCache<Key, Val>::erase(i->second.k);
            m_keys_timeout_iters.erase(i->second.k);
        }
    }

private:
    mutable std::multimap<TimePoint, TimeoutKey> m_timeout_keys;
    mutable std::unordered_map<Key, typename decltype(m_timeout_keys)::iterator> m_keys_timeout_iters;

    friend class ::LruTimeoutCache_DoesNotLeak_Test;

    void on_key_evicted(const Key &key) override {
        if (auto it = m_keys_timeout_iters.find(key); it != m_keys_timeout_iters.end()) {
            m_timeout_keys.erase(it->second);
            m_keys_timeout_iters.erase(it);
        }
    }
};

/**
 * A cache where each entry has a constant TTL,
 * starting from the time it was added to the cache.
 * Complexity of adding and erasing a single element is O(1).
 */
template<typename Key, typename Val>
class TimeoutCache {
private:
    struct Entry {
        Key key;
        Val value;
        SteadyClock::time_point expires;

        Entry(Key key, Val value, SteadyClock::time_point expires)
                : key(std::move(key)), value(std::move(value)), expires(expires) {}
    };

    std::list<Entry> m_entries; // Newer entries go to the front
    std::unordered_map<Key, typename decltype(m_entries)::iterator> m_entry_iter_by_key;

    const std::chrono::nanoseconds TIMEOUT;
    const size_t CAPACITY;

public:
    /**
     * @param timeout entry timeout
     * @param maxSize maximum number of entries, 0 means unlimited
     */
    explicit TimeoutCache(std::chrono::nanoseconds timeout, size_t maxSize = 0)
            : TIMEOUT{timeout}, CAPACITY{maxSize} {
    }

    void insert(Key key, Val value) {
        if (CAPACITY != 0 && m_entry_iter_by_key.size() == CAPACITY) {
            auto oldest_it = --m_entries.end();
            m_entry_iter_by_key.erase(oldest_it->key);
            m_entries.erase(oldest_it);
        }
        auto it = m_entry_iter_by_key.find(key);
        auto expires = SteadyClock::now() + TIMEOUT;
        if (it != m_entry_iter_by_key.end()) {
            auto &entry_it = it->second;
            entry_it->value = std::move(value);
            entry_it->expires = expires;
            m_entries.splice(m_entries.begin(), m_entries, entry_it);
        } else {
            m_entries.emplace_front(std::move(key), std::move(value), expires);
            m_entry_iter_by_key.emplace(m_entries.front().key, m_entries.begin());
        }
    }

    const Val *get(const Key &key) {
        auto it = m_entry_iter_by_key.find(key);
        if (it == m_entry_iter_by_key.end()) {
            return nullptr;
        }
        auto expires = it->second->expires;
        auto now = SteadyClock::now();
        if (now >= expires) {
            m_entries.erase(it->second);
            m_entry_iter_by_key.erase(it);
            return nullptr;
        }
        return &it->second->value;
    }

    void erase(const Key &key) {
        auto it = m_entry_iter_by_key.find(key);
        if (it == m_entry_iter_by_key.end()) {
            return;
        }
        m_entries.erase(it->second);
        m_entry_iter_by_key.erase(it);
    }

    void clear() {
        m_entries.clear();
        m_entry_iter_by_key.clear();
    }

    size_t size() const {
        return m_entries.size();
    }

    bool empty() const {
        return m_entries.empty();
    }
};

} // namespace ag
