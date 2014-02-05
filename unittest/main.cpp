// #define FASTJSON_NO_EXCEPTIONS 1

#include "gtest/gtest.h"

template<class Ch> class dummy_iter
{
public:
    dummy_iter& operator = (Ch ch) { std::cout << char(ch); return *this; }
    dummy_iter& operator ++ (int) { return *this; }
    dummy_iter& operator * () { return *this; }
};

int main(int argc, char** argv)
{
    testing::InitGoogleTest(&argc, argv);
    int i = RUN_ALL_TESTS();
	assert(i == 0);
    return i;
}

#if 0
#include "../fastjson.hpp"
#include "../fastjson_print.hpp"

#include <stdio.h>
#include <iostream>

void foo()
{
    char buf[128];
    char* p = buf + 128;
    sprintf_s<128>(buf, "%f", 1.0);
    fastjson::internal::number_to_string(1.0, buf, p = buf + 128);
    fastjson::internal::number_to_string(2.0, buf, p = buf + 128);
    fastjson::internal::number_to_string(-1.0, buf, p = buf + 128);
    fastjson::internal::number_to_string(123.123, buf, p = buf + 128);
    fastjson::internal::number_to_string(123.123e7, buf, p = buf + 128);
    fastjson::internal::number_to_string(-123.123e-7, buf, p = buf + 128);
    fastjson::internal::number_to_string(123.123e12, buf, p = buf + 128);
    fastjson::internal::number_to_string(-123.123e-12, buf, p = buf + 128);
    fastjson::internal::number_to_string(1e-20, buf, p = buf + 128);
    double d = 0.0;
    fastjson::internal::number_to_string(0.0/d, buf, p = buf + 128); // NaN
    fastjson::internal::number_to_string(1e199 * -1e199, buf, p = buf + 128); // Inf

    int i = 0; (void)i;

    {
        char buf[] = "{\n"
                     "    \"Image\": {\n"
                     "        \"Width\":  800,\n"
                     "        \"Height\": 600,\n"
                     "        \"Title\":  \"View from 15th Floor\",\n"
                     "        \"Thumbnail\": {\n"
                     "            \"Url\":    \"http://www.example.com/image/481989943\",\n"
                     "            \"Height\": 125,\n"
                     "            \"Width\":  \"100\"\n"
                     "        },\n"
                     "        \"IDs\": [116, 943, 234, 38793]\n"
                     "    }\n"
                     "}\n";
        fastjson::json_document<> doc;
        doc.parse<0>(buf);
        fastjson::print(std::cout, doc);
    }

    {
        char buf[] = "{ \"Number\" : -123.123e-9 }";
        fastjson::json_document<> doc;
        doc.parse<0>(buf);
        assert(!doc.root()["Number"].is_null());
        assert(doc.root()["String"].is_null());

        double num = doc.root()["Number"].as_number();
        printf("%f  %g\n", num, num);
    }

    {
        char buf[] = "{ \"teststr\": \"hello \\uD834\\uDD1E world\" }";
        fastjson::json_document<> doc;
        doc.parse<0>(buf);
        fastjson::print(std::cout, doc);

        const char* s = doc.root()["teststr"].as_string();
        printf("%s\n", s);
    }

    {
        wchar_t buf[] = L"{ \"teststr\": \"hello \\uD834\\uDD1E world\" }";
        fastjson::json_document<wchar_t> doc;
        doc.parse<0>(buf);
        fastjson::print(std::wcout, doc);

        const wchar_t* s = doc.root()[L"teststr"].as_string();
        printf("%S\n", s);
    }

    {
        char cbuf[] = "{ \"teststr\": \"hello \\uD834\\uDD1E world\" }";
        unsigned int buf[sizeof(cbuf) + 1];

        for (int i = 0; i < (sizeof(cbuf) + 1); ++i)
        {
            buf[i] = cbuf[i];
        }

        fastjson::json_document<unsigned int> doc;
        doc.parse<0>(buf);
        dummy_iter<unsigned int> iter;
        fastjson::print(iter, doc);

        const unsigned int* s = doc.root().first_child()->as_string();
        static_cast<void>(s);
        printf("");
    }
}
#endif