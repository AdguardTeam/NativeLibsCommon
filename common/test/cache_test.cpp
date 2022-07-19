#include <gtest/gtest.h>

#include "common/cache.h"
#include "common/clock.h"

static constexpr size_t CACHE_SIZE = 1000u;

class LruCacheTest : public ::testing::Test {
public:
    LruCacheTest()
            : m_cache(CACHE_SIZE) {
    }

protected:
    ag::LruCache<int, std::string> m_cache;

    void SetUp() override {
        for (size_t i = 0; i < CACHE_SIZE; ++i) {
            m_cache.insert(i, std::to_string(i));
            ASSERT_EQ(m_cache.size(), i + 1);
        }
    }

    void TearDown() override {
        m_cache.clear();
    }
};

TEST_F(LruCacheTest, Clear) {
    ASSERT_NE(m_cache.size(), 0u);
    m_cache.clear();
    ASSERT_EQ(m_cache.size(), 0u);
}

TEST_F(LruCacheTest, InsertAndGet) {
    // check that values were inserted
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        auto v = m_cache.get(i);
        ASSERT_TRUE(v);
        ASSERT_EQ(*v, std::to_string(i));
    }

    // check that cache grows no more
    for (size_t i = CACHE_SIZE; i < CACHE_SIZE * 2; ++i) {
        m_cache.insert(i, std::to_string(i));
        ASSERT_EQ(m_cache.size(), CACHE_SIZE);
    }

    // check that old values were displaced
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        ASSERT_FALSE(m_cache.get(i));
    }

    // check that new values were inserted
    for (size_t i = CACHE_SIZE; i < CACHE_SIZE * 2; ++i) {
        auto v = m_cache.get(i);
        ASSERT_TRUE(v);
        ASSERT_EQ(*v, std::to_string(i));
    }
}

TEST_F(LruCacheTest, Erase) {
    // erase every second entry
    for (size_t i = 0; i < CACHE_SIZE; i += 2) {
        m_cache.erase(i);
    }

    // check that size corresponds to the entries number
    ASSERT_EQ(m_cache.size(), CACHE_SIZE / 2);

    // check that erased entries were deleted and other ones are still in cache
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        if ((i % 2) == 0) {
            ASSERT_FALSE(m_cache.get(i));
        } else {
            ASSERT_TRUE(m_cache.get(i));
        }
    }
}

TEST_F(LruCacheTest, MakeLru) {
    auto acc = m_cache.get(CACHE_SIZE - 1);
    ASSERT_TRUE(acc);
    m_cache.make_lru(acc);
    m_cache.insert(1234, "1234");

    // Check that the MRU entry that has been made LRU has been pushed out of the cache
    ASSERT_FALSE(m_cache.get(CACHE_SIZE - 1));
    ASSERT_TRUE(m_cache.get(1234));
    ASSERT_EQ(CACHE_SIZE, m_cache.size());
}

TEST_F(LruCacheTest, DisplaceOrder) {
    // check that the least recent used values are being displaced first
    size_t j = 0;
    size_t i = CACHE_SIZE;
    for (; i < CACHE_SIZE * 2; ++i, ++j) {
        m_cache.insert(i, std::to_string(i));
        ASSERT_FALSE(m_cache.get(j));
    }
}

TEST_F(LruCacheTest, RefreshOnInsert) {
    // check that inserting existing key refreshes entry
    m_cache.insert(0, "42");
    m_cache.insert(CACHE_SIZE, std::to_string(CACHE_SIZE));

    ASSERT_TRUE(m_cache.get(0));
    ASSERT_EQ(*m_cache.get(0), "42");
    ASSERT_FALSE(m_cache.get(1));
    ASSERT_TRUE(m_cache.get(CACHE_SIZE));
}

TEST_F(LruCacheTest, RefreshOnGet) {
    // check that getting key refreshes entry
    ASSERT_TRUE(m_cache.get(0));
    m_cache.insert(CACHE_SIZE, std::to_string(CACHE_SIZE));

    ASSERT_TRUE(m_cache.get(0));
    ASSERT_FALSE(m_cache.get(1));
    ASSERT_TRUE(m_cache.get(CACHE_SIZE));
}

TEST_F(LruCacheTest, UpdateCapacity) {
    // check that changing capacity to lower m_value removes LRU entries
    m_cache.set_capacity(CACHE_SIZE / 2);
    ASSERT_EQ(m_cache.size(), CACHE_SIZE / 2);

    for (size_t i = 0; i < CACHE_SIZE / 2; ++i) {
        ASSERT_FALSE(m_cache.get(i)) << i << std::endl;
    }
}

static constexpr size_t TIMEOUT_MS = 1000u;

TEST(LruTimeoutCache, Timeout) {
    ag::LruTimeoutCache<int, std::string> cache(CACHE_SIZE, std::chrono::milliseconds(TIMEOUT_MS), false);
    cache.insert(1, "val");
    cache.insert(2, "val");
    cache.insert(3, "val");
    cache.insert(4, "val", std::chrono::milliseconds(TIMEOUT_MS * 4));
    cache.insert(5, "val", std::chrono::milliseconds(TIMEOUT_MS));

    ag::SteadyClock::add_time_shift(std::chrono::milliseconds(TIMEOUT_MS * 15 / 10));

    // call get to refresh entry and add new one
    ASSERT_NE(cache.get(3), nullptr);
    cache.insert(6, "val");

    ag::SteadyClock::add_time_shift(std::chrono::milliseconds(TIMEOUT_MS * 7 / 10));

    // check that timed out entries are removed after update
    cache.update();
    ASSERT_EQ(cache.get(1), nullptr);
    ASSERT_EQ(cache.get(2), nullptr);
    ASSERT_NE(cache.get(3), nullptr);
    ASSERT_NE(cache.get(4), nullptr);
    ASSERT_EQ(cache.get(5), nullptr);
    ASSERT_NE(cache.get(6), nullptr);
}

TEST(LruTimeoutCache, AutoUpdate) {
    ag::LruTimeoutCache<int, std::string> autoCacheInsert(CACHE_SIZE, std::chrono::milliseconds(TIMEOUT_MS), true);
    autoCacheInsert.insert(1, "val");

    ag::LruTimeoutCache<int, std::string> autoCacheGet(CACHE_SIZE, std::chrono::milliseconds(TIMEOUT_MS), true);
    autoCacheGet.insert(1, "val");

    ag::LruTimeoutCache<int, std::string> manCache(CACHE_SIZE, std::chrono::milliseconds(TIMEOUT_MS), false);
    manCache.insert(1, "val");

    ag::SteadyClock::add_time_shift(std::chrono::milliseconds(TIMEOUT_MS * 2));

    // check that cache performs update only if autoUpdate flag is set and does not otherwise
    autoCacheInsert.insert(2, "val");
    manCache.insert(2, "val");

    autoCacheGet.get(2);
    manCache.get(2);

    ASSERT_EQ(autoCacheInsert.get(1), nullptr);
    ASSERT_EQ(autoCacheGet.get(1), nullptr);
    ASSERT_NE(manCache.get(1), nullptr);
}

TEST(LruTimeoutCache, Works) {
    ag::LruTimeoutCache<size_t, std::string> cache(CACHE_SIZE, std::chrono::milliseconds(TIMEOUT_MS), true);
    // initialize, check size and `get` func
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        cache.insert(i, std::to_string(i));
        ASSERT_EQ(cache.size(), i + 1);
        auto v = cache.get(i);
        ASSERT_TRUE(v);
        ASSERT_EQ(*v, std::to_string(i));
    }

    // remove every second entry
    for (size_t i = 0; i < CACHE_SIZE; i += 2) {
        cache.erase(i);
    }

    // check that size corresponds to the entries number
    ASSERT_EQ(cache.size(), CACHE_SIZE / 2);

    // check that removed entries were deleted and other ones are still in cache
    for (size_t i = 0; i < CACHE_SIZE; ++i) {
        if ((i % 2) == 0) {
            ASSERT_FALSE(cache.get(i));
        } else {
            ASSERT_TRUE(cache.get(i));
        }
    }
}

TEST(LruTimeoutCache, DoesNotLeak) {
    using namespace std::chrono_literals;
    ag::LruTimeoutCache<int, std::string> cache(3, 1h, true);

    cache.insert(1, "a");
    cache.insert(2, "b");
    ag::SteadyClock::add_time_shift(90min);
    cache.insert(3, "c");

    // Check that timed out entries were deleted from everywhere
    ASSERT_EQ(1, cache.size());
    ASSERT_EQ(cache.size(), cache.m_timeout_keys.size());
    ASSERT_EQ(cache.m_timeout_keys.size(), cache.m_keys_timeout_iters.size());

    cache.insert(4, "d");
    cache.insert(5, "e");
    cache.insert(6, "f");
    cache.insert(7, "g");
    cache.insert(8, "h");

    // Check that evicted entries were deleted from everywhere
    ASSERT_EQ(3, cache.size());
    ASSERT_EQ(cache.size(), cache.m_timeout_keys.size());
    ASSERT_EQ(cache.m_timeout_keys.size(), cache.m_keys_timeout_iters.size());
}
