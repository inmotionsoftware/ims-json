/*!
    @file main.cpp
    @author Brian Howard
    @copyright
    Copyright (c) 2015 InMotion Software, LLC.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#if defined(NDEBUG)
    #undef NDEBUG
    #define NDEBUG 0
#endif

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

#define STRINGIFY(...) #__VA_ARGS__
#define LOG_FUNC() log_debug("starting test: '%s'", __func__)

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
static void get_fullpath( const char* path, char* buf, size_t blen )
{
    // get the current directory
    char dbuf[255];
    strncpy(dbuf, __FILE__, sizeof(dbuf));
    char* dir = dirname(dbuf);

    // get the full path of our json file
    snprintf(buf, blen, "%s/%s", dir, path);
    buf[blen-1] = '\0';
}

//------------------------------------------------------------------------------
static void test_numbers()
{
    LOG_FUNC();
    std::string jstr;

    std::vector<std::string> nums =
    {
        "12345678901234567890123456789012345678901234567890123456789012345678901234567890",
        "1.5e-5",
        "1.9e6",
        "1000000000",
        "1.84594059860598e307",
        "7.094809809e307",
        "1.2094850986e-308",
        "0",
        "-0",
        "1",
        "134217727",
        "-134217727",
        "123.456789",
        "98",
        "-12",
        "1e0",
        "209098.098098098e-3",
        "1e-500",
        "1",
        "-3.098098e6"
    };

    jstr += "[";
    for ( auto it = nums.begin(), end = nums.end(); it != end; ++it )
    {
        jstr += (*it);
        jstr += ",";
    }
    jstr.pop_back();
    jstr += "]";

//    std::cout << jstr << std::endl;

    jerr_t err;

    json_t jsn;
    json_init(&jsn);
    if (json_load_buf(&jsn, jstr.c_str(), jstr.size(), &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

//    json_print_file(&jsn, JPRINT_PRETTY, stdout);

    jarray_t array = json_root_array(&jsn);
    for ( size_t i = 0; i < nums.size(); i++ )
    {
        const std::string& str = nums[i];
        double d = strtod(str.c_str(), NULL);
        jnum_t n = jarray_get_num(array, i);
        assert(n == d);
    }

    json_destroy(&jsn);
}

//------------------------------------------------------------------------------
static void test_reload()
{
    LOG_FUNC();

    static const char KEY[] = "the-thing-that-should-not-be";

    json_t* jsn = json_new();
    jobj_add_str(json_root_obj(jsn), KEY, "cthulu");

    const char* json_doc = STRINGIFY
    ({
        "string":"str",
        "num": 1
    });

    jerr_t err;
    if (json_load_str(jsn, json_doc, &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    assert(!jobj_contains_key(json_root_obj(jsn), KEY));
    json_free(jsn); jsn = NULL;
}

//------------------------------------------------------------------------------
static void test_read()
{
    LOG_FUNC();

    json_t* jsn = json_new();
    jerr_t err;

    char buf[255];
    get_fullpath(FILE_PATH, buf, sizeof(buf));

    if (json_load_path(jsn, buf, &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    assert(jsn);
    if (jsn->strmap.slen < 50)
    {
        json_print_file(jsn, JPRINT_PRETTY, stdout);
        putc('\n', stdout);
    }
    json_free(jsn); jsn = NULL;
}

//------------------------------------------------------------------------------
static void test_compare()
{
    LOG_FUNC();

    jerr_t err;

    char buf[255];
    get_fullpath("test.json", buf, sizeof(buf));

    json_t j1;
    json_init(&j1);
    if (json_load_path(&j1, buf, &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    json_t j2;
    json_init(&j2);
    if (json_load_path(&j2, buf, &err) != 0)
    {
        jerr_fprint(stderr, &err);
        exit(EXIT_FAILURE);
    }

    assert(json_compare(&j1, &j2) == 0);

    jobj_add_str(json_root_obj(&j1), "not-equal", "anymore");

    assert(json_compare(&j1, &j2) != 0);

    json_destroy(&j1);
    json_destroy(&j2);
}

//------------------------------------------------------------------------------
static void test_construction()
{
    LOG_FUNC();

    json_t* jsn = json_new();
    jobj_t root = json_root_obj(jsn);
    {
        jobj_add_bool(root, "true", true);
        jobj_add_bool(root, "false", false);
        jobj_add_nil(root, "nil");
        jobj_add_int(root, "int", 1);
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

    assert( jval_is_int(jobj_find(root, "int")) );
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
//static void test_cpp_val( const obj& root, const std::string& key, const const_val& val )
//{
//    auto it = root.find(key);
//    assert(it != root.end());
//    assert((*it).second == val);
//}

//------------------------------------------------------------------------------
static void test_construction_cpp()
{
    LOG_FUNC();

    ims::json jsn;
    auto root = jsn.root_obj();
    {
        root["true"] = true;
        root["false"] = false;
        root["nil"] = nullptr;
        root["num"] = 3.14;
        root["int"] = 1;
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

//    test_cpp_val(root, "true", true);
//    test_cpp_val(root, "false", false);

    auto it = root.find("string");
    assert( it != root.end() && (*it).second.is_str());
    std::cout << jsn << std::endl;
}

typedef void (*test_func)(void);

//------------------------------------------------------------------------------
test_func TESTS[] =
{
    test_read,
    test_compare,
    test_construction,
    test_construction_cpp,
    test_reload,
    test_numbers
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
