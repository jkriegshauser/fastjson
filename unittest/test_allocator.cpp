#include "doctest.h"

#include "fastjson.hpp"

TEST_CASE("fastjson_allocators heap_allocator")
{
    fastjson::default_allocator alloc;
    fastjson::byte* p = alloc.raw_heap_alloc(100);
    REQUIRE_NE(p, (fastjson::byte*)0);
    alloc.raw_heap_free(p);
}

TEST_CASE("fastjson_allocators static_pool")
{
    {
        struct test : public fastjson::static_pool<0>
        {
            typedef fastjson::static_pool<0> super;
            using super::static_pool_start;
            using super::static_pool_end;
        } local;
        CHECK_EQ(local.static_pool_start(), local.static_pool_end());
    }

    {
        struct test : public fastjson::static_pool<100>
        {
            typedef fastjson::static_pool<100> super;
            using super::static_pool_start;
            using super::static_pool_end;
        } local;
        CHECK_NE(local.static_pool_start(), local.static_pool_end());
        CHECK_GE(local.static_pool_end() - local.static_pool_start(), 100);
    }
}

TEST_CASE("fastjson_allocators memory_pool")
{
    {
        // Shoudn't fail even if static and dynamic pools are zero (force heap for all transactions)
        fastjson::memory_pool<fastjson::default_allocator, 0, 0> pool;
        CHECK_NE(pool.alloc(100), (fastjson::byte*)0);
        CHECK_NE(pool.alloc(1024), (fastjson::byte*)0);
        CHECK_NE(pool.alloc(99999), (fastjson::byte*)0);
    }

    {
        fastjson::memory_pool<> pool;
        CHECK_NE(pool.alloc(100), (fastjson::byte*)0);
        CHECK_NE(pool.alloc(1024), (fastjson::byte*)0);
        CHECK_NE(pool.alloc(99999), (fastjson::byte*)0);
    }
}