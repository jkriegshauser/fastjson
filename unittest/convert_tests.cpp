#include "gtest/gtest.h"
#include "../fastjson.hpp"

#pragma warning (disable : 4127) // Conditional expression is constant

using namespace fastjson;

static const char text[] =
"{\n"
"    \"num1\":\t0.123556426,\n"
"    \"bool1\": true,\n"
"    \"null1\": null,\n"
"    \"test1\": \"hello world\",\n"
"    \"test2\": \"hello\\u0020world\",\n"
"    \"test3\": \"hello\\n\\tworld\",\n"
"    \"test4\": \"hello \\ud800\\udc00\",\n"
"    \"array1\": [ true, false, 0.1, \"hello\" ],\n"
"    \"obj1\": { \"sub1\": -123.456, \"bool2\":\tfalse }\n"
"}";

template<class ChIn, class ChOut, bool Swap> void do_test()
{
	ChIn copy[sizeof(text)];
	ChIn* out = copy; const char* in = text;
	while (*in)
	{
		*out = (ChIn)*in++;
		if (Swap)
		{
			unsigned char* p = (unsigned char*)out;
			if (sizeof(ChIn) == 2)
			{
				std::swap(p[0], p[1]);
			}
			else if (sizeof(ChIn) == 4)
			{
				std::swap(p[0], p[3]);
				std::swap(p[1], p[2]);
			}
		}
		++out;
	}
	*out = ChIn(0);

	json_document<ChOut> doc;
	try
	{
		doc.parse<0>(copy, (out-copy)*sizeof(ChIn));
	}
	catch (parse_error e)
	{
		ASSERT_FALSE(0) << "Unexpected parse error: " << e.what() << " at offset " << (std::size_t)(e.where<ChIn>() - copy);
	}
}

TEST(convert, utf8_to_8)
{
	do_test<char, char, false>();
}

TEST(convert, utf8_to_16)
{
	do_test<char, wchar_t, false>();
}

TEST(convert, utf8_to_32)
{
	do_test<char, unsigned, false>();
}

TEST(convert, utf16_to_8)
{
	do_test<wchar_t, char, false>();
	do_test<wchar_t, char, true>();
}

TEST(convert, utf16_to_16)
{
	do_test<wchar_t, wchar_t, false>();
	do_test<wchar_t, wchar_t, true>();
}

TEST(convert, utf16_to_32)
{
	do_test<wchar_t, unsigned, false>();
	do_test<wchar_t, unsigned, true>();
}

TEST(convert, utf32_to_8)
{
	do_test<unsigned, char, false>();
	do_test<unsigned, char, true>();
}

TEST(convert, utf32_to_16)
{
	do_test<unsigned, wchar_t, false>();
	do_test<unsigned, wchar_t, true>();
}

TEST(convert, utf32_to_32)
{
	do_test<unsigned, unsigned, false>();
	do_test<unsigned, unsigned, true>();
}