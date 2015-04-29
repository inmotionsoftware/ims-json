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
typedef uint32_t jsize_t;

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

struct jstr_t;

int jstr_cmp(jstr_t* str, const char* cstr);
int jstr_ncmp(jstr_t* str, const char* cstr, size_t len);

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

struct _jobj_t;
struct _jarray_t;

struct jbuf_t
{
    size_t cap;
    size_t len;
    char* ptr;
};

void jbuf_init(jbuf_t*);
void jbuf_destroy(jbuf_t*);

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
    size_t len;
    size_t cap;
    size_t nbuckets;
    jmapbucket_t* buckets;
    jstr_t* strs;
};

void jmap_init(jmap_t* map);
size_t jmap_find_str(jmap_t*, const char*);
jstr_t* jmap_get_str(jmap_t*, size_t);

struct jlist_t
{
    json_t* json;
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

json_t* json_load_file( const char* path );

json_t* json_new();
void json_print(json_t* j, FILE*);
void json_free(json_t* j);
jobj_t json_root(json_t* j);


#endif /* defined(__MMapJson__json2__) */
