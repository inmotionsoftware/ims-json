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

#include <math.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

using namespace ims;

#define log_debug(FMT,...) printf(FMT "\n", ## __VA_ARGS__ )

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
//    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/test.json";
    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";
//    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/medium.json";

    json_t* jsn = json_load_file(path);
    assert(jsn);
//    json_print(jsn, stdout);
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
int main(int argc, const char * argv[])
{
    log_debug("starting test 'test_read'");
    double t1 = time_call(test_read);
    log_debug("completed in: %f secs", t1);

    log_debug("starting 'test_construction'");
    double t2 = time_call(test_construction);
    log_debug("completed in: %f secs", t2);

    return 0;
}
