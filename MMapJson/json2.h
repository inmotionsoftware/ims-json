//
//  json2.h
//  MMapJson
//
//  Created by Brian Howard on 4/20/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#ifndef __MMapJson__json2__
#define __MMapJson__json2__

#include <stdio.h>
#include <stdint.h>
#include <memory.h>
#include <stdlib.h>
#include <math.h>
#include <vector>

typedef double jnum_t;
typedef uint8_t jbool;

//------------------------------------------------------------------------------
enum jtype
{
    JTYPE_NIL = 0,
    JTYPE_STR,
    JTYPE_NUM,
    JTYPE_ARRAY,
    JTYPE_OBJ,
    JTYPE_TRUE,
    JTYPE_FALSE
};

//------------------------------------------------------------------------------
struct jstr_t
{
    size_t len;
    uint32_t hash;
    char* chars;
    char buf[10];
};

//------------------------------------------------------------------------------
struct jval_t
{
    uint32_t type : 4;
    uint32_t idx : 28;
};

//------------------------------------------------------------------------------
struct jkv_t
{
    uint16_t key;
    jval_t val;
};

//------------------------------------------------------------------------------
struct jobj_t
{
    struct json_t* json;
    size_t cap;
    size_t len;
    jkv_t* kvs;
    jkv_t buf[10];
};

void jobj_add_num( jobj_t* obj, const char* key, jnum_t num );
void jobj_add_str( jobj_t* obj, const char* key, const char* str );
void jobj_add_bool( jobj_t* obj, const char* key, jbool b );
void jobj_add_nil( jobj_t* obj, const char* key );
struct jarray_t* jobj_add_array( jobj_t* obj, const char* key );
jobj_t* jobj_add_obj( jobj_t* obj, const char* key );

//------------------------------------------------------------------------------
struct jarray_t
{
    struct json_t* json;
    size_t cap;
    size_t len;
    jval_t* vals;
    jval_t buf[10];
};

void jarray_add_num( jarray_t* a, jnum_t num );
void jarray_add_str( jarray_t* a, const char* str );
void jarray_add_bool( jarray_t* a, jbool b );
void jarray_add_nil(jarray_t* a);
jarray_t* jarray_add_array(jarray_t* a);
jobj_t* jarray_add_obj(jarray_t* a);


//------------------------------------------------------------------------------
struct json_t
{
    std::vector<jnum_t> nums;
    std::vector<jstr_t> strs;
    std::vector<jobj_t> objs;
    std::vector<jarray_t> arrays;
};

json_t* json_new();
void json_print(json_t* j, FILE*);
void json_free(json_t* j);
jobj_t* json_root(json_t* j);


#endif /* defined(__MMapJson__json2__) */
