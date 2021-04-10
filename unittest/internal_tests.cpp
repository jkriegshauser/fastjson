#include "doctest.h"
#include "../fastjson.hpp"

using namespace fastjson;
using namespace fastjson::internal;

TEST_CASE("internal lookup_tables")
{
    REQUIRE_EQ(256, sizeof(lookup_tables<0>::lookup_whitespace)/sizeof(lookup_tables<0>::lookup_whitespace[0]));
    for (int i = 0; i < 256; ++i)
    {
        switch (i)
        {
        case '\t':
        case '\r':
        case '\n':
        case ' ':
            CHECK(lookup_tables<0>::lookup_whitespace[i]);
            break;

        default:
            CHECK_FALSE(lookup_tables<0>::lookup_whitespace[i]);
        }
    }

    REQUIRE_EQ(256, sizeof(lookup_tables<0>::lookup_digit)/sizeof(lookup_tables<0>::lookup_digit[0]));
    for (int i = 0; i < 256; ++i)
    {
        switch (i)
        {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            CHECK(lookup_tables<0>::lookup_digit[i]);
            break;

        default:
            CHECK_FALSE(lookup_tables<0>::lookup_digit[i]);
        }
    }

    REQUIRE_EQ(10, sizeof(lookup_tables<0>::lookup_double)/sizeof(lookup_tables<0>::lookup_double[0]));
    for (int i = 0; i < 10; ++i)
    {
        CHECK_EQ((double)i, lookup_tables<0>::lookup_double[i]);
    }

    REQUIRE_EQ(16, sizeof(lookup_tables<0>::lookup_hexchar)/sizeof(lookup_tables<0>::lookup_hexchar[0]));
    for (int i = 0; i < 16; ++i)
    {
        const char c[] = { '0', 'x', lookup_tables<0>::lookup_hexchar[i], '\0' };
        CHECK_EQ(i, strtol(c, 0, 16));
    }
}

template<class Ch> struct emptystr_test
{
    void test()
    {
        const Ch* p = emptystr<Ch>();
        REQUIRE(p != 0);            // pointer must be valid
        CHECK_EQ(p, emptystr<Ch>());    // function must always return the same
        CHECK_EQ(Ch(0), *p);           // pointer must be an empty string
    }
};

TEST_CASE("internal emptystr")
{
    emptystr_test<char>().test();
    emptystr_test<wchar_t>().test();
    emptystr_test<wint_t>().test();
    emptystr_test<unsigned>().test();
}

template<class Ch, int size> struct string_test
{
    Ch str[size];
    string_test(const char* in_str)
    {
        int i = 0;
        while (*in_str && i < size)
        {
            str[i++] = Ch(*in_str++);
        }
        str[size-1] = Ch(0);
    }
    template <class func> void test(func f)
    {
        const Ch* p = f();
        REQUIRE(p != 0);        // pointer must be valid
        CHECK_EQ(p, f());       // function must always return the same
        for (int i = 0; i < size; ++i)
        {
            CHECK_EQ(str[i], p[i]);   // string must match
        }
    }
};

TEST_CASE("internal strings")
{
    string_test<char, 5>("null").test(nullstr<char>);
    string_test<wchar_t, 5>("null").test(nullstr<wchar_t>);
    string_test<wint_t, 5>("null").test(nullstr<wint_t>);
    string_test<unsigned, 5>("null").test(nullstr<unsigned>);

    string_test<char, 5>("true").test(truestr<char>);
    string_test<wchar_t, 5>("true").test(truestr<wchar_t>);
    string_test<wint_t, 5>("true").test(truestr<wint_t>);
    string_test<unsigned, 5>("true").test(truestr<unsigned>);

    string_test<char, 6>("false").test(falsestr<char>);
    string_test<wchar_t, 6>("false").test(falsestr<wchar_t>);
    string_test<wint_t, 6>("false").test(falsestr<wint_t>);
    string_test<unsigned, 6>("false").test(falsestr<unsigned>);
}

template<class Ch> struct nullval_test
{
    void test()
    {
        json_value<Ch>* p = json_value<Ch>::null();
        REQUIRE(p != 0);            // pointer must be valid
        CHECK_EQ(p, json_value<Ch>::null());    // function must always return the same
        CHECK(p->is_null());      // must be a null value
        bool const isEmpty = p->name() == p->nameend();
        CHECK(isEmpty); // name must be empty
        CHECK_EQ(p->next_sibling(), nullptr); // shouldn't ever have siblings
        const Ch* str = p->as_string();
        bool const match = (str[0] == Ch('n') && str[1] == Ch('u') && str[2] == Ch('l') && str[3] == Ch('l'));
        CHECK(match);
    }
};

TEST_CASE("internal nullval")
{
    nullval_test<char>().test();
    nullval_test<wchar_t>().test();
    nullval_test<wint_t>().test();
    nullval_test<unsigned>().test();
}
