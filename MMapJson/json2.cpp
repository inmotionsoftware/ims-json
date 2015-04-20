//
//  json2.c
//  MMapJson
//
//  Created by Brian Howard on 4/20/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include "json2.h"
#include <math.h>

#ifndef MAX
    #define MAX(A,B) (A)>(B)?(A):(B)
#endif

#ifndef MIN
    #define MIN(A,B) (A)<(B)?(A):(B)
#endif

//------------------------------------------------------------------------------
uint32_t murmur3_32(const char *key, size_t len, uint32_t seed)
{
	static const uint32_t c1 = 0xcc9e2d51;
	static const uint32_t c2 = 0x1b873593;
	static const uint32_t r1 = 15;
	static const uint32_t r2 = 13;
	static const uint32_t m = 5;
	static const uint32_t n = 0xe6546b64;
 
	uint32_t hash = seed;
 
	const size_t nblocks = len / 4;
	const uint32_t *blocks = (const uint32_t *) key;

	for (int i = 0; i < nblocks; i++)
    {
		uint32_t k = blocks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
 
		hash ^= k;
		hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
	}
 
	const uint8_t *tail = (const uint8_t *) (key + nblocks * 4);
	uint32_t k1 = 0;
 
	switch (len & 3) {
	case 3:
		k1 ^= tail[2] << 16;
	case 2:
		k1 ^= tail[1] << 8;
	case 1:
		k1 ^= tail[0];
 
		k1 *= c1;
		k1 = (k1 << r1) | (k1 >> (32 - r1));
		k1 *= c2;
		hash ^= k1;
	}
 
	hash ^= len;
	hash ^= (hash >> 16);
	hash *= 0x85ebca6b;
	hash ^= (hash >> 13);
	hash *= 0xc2b2ae35;
	hash ^= (hash >> 16);
 
	return hash;
}


//------------------------------------------------------------------------------
static jobj_t* json_add_obj( json_t* j, size_t* idx )
{
    *idx = j->objs.size();
    j->objs.emplace_back();
    jobj_t* o = &j->objs.back();
    o->json = j;
    o->cap = 10;
    o->len = 0;
    o->kvs = o->buf;
    return o;
}

static jarray_t* json_add_array( json_t* j, size_t* idx )
{
    *idx = j->arrays.size();
    j->arrays.emplace_back();
    jarray_t* a = &j->arrays.back();
    a->json = j;
    a->cap = 10;
    a->len = 0;
    a->vals = a->buf;
    return a;
}

static size_t json_add_num( json_t* j, jnum_t n )
{
    size_t idx = j->nums.size();
    j->nums.push_back(n);
    return idx;
}

static size_t json_add_str( json_t* j, const char* str )
{
    static const uint32_t MURMER32_SEED = 0;

    size_t idx = j->strs.size();
    j->strs.emplace_back();
    jstr_t* jstr = &j->strs.back();

    size_t len = strlen(str);
    if (len <= 10)
    {
        jstr->chars = jstr->buf;
    }
    else
    {
        jstr->chars = (char*)malloc( len );
    }
    memcpy(jstr->chars, str, len);
    jstr->len = len;
    jstr->hash = murmur3_32(jstr->chars, MIN(30, jstr->len), MURMER32_SEED);
    return idx;
}

//------------------------------------------------------------------------------
void jobj_reserve( jobj_t* obj, size_t cap )
{
    if ( cap < (obj->cap - obj->len))
        return;

    cap = MAX(obj->len+cap, obj->cap*2+1);
    void* ptr = (obj->kvs == obj->buf) ? NULL : obj->kvs;
    obj->kvs = (jkv_t*)realloc( ptr, cap * sizeof(jkv_t) );
}

void jobj_add_num( jobj_t* obj, const char* key, jnum_t num )
{
    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = JTYPE_NUM;
    kv->val.idx = (uint32_t)json_add_num(obj->json, num);
}

void jobj_add_str( jobj_t* obj, const char* key, const char* str )
{
    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = JTYPE_STR;
    kv->val.idx = (uint32_t)json_add_str(obj->json, str);
}

void jobj_add_bool( jobj_t* obj, const char* key, jbool b )
{
    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    kv->val.idx = 0;
}

void jobj_add_nil( jobj_t* obj, const char* key )
{
    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;
}

struct jarray_t* jobj_add_array( jobj_t* obj, const char* key )
{
    size_t idx = 0;
    jarray_t* a = json_add_array(obj->json, &idx);

    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = JTYPE_ARRAY;
    kv->val.idx = (uint32_t)idx;

    return a;
}

jobj_t* jobj_add_obj( jobj_t* obj, const char* key )
{
    size_t idx = 0;
    jobj_t* o = json_add_obj(obj->json, &idx);

    jobj_reserve(obj, 1);
    jkv_t* kv = &obj->kvs[obj->len++];
    kv->key = json_add_str(obj->json, key);
    kv->val.type = JTYPE_OBJ;
    kv->val.idx = (uint32_t)idx;

    return o;
}

//------------------------------------------------------------------------------
void jarray_reserve( jarray_t* a, size_t cap )
{
    if ( cap < (a->cap - a->len))
        return;

    cap = MAX(a->len+cap, a->cap*2+1);
    void* ptr = (a->vals == a->buf) ? NULL : a->vals;
    a->vals = (jval_t*)realloc( ptr, cap * sizeof(jkv_t) );
}

void jarray_add_num( jarray_t* a, jnum_t num )
{
    jarray_reserve(a, 1);
    size_t idx = json_add_num(a->json, num);
    jval_t* val = &a->vals[a->len++];
    val->type = JTYPE_NUM;
    val->idx = (uint32_t)idx;
}

void jarray_add_str( jarray_t* a, const char* str )
{
    jarray_reserve(a, 1);
    size_t idx = json_add_str(a->json, str);
    jval_t* val = &a->vals[a->len++];
    val->type = JTYPE_STR;
    val->idx = (uint32_t)idx;
}

void jarray_add_bool( jarray_t* a, jbool b )
{
    jarray_reserve(a, 1);
    jval_t* val = &a->vals[a->len++];
    val->type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    val->idx = 0;
}

void jarray_add_nil( jarray_t* a )
{
    jarray_reserve(a, 1);
    jval_t* val = &a->vals[a->len++];
    val->type = JTYPE_NIL;
    val->idx = 0;
}

jarray_t* jarray_add_array( jarray_t* array )
{
    size_t idx = 0;
    jarray_t* a = json_add_array(array->json, &idx);

    jarray_reserve(array, 1);
    jval_t* val = &array->vals[array->len++];
    val->type = JTYPE_ARRAY;
    val->idx = (uint32_t)idx;

    return a;
}

jobj_t* jarray_add_obj( jarray_t* obj )
{
    size_t idx = 0;
    jobj_t* o = json_add_obj(obj->json, &idx);

    jarray_reserve(obj, 1);
    jval_t* val = &obj->vals[obj->len++];
    val->type = JTYPE_OBJ;
    val->idx = (uint32_t)idx;

    return o;
}

//------------------------------------------------------------------------------
static void jobj_print(jobj_t* root, size_t depth, FILE* f);
static void jstr_print( jstr_t* str, FILE* f )
{
    putc('"', f);
    for ( size_t i = 0; i < str->len; ++i )
    {
        putc(str->chars[i], f);
    }
    putc('"', f);
}

static void print_tabs( size_t cnt, FILE* f)
{
    while (cnt--)
    {
        fputs("   ", f);
    }
}

static void jarray_print(jarray_t* root, size_t depth, FILE* f)
{
    fputc('[', f);

    json_t* j = root->json;
    for ( size_t i = 0; i < root->len; ++i )
    {
        jval_t* val = &root->vals[i];

        fputs("\n", f);
        print_tabs(depth+1, f);

        switch (val->type)
        {
            case JTYPE_NIL:
                fputs("nil", f);
                break;

            case JTYPE_STR:
                jstr_print(&j->strs[val->idx], f);
                break;

            case JTYPE_NUM:
                fprintf(f, "%f", j->nums[val->idx]);
                break;

            case JTYPE_ARRAY:
                jarray_print(&j->arrays[val->idx], depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print(&j->objs[val->idx], depth+1, f);
                break;

            case JTYPE_TRUE:
                fputs("true", f);
                break;

            case JTYPE_FALSE:
                fputs("false", f);
                break;

            default:
                break;
        }

        // trailing comma
        if (i+1 < root->len) fputs(",", f);
    }

    fputc('\n', f);
    print_tabs(depth, f);
    fputc(']', f);
}

static void jobj_print(jobj_t* root, size_t depth, FILE* f)
{
    fputc('{', f);

    json_t* j = root->json;
    for ( size_t i = 0; i < root->len; ++i )
    {
        jkv_t* kv = &root->kvs[i];
        jstr_t* key = &j->strs[kv->key];

        fputs("\n", f);
        print_tabs(depth+1, f);
        jstr_print(key, f);
        fputs(": ", f);

        switch (kv->val.type)
        {
            case JTYPE_NIL:
                fputs("nil", f);
                break;

            case JTYPE_STR:
                jstr_print(&j->strs[kv->val.idx], f);
                break;

            case JTYPE_NUM:
                fprintf(f, "%f", j->nums[kv->val.idx]);
                break;

            case JTYPE_ARRAY:
                jarray_print(&j->arrays[kv->val.idx], depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print(&j->objs[kv->val.idx], depth+1, f);
                break;

            case JTYPE_TRUE:
                fputs("true", f);
                break;

            case JTYPE_FALSE:
                fputs("false", f);
                break;

            default:
                break;
        }

        // trailing comma
        if (i+1 < root->len) fputs(",", f);
    }

    fputc('\n', f);
    print_tabs(depth, f);
    fputc('}', f);
}

//------------------------------------------------------------------------------
void json_print(json_t* j, FILE* f)
{
    jobj_t* root = json_root(j);
    jobj_print(root, 0, f);
}

json_t* json_new()
{
    json_t* j = new json_t();

    size_t idx = 0;
    json_add_obj(j, &idx);

    return j;
}

void json_free(json_t* j)
{
    delete j;
}

jobj_t* json_root(json_t* j)
{
    return &j->objs[0];
}