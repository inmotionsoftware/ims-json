//
//  main.cpp
//  MMapJson
//
//  Created by Brian Howard on 4/8/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include <time.h>
#include "json.h"
#include "json2.h"

//------------------------------------------------------------------------------
int main(int argc, const char * argv[])
{
////    const char* path = "/Users/bghoward/Projects/Emblazed-NextGen/IMSFoundation/sample.json";
//
//    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";
////    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/test.json";
////    const char* path = "/Users/bghoward/Dropbox/Project Sluice/external data/gz_2010_us_050_00_500k.json";
////    const char* path = "/Users/bghoward/Downloads/Newtonsoft.Json-6.0.8/Src/Newtonsoft.Json.Tests/large.json";
//
//    // mmap a file
//    // parse into json structure
//
//    clock_t start = clock();
//    json doc = json::load_file(path);
//    clock_t diff = clock() - start;
//    printf("completed in: %f secs\n", diff / (double)CLOCKS_PER_SEC);
//
////    std::cout << doc << std::endl;

    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";

    printf("starting test\n");
    clock_t start = clock();
    json_t* jsn = json_load_file(path);
    assert(jsn);
    clock_t diff = clock() - start;
    printf("completed in: %f secs\n", diff / (double)CLOCKS_PER_SEC);
    json_print(jsn, stdout);

    printf("done\n");

//    json_t* jsn = json_new();
//    jobj_t root = json_root(jsn);
//
//    jobj_add_bool(root, "true", true);
//    jobj_add_bool(root, "false", false);
//    jobj_add_nil(root, "nil");
//    jobj_add_num(root, "num", 3.14);
//    jobj_add_str(root, "string", "string");
//    jobj_t child = jobj_add_obj(root, "obj");
//        jobj_add_str(child, "key", "child");
//    jarray_t array = jobj_add_array(root, "array");
//        jarray_add_bool(array, true);
//        jarray_add_bool(array, false);
//        jarray_add_nil(array);
//        jarray_add_num(array, 5.5);
//        jobj_t child2 = jarray_add_obj(array);
//            jobj_add_bool(child2, "true", true);
//        jarray_t sarray = jarray_add_array(array);
//            jarray_add_num(sarray, 1);
//            jarray_add_num(sarray, 2);
//            jarray_add_num(sarray, 3);
//    json_print(jsn, stdout);
    return 0;
}
