//
//  main.cpp
//  MMapJson
//
//  Created by Brian Howard on 4/8/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include <time.h>
#include "json.h"

//------------------------------------------------------------------------------
int main(int argc, const char * argv[])
{
//    const char* path = "/Users/bghoward/Projects/Emblazed-NextGen/IMSFoundation/sample.json";

    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";
//    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/test.json";
//    const char* path = "/Users/bghoward/Dropbox/Project Sluice/external data/gz_2010_us_050_00_500k.json";
//    const char* path = "/Users/bghoward/Downloads/Newtonsoft.Json-6.0.8/Src/Newtonsoft.Json.Tests/large.json";

    // mmap a file
    // parse into json structure

    clock_t start = clock();
    json doc = json::load_file(path);
    clock_t diff = clock() - start;
    printf("completed in: %f secs\n", diff / (double)CLOCKS_PER_SEC);

//    std::cout << doc << std::endl;

    return 0;
}
