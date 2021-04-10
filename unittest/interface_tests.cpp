#include "doctest.h"
#include "../fastjson.hpp"

using namespace fastjson;

TEST_CASE("interface remove_all")
{
    json_document<> doc;

    auto obj = doc.allocate_object();
    obj->object_set("test", doc.allocate_bool_value(true));
    obj->remove_all();
}
