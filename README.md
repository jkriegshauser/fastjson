fastjson
by Joshua M. Kriegshauser
========

A fast json parser written in C++
Inspired by rapidxml (http://rapidxml.sourceforge.net/)

Getting Started
===============

A simple example:

#include <iostream>
#include <fastjson.hpp>

int main(int argc, char** argv)
{
    const char* json = "{ \"name\": \"hello world!\" }";
    
    fastjson::json_document<> doc;
    try
    {
        doc.parse<0>((void*)json, std::size_t(-1), fastjson::utf8);
        std::cout << "name: " << doc.root()["name"] << std::endl;
    }
    catch (fastjson::parse_error e)
    {
    }
    
    return 0;
}
