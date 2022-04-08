#include <gtest/gtest.h>

#include "common/cache.h"

static constexpr size_t CACHE_SIZE = 1000u;

class LruCacheTest : public ::testing::Test {
public:
    LruCacheTest() : m_cache(CACHE_SIZE) {}

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
