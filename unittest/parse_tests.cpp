#include "gtest/gtest.h"

#include "fastjson.hpp"

#ifdef _MSC_VER
#pragma warning (disable : 4127) // Conditional expression is constant
#endif

using namespace fastjson;

static const char text[] =
"{\n"
"    \"num1\":  0.123556426,\n"
"    \"bool1\": true,\n"
"    \"null1\": null,\n"
"    \"test1\": \"hello world\",\n"
"    \"test2\": \"hello\\u0020world\",\n"
"    \"test3\": \"hello\\n\\tworld\",\n"
"    \"test4\": \"hello \\ud800\\udc00\"\n"
"}";

template<class Ch, int Flags, bool destructive> void test()
{
	Ch buffer[sizeof(text)];
	for (int i = 0; i < sizeof(text); ++i)
	{
		buffer[i] = (Ch)text[i];
	}

	json_document<Ch> doc;
	doc.parse<Flags>(buffer, sizeof(buffer));

	if (!destructive)
	{
		for (int i = 0; i < sizeof(text); ++i)
		{
			EXPECT_EQ(Ch(text[i]), buffer[i]) << "Flags(" << Flags << ") Buffer destroyed at index " << i << "\"" << text + i << "\"";
			if (Ch(text[i]) != buffer[i]) break;
		}			
	}
}

TEST(parse_tests, non_destructive_char)
{
	test<char, parse_non_destructive, false>();
	test<char, parse_non_destructive_nul, false>();
	test<char, parse_no_string_terminators, true>();
	test<char, parse_no_inline_translation, true>();
	test<wchar_t, parse_non_destructive, false>();
	test<wchar_t, parse_non_destructive_nul, false>();
	test<wchar_t, parse_no_string_terminators, true>();
	test<wchar_t, parse_no_inline_translation, true>();
	test<unsigned, parse_non_destructive, false>();
	test<unsigned, parse_non_destructive_nul, false>();
	test<unsigned, parse_no_string_terminators, true>();
	test<unsigned, parse_no_inline_translation, true>();
}