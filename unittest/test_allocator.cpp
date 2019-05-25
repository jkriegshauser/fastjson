#include "gtest/gtest.h"

#include "fastjson.hpp"

TEST(fastjson_allocators, heap_allocator)
{
    fastjson::default_allocator alloc;
    fastjson::byte* p = alloc.raw_heap_alloc(100);
    ASSERT_NE(p, (fastjson::byte*)0);
    alloc.raw_heap_free(p);
}

TEST(fastjson_allocators, static_pool)
{
    {
        struct test : public fastjson::static_pool<0>
        {
            typedef fastjson::static_pool<0> super;
            using super::static_pool_start;
            using super::static_pool_end;
        } local;
        EXPECT_EQ(local.static_pool_start(), local.static_pool_end());
    }

    {
        struct test : public fastjson::static_pool<100>
        {
            typedef fastjson::static_pool<100> super;
            using super::static_pool_start;
            using super::static_pool_end;
        } local;
        EXPECT_NE(local.static_pool_start(), local.static_pool_end());
        EXPECT_GE(local.static_pool_end() - local.static_pool_start(), 100);
    }
}

TEST(fastjson_allocators, memory_pool)
{
    {
        // Shoudn't fail even if static and dynamic pools are zero (force heap for all transactions)
        fastjson::memory_pool<fastjson::default_allocator, 0, 0> pool;
        EXPECT_NE(pool.alloc(100), (fastjson::byte*)0);
        EXPECT_NE(pool.alloc(1024), (fastjson::byte*)0);
        EXPECT_NE(pool.alloc(99999), (fastjson::byte*)0);
    }

    {
        fastjson::memory_pool<> pool;
        EXPECT_NE(pool.alloc(100), (fastjson::byte*)0);
        EXPECT_NE(pool.alloc(1024), (fastjson::byte*)0);
        EXPECT_NE(pool.alloc(99999), (fastjson::byte*)0);
    }
}