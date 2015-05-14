//
//  main.cpp
//  MMapJson
//
//  Created by Brian Howard on 4/8/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include <time.h>

extern "C"
{
    #include "json2.h"
}

#include "json.h"
#include "json.hpp"

#include <math.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <list>
#include <iostream>

using namespace ims;

#define log_debug(FMT,...) printf(FMT "\n", ## __VA_ARGS__ )

//const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/test.json";
const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";
//const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/medium.json";

//------------------------------------------------------------------------------
template < typename F >
double time_call( F func )
{
    static constexpr double CLOCKS_TO_SECS = 1.0 / (double)CLOCKS_PER_SEC;
    clock_t start = clock();
    func();
    return (clock() - start) * CLOCKS_TO_SECS;
}

//------------------------------------------------------------------------------
static void test_read()
{
    json_t* jsn = json_new();
    const char *err = json_load_path(jsn, path);
    if (err)
    {
        puts(err);
        abort();
    }

    assert(jsn);
    if (jsn->strmap.slen < 50)
    {
        json_print(jsn, stdout);
    }
    json_free(jsn); jsn = NULL;
}

//------------------------------------------------------------------------------
static void test_construction()
{
    json_t* jsn = json_new();
    jobj_t root = json_root(jsn);
    {
        jobj_add_bool(root, "true", true);
        jobj_add_bool(root, "false", false);
        jobj_add_nil(root, "nil");
        jobj_add_num(root, "num", 3.14);
        jobj_add_str(root, "string", "string");
        jobj_t child = jobj_add_obj(root, "obj");
        {
            jobj_add_str(child, "key", "child");
        }
        jarray_t array = jobj_add_array(root, "array");
        {
            jarray_add_bool(array, true);
            jarray_add_bool(array, false);
            jarray_add_nil(array);
            jarray_add_num(array, 5.5);
            jobj_t child2 = jarray_add_obj(array);
            {
                jobj_add_bool(child2, "true", true);
            }
            jarray_t sarray = jarray_add_array(array);
            {
                jarray_add_num(sarray, 1);
                jarray_add_num(sarray, 2);
                jarray_add_num(sarray, 3);
            }
        }
    }

    json_print(jsn, stdout);
}

//------------------------------------------------------------------------------
static void test_construction_cpp()
{
    ims::json jsn;
    auto root = jsn.root();
    {
        root["true"] = true;
        root["false"] = false;
        root["nil"] = nullptr;
        root["num"] = 3.14;
        root["string"] = "string";
        root["child"].add_obj("key", "child");
        root["array"] = val::array
        {
            true,
            false,
            nullptr,
            5.5,
            val::obj
            {
                {"true", true},
                {"num", 5}
            },
            val::array{1,2,3}
        };
    }

    auto it = root.find("string");
    if (it != root.end())
    {
        std::cout << (*it).first << ": " << (*it).second << std::endl;
    }

    std::cout << jsn << std::endl;
}

//------------------------------------------------------------------------------
int main(int argc, const char * argv[])
{
    log_debug("starting test 'test_read'");
    double t1 = time_call(test_read);
    log_debug("completed in: %f secs", t1);

//    log_debug("starting test 'read c++'");
//    double t4 = time_call([]()
//    {
//        auto j = ims::json::from_file(path);
////        auto j = ::json::load_file2(path);
//    });
//    log_debug("completed in: %f secs", t4);


//    log_debug("starting 'test_construction'");
//    double t2 = time_call(test_construction);
//    log_debug("completed in: %f secs", t2);
//
    log_debug("starting 'test_construction_cpp'");
    double t3 = time_call(test_construction_cpp);
    log_debug("completed in: %f secs", t3);

//    auto j2 = ims::json::from_file("/Users/bghoward/Projects/MMapJson/MMapJson/test.json");
//    if (j2)
//    {
//        std::cout << j2 << std::endl;
//    }
//
//    ims::json jsn;
//    obj root = jsn.root();
//    root["key"] = "string";
//    root["num"] = 1.25;
//    root["num"] = 10;
//    root["true"] = true;
//    root["false"] = false;
//    root["array"].add_array(1, 2, 3, 4, 5, "blah");
//    root["obj"].add_obj("a", 1, "b", 2);
//    std::cout << jsn << std::endl;

    return 0;
}
