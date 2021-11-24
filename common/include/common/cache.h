#pragma once

#include <unordered_map>
#include <list>
#include "common/defs.h"
#include <mutex>

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
    size_t m_max_size;

    /** MRU gravitate to the front, LRU gravitate to the back */
    // This is guarded with its own mutex to allow clients to share access to the
    // "const" (from their point of view) functions, which actually modify this list
    mutable std::mutex m_guard;
    mutable std::list<Node> m_key_values;

    /** The main map */
    using MapType = std::unordered_map<Key, typename std::list<Node>::iterator>;
    mutable MapType m_mapped_values;

public:
    static constexpr size_t DEFAULT_CAPACITY = 128;

    /** A pointer-like object for accessing the cached m_value */
    struct Accessor {
        using ItType = typename MapType::iterator;
        ItType m_it{};

        Accessor() = default;

        explicit Accessor(ItType it) : m_it{it} {}

        explicit operator bool() const {
            return m_it != ItType{};
        }

        const Val &operator*() const {
            return m_it->second->second;
        }

        const Val *operator->() const {
            return &m_it->second->second;
        }
    };

    /**
     * Initialize a new cache
     * @param max_size cache capacity, 0 means default
     */
    explicit LruCache(size_t max_size = DEFAULT_CAPACITY) : m_max_size{max_size} {
        set_capacity(max_size);
    }

    /**
     * Insert a new key-value pair or update an existing one.
     * The new or updated entry will become most-recently-used.
     * @param k key
     * @param v value
     * @return false if an entry with this key already exists and was updated, or
     *         true if an entry with this key didn't exist.
     */
    bool insert(Key k, Val v) {
        auto i = m_mapped_values.find(k);
        if (i != m_mapped_values.end()) {
            m_guard.lock();
            m_key_values.splice(m_key_values.begin(), m_key_values, i->second);
            i->second = m_key_values.begin();
            m_guard.unlock();
            i->second->second = std::move(v);
            return false;
        } else {
            assert(m_max_size);
            std::unique_lock l(m_guard);
            if (m_key_values.size() == m_max_size) {
                m_mapped_values.erase(m_key_values.back().first);
                m_key_values.pop_back();
            }
            m_key_values.push_front(std::make_pair(k, std::move(v)));
            m_mapped_values.emplace(std::make_pair(std::move(k), m_key_values.begin()));
            return true;
        }
    }

    /**
     * Get the m_value associated with the given key.
     * The corresponding entry will become most-recently-used.
     * The returned pointer will only be valid until the next modification of the cache!
     * @param k the key
     * @return pointer to the found m_value, or
     *         nullptr if nothing was found
     */
    Accessor get(const Key &k) const {
        auto i = m_mapped_values.find(k);
        if (i != m_mapped_values.end()) {
            std::unique_lock l(m_guard);
            m_key_values.splice(m_key_values.begin(), m_key_values, i->second);
            return Accessor(i);
        } else {
            return {};
        }
    }

    /**
     * Forcibly make the specified cache entry least-recently-used
     * @param acc the Accessor for the cache entry to become LRU
     */
    void make_lru(Accessor acc) {
        std::unique_lock l(m_guard);
        m_key_values.splice(m_key_values.end(), m_key_values, acc.m_it->second);
    }

    /**
     * Delete the m_value with the given key from the cache
     * @param k the key
     */
    void erase(const Key &k) {
        auto i = m_mapped_values.find(k);
        if (i != m_mapped_values.end()) {
            std::unique_lock l(m_guard);
            m_key_values.erase(i->second);
            m_mapped_values.erase(i);
        }
    }

    /**
     * Clear the cache
     */
    void clear() {
        std::unique_lock l(m_guard);
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
        return m_max_size;
    }

    /**
     * Set cache capacity. If the new capacity is less than the current,
     * the least recenlty used entries are removed from the cache.
     * @param max_size new capacity, 0 means default capacity
     */
    void set_capacity(size_t max_size) {
        if (!max_size) {
            max_size = DEFAULT_CAPACITY;
        }
        if (max_size < size()) {
            size_t diff = size() - max_size;
            std::unique_lock l(m_guard);
            for (size_t i = 0; i < diff; i++) {
                m_mapped_values.erase(m_key_values.back().first);
                m_key_values.pop_back();
            }
        }
        m_max_size = max_size;
    }
};

} // namespace ag
