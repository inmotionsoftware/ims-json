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
#include <stdlib.h>

//------------------------------------------------------------------------------
typedef double jnum_t;
typedef uint8_t jbool;
typedef uint32_t jsize_t;

//------------------------------------------------------------------------------
struct jstr_t;
struct _jobj_t;
struct _jarray_t;

//------------------------------------------------------------------------------
enum jtype
{
    JTYPE_NIL   = 0,
    JTYPE_STR   = 1,
    JTYPE_NUM   = 2,
    JTYPE_ARRAY = 3,
    JTYPE_OBJ   = 4,
    JTYPE_TRUE  = 5,
    JTYPE_FALSE = 6
};
#define JTYPE_MASK 0x7

//------------------------------------------------------------------------------
struct jobj_t
{
    struct json_t* json;
    size_t idx;
};

void jobj_add_num( jobj_t obj, const char* key, jnum_t num );
void jobj_add_str( jobj_t obj, const char* key, const char* str );
void jobj_add_bool( jobj_t obj, const char* key, jbool b );
void jobj_add_nil( jobj_t obj, const char* key );
struct jarray_t jobj_add_array( jobj_t obj, const char* key );
jobj_t jobj_add_obj( jobj_t obj, const char* key );

//------------------------------------------------------------------------------
struct jarray_t
{
    struct json_t* json;
    size_t idx;
};

void jarray_add_num( jarray_t a, jnum_t num );
void jarray_add_str( jarray_t a, const char* str );
void jarray_add_bool( jarray_t a, jbool b );
void jarray_add_nil(jarray_t a);
jarray_t jarray_add_array(jarray_t a);
jobj_t jarray_add_obj(jarray_t a);

//------------------------------------------------------------------------------
struct jbuf_t
{
    size_t cap;
    size_t len;
    char* ptr;
};

//------------------------------------------------------------------------------
struct jmapbucket_t
{
    size_t len;
    size_t cap;
    size_t* slots;
};

//------------------------------------------------------------------------------
struct jmap_t
{
    size_t blen;
    size_t bcap;
    jmapbucket_t* buckets;

    size_t slen;
    size_t scap;
    jstr_t* strs;
};

//------------------------------------------------------------------------------
struct jlist_t
{
    json_t* json; // pointer back to parent
    size_t len;
    size_t cap;
    char data[0];
};

//------------------------------------------------------------------------------
struct json_t
{
    // objs
    size_t nums_len;
    size_t nums_cap;
    jnum_t* nums;

    jlist_t* objs;
    jlist_t* arrays;

    struct jmap_t strmap;

    jbuf_t keybuf; // buffer for temporarily storing the key string
    jbuf_t valbuf; // buffer for temporarily storing the value string
};

json_t* json_new();
json_t* json_load_file( const char* path );
void json_print(json_t* j, FILE*);
void json_free(json_t* j);
jobj_t json_root(json_t* j);

#endif /* defined(__MMapJson__json2__) */
