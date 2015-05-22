//
//  main.cpp
//  MMapJson
//
//  Created by Brian Howard on 4/8/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include <time.h>

#include <libgen.h>
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

#define btomb(bytes) (bytes / (double)(1024*1024))

using namespace ims;

static inline void log_debug( const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
}

const char* FILE_PATH = "test.json";
//const char* FILE_PATH = "citylots.json";
//const char* FILE_PATH = "magic.json";
//const char* FILE_PATH = "medium.json";

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
static void jmem_print( jmem_stats_t* mem )
{
    assert(mem);
    log_debug("[MEM][STRS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem->strs.used), btomb(mem->strs.reserved));
    log_debug("[MEM][NUMS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem->nums.used), btomb(mem->nums.reserved));
    log_debug("[MEM][OBJS ]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem->objs.used), btomb(mem->objs.reserved));
    log_debug("[MEM][ARRAY]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem->arrays.used), btomb(mem->arrays.reserved));
    log_debug("[MEM][TOTAL]: [used]: %0.2f MB [reserved]: %0.2f MB", btomb(mem->total.used), btomb(mem->total.reserved));
}

//------------------------------------------------------------------------------
int json_load_mmap(json_t* jsn, const char* path, jerr_t* err)
{
    int fd = open(path, O_RDONLY);
    if (!fd)
    {
        strncpy(err->msg, "could not read file", sizeof(err->msg));
        return 1;
    }

    struct stat st;
    int rt = fstat(fd, &st);
    if (rt != 0)
    {
        close(fd);
        strncpy(err->msg, "could not read file", sizeof(err->msg));
        return 1;
    }

    size_t len = st.st_size;
    if (len == 0)
    {
        close(fd);
        strncpy(err->msg, "file is empty", sizeof(err->msg));
        return 1;
    }

    void* mem = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    int status = json_load_buf(jsn, mem, len, err);
    munmap(mem, len);
    close(fd);

    return status;
}

//------------------------------------------------------------------------------
static void test_read()
{
    log_debug("starting test: '%s'", __func__);

    json_t* jsn = json_new();
    jerr_t err;

    // get the current directory
    char dbuf[255];
    strncpy(dbuf, __FILE__, sizeof(dbuf));
    char* dir = dirname(dbuf);

    // get the full path of our json file
    char buf[255];
    snprintf(buf, sizeof(buf), "%s/%s", dir, FILE_PATH);
    buf[sizeof(buf)-1] = '\0';

    if (json_load_path(jsn, buf, &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    jmem_stats_t mem = json_get_mem(jsn);
    jmem_print(&mem);

    assert(jsn);
    if (jsn->strmap.slen < 50)
    {
        json_print_file(jsn, JPRINT_PRETTY, stdout);
        putc('\n', stdout);
    }
    json_free(jsn); jsn = NULL;
}

//------------------------------------------------------------------------------
static void test_construction()
{
    log_debug("starting test: '%s'", __func__);

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

    assert( jval_is_true(jobj_find(root, "true")) );
    assert( jval_is_false(jobj_find(root, "false")) );
    assert( jval_is_bool(jobj_find(root, "true")) );
    assert( jval_is_bool(jobj_find(root, "false")) );
    assert( jval_is_nil(jobj_find(root, "nil")) );
    assert( jval_is_num(jobj_find(root, "num")) );
    assert( jval_is_str(jobj_find(root, "string")) );
    assert( jval_is_array(jobj_find(root, "array")) );
    assert( jval_is_obj(jobj_find(root, "obj")) );

    jarray_t array = jobj_find_array(root, "array");
    for ( size_t i = 0; i < jarray_len(array); i++ )
    {
        jval_t val = jarray_get(array, i);
        switch(i)
        {
            case 0:
                assert( jval_is_true(val) );
                break;

            case 1:
                assert( jval_is_false(val) );
                break;

            case 2:
                assert( jval_is_nil(val) );
                break;

            case 3:
                assert( jval_is_num(val) );
                break;

            case 4:
                assert( jval_is_obj(val) );
                break;

            case 5:
            {
                assert( jval_is_array(val) );
                jarray_t sub = json_get_array(jsn, val);
                for ( size_t n = 0; n < jarray_len(sub); n++ )
                {
                    jnum_t num = jarray_get_num(sub, n);
                    assert(num == n+1);
                }
                break;
            }
        }
    }

    json_print_file(jsn, JPRINT_PRETTY, stdout);
    putc('\n', stdout);
}

//------------------------------------------------------------------------------
static void test_construction_cpp()
{
    log_debug("starting test: '%s'", __func__);

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
    assert( it != root.end() && (*it).second.is_str());

//    auto it = root.find("true");
//    assert( it != root.end() && (*it).second.is());

//    auto it = root.find("string");
//    assert( it != root.end() && (*it).second.is_str());

    std::cout << jsn << std::endl;
}

typedef void (*test_func)(void);

//------------------------------------------------------------------------------
test_func TESTS[] =
{
    test_read,
    test_construction,
    test_construction_cpp,
};
static const size_t TEST_LEN = sizeof(TESTS)/sizeof(TESTS[0]);

//------------------------------------------------------------------------------
int main(int argc, const char * argv[])
{
    for (size_t i = 0; i < TEST_LEN; i++ )
    {
        log_debug("-----------------------------------------------");
        log_debug("completed in: %.2f secs", time_call(TESTS[i]));
    }
    return 0;
}
