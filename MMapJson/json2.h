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
#include <string.h>

#ifdef __cplusplus
extern "C" {
namespace ims {
#endif

//------------------------------------------------------------------------------
typedef double jnum_t;
typedef uint8_t jbool_t;
typedef uint32_t jsize_t;

//------------------------------------------------------------------------------
struct jstr_t;
struct _jobj_t;
struct _jarray_t;
struct json_t;

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
struct jval_t
{
    uint32_t type : 4;
    uint32_t idx : 28;
};
typedef struct jval_t jval_t;

void jval_print( struct json_t* jsn, jval_t val, size_t depth, FILE* f );

#define jval_type(VAL)      (VAL.type & JTYPE_MASK)
#define jval_is_str(VAL)    (jval_type(VAL) == JTYPE_STR)
#define jval_is_num(VAL)    (jval_type(VAL) == JTYPE_NUM)
#define jval_is_obj(VAL)    (jval_type(VAL) == JTYPE_OBJ)
#define jval_is_true(VAL)   (jval_type(VAL) == JTYPE_TRUE)
#define jval_is_false(VAL)  (jval_type(VAL) == JTYPE_FALSE)
#define jval_is_bool(VAL)   (jval_is_true(VAL) || jval_is_false(VAL))
#define jval_is_nil(VAL)    (jval_type(VAL) == JTYPE_NIL)
#define jval_is_array(VAL)  (jval_type(VAL) == JTYPE_ARRAY)

//------------------------------------------------------------------------------
struct jobj_t
{
    struct json_t* json;
    size_t idx;
};
typedef struct jobj_t jobj_t;

void jobj_reserve( jobj_t obj, size_t cap );
void jobj_add_num( jobj_t obj, const char* key, jnum_t num );
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen );
void jobj_add_bool( jobj_t obj, const char* key, jbool_t b );
void jobj_add_nil( jobj_t obj, const char* key );
jobj_t jobj_add_obj( jobj_t obj, const char* key );
jval_t jobj_findl( jobj_t obj, const char* key, size_t klen );
size_t jobj_len( jobj_t obj );
const char* jobj_get(jobj_t obj, size_t idx, jval_t* val);
void jobj_print(jobj_t obj, size_t depth, FILE* f);

#define jobj_get_json(OBJ) (OBJ).json
#define jobj_find(OBJ, KEY) jobj_findl(OBJ, KEY, strlen(KEY))
#define jobj_add_str(OBJ, KEY, STR) jobj_add_strl(OBJ, KEY, STR, strlen(STR))

//------------------------------------------------------------------------------
struct jarray_t
{
    struct json_t* json;
    size_t idx;
};
typedef struct jarray_t jarray_t;

void jarray_reserve( jarray_t a, size_t cap );
jarray_t jobj_add_array( jobj_t obj, const char* key );
void jarray_add_num( jarray_t a, jnum_t num );
void jarray_add_strl( jarray_t a, const char* str, size_t slen );
void jarray_add_bool( jarray_t a, jbool_t b );
void jarray_add_nil(jarray_t a);
jarray_t jarray_add_array(jarray_t a);
jobj_t jarray_add_obj(jarray_t a);
size_t jarray_len(jarray_t a);
jval_t jarray_get(jarray_t a, size_t idx);
void jarray_print( jarray_t array, size_t depth, FILE* f );

#define jarray_add_str(A, STR) jarray_add_strl(A, STR, strlen(STR))
#define jarray_get_json(A) (A).json
#define jarray_get_str(A, IDX) json_get_str(jarray_get_json(A), jarray_get(A, IDX))
#define jarray_get_num(A, IDX) json_get_num(jarray_get_json(A), jarray_get(A, IDX))

//------------------------------------------------------------------------------
struct jmap_t
{
    size_t blen;
    size_t bcap;
    struct jmapbucket_t* buckets;

    size_t slen;
    size_t scap;
    struct jstr_t* strs;
};
typedef struct jmap_t jmap_t;

//------------------------------------------------------------------------------
struct json_t
{
    struct
    {
        size_t len;
        size_t cap;
        jnum_t* ptr;
    } nums;

    struct
    {
        size_t len;
        size_t cap;
        struct _jobj_t* ptr;
    } objs;

    struct
    {
        size_t len;
        size_t cap;
        struct _jarray_t* ptr;
    } arrays;

    jmap_t strmap;
};
typedef struct json_t json_t;

json_t* json_new();
const char* json_load_path(json_t* jsn, const char* path);
const char* json_load_file(json_t* jsn, FILE* file);
const char* json_load_buf(json_t* jsn, void* buf, size_t blen);
void json_print(json_t* j, FILE*);
void json_free(json_t* j);
jobj_t json_root(json_t* j);
const char* json_get_str( json_t* jsn, jval_t val );
jnum_t json_get_num( json_t* jsn, jval_t val );
jbool_t json_get_bool( json_t* jsn, jval_t val );
jobj_t json_get_obj( json_t* jsn, jval_t val );
jarray_t json_get_array( json_t* jsn, jval_t val );

#define jobj_find_str(OBJ, KEY) json_get_str(jobj_get_json(OBJ), jobj_find(OBJ, KEY))
#define jobj_find_num(OBJ, KEY) json_get_num(jobj_get_json(OBJ), jobj_find(OBJ, KEY))
#define jobj_find_bool(OBJ, KEY) json_get_bool(jobj_get_json(OBJ), jobj_find(OBJ, KEY))
#define jobj_find_nil(OBJ, KEY) jval_is_nil(jobj_find(OBJ, KEY))
#define jobj_find_obj(OBJ, KEY) json_get_obj(jobj_get_json(OBJ), jobj_find(OBJ, KEY))
#define jobj_find_array(OBJ, KEY) json_get_array(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

#ifdef __cplusplus
} // namespace
}
#endif

#endif /* defined(__MMapJson__json2__) */
