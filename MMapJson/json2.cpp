//
//  json2.c
//  MMapJson
//
//  Created by Brian Howard on 4/20/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include "json2.h"
#include <math.h>
#include <errno.h>
#include <memory.h>
#include <limits.h>
#include <stddef.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>

#pragma mark - macros

#ifndef MAX
    #define MAX(A,B) (A)>(B)?(A):(B)
#endif

#ifndef MIN
    #define MIN(A,B) (A)<(B)?(A):(B)
#endif

#ifndef json_assert
    #define json_assert(A, STR, ...) { if (!(A)) {printf(STR "\n", ## __VA_ARGS__ ); abort();} }
#endif

#define PRINT 1
#if PRINT
    #define json_fprintf(F, FMT, ...) fprintf(F, FMT, ## __VA_ARGS__ )
#else
    #define json_fprintf(F, FMT, ...)
#endif

#pragma mark - constants

#define BUF_SIZE ((size_t)6)
#define MAX_VAL_IDX 268435456 // 2^28
#define MAX_KEY_IDX UINT32_MAX

#define JMAP_MAX_LOADFACTOR 0.8f
#define JMAP_IDEAL_LOADFACTOR 0.3f

#pragma mark - structs

typedef uint32_t jhash_t;

//------------------------------------------------------------------------------
struct jstr_t
{
    jsize_t len;
    jhash_t hash;
    union
    {
        char* chars;
        char buf[BUF_SIZE];
    };
};
typedef struct jstr_t jstr_t;

//------------------------------------------------------------------------------
struct jval_t
{
    uint32_t type : 4;
    uint32_t idx : 28;
};
typedef struct jval_t jval_t;

//------------------------------------------------------------------------------
struct jkv_t
{
    uint32_t key;
    jval_t val;
};
typedef struct jkv_t jkv_t;

//------------------------------------------------------------------------------
struct _jobj_t
{
    jsize_t idx;
    jsize_t cap;
    jsize_t len;
    union
    {
        jkv_t* kvs;
        jkv_t buf[BUF_SIZE];
    };
};
typedef struct _jobj_t _jobj_t;

//------------------------------------------------------------------------------
struct _jarray_t
{
    jsize_t idx;
    jsize_t cap;
    jsize_t len;
    union
    {
        jval_t* vals;
        jval_t buf[BUF_SIZE];
    };
};
typedef struct _jarray_t _jarray_t;

#pragma mark - function prototypes
//------------------------------------------------------------------------------
static inline void jarray_print(jarray_t _root, size_t depth, FILE* f);
size_t jmap_add_str(jmap_t* map, const char* cstr, size_t slen);
template < typename T > inline void parse_obj( jobj_t obj, T& beg, const T& end );

#pragma mark - util

//------------------------------------------------------------------------------
static void print_tabs( size_t cnt, FILE* f)
{
    while (cnt--)
    {
        json_fprintf(f, "   ");
    }
}

//------------------------------------------------------------------------------
static inline jnum_t btomb(size_t bytes)
{
    return (bytes / (jnum_t)(1024*1024));
}

//------------------------------------------------------------------------------
static inline size_t grow( size_t min, size_t cur )
{
    static const jnum_t GROWTH_FACTOR = 1.1;
    assert(min >= cur);

    size_t size = min;
//    size_t size = MAX(13, min);
    size = MAX(size, BUF_SIZE*2);
    size = MAX(size, cur*GROWTH_FACTOR+2);
    assert(size > cur);
    return size;
}

#pragma mark - jstr_t

//------------------------------------------------------------------------------
static inline void jstr_print( jstr_t* str, FILE* f )
{
    json_fprintf(f, "\"");

#if PRINT
    const char* chars = (str->len > BUF_SIZE) ? str->chars : str->buf;
    for ( size_t i = 0; i < str->len; ++i )
    {
        putc(chars[i], f);
    }
#endif

    json_fprintf(f, "\"");
}

//------------------------------------------------------------------------------
jhash_t murmur3_32(const char *key, size_t len, jhash_t seed)
{
	static const uint32_t c1 = 0xcc9e2d51;
	static const uint32_t c2 = 0x1b873593;
	static const uint32_t r1 = 15;
	static const uint32_t r2 = 13;
	static const uint32_t m = 5;
	static const uint32_t n = 0xe6546b64;
 
	jhash_t hash = seed;
 
	const size_t nblocks = len / sizeof(jhash_t);
	const jhash_t *blocks = (const jhash_t *) key;

	for (int i = 0; i < nblocks; i++)
    {
		jhash_t k = blocks[i];
		k *= c1;
		k = (k << r1) | (k >> (32 - r1));
		k *= c2;
 
		hash ^= k;
		hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
	}
 
	const uint8_t *tail = (const uint8_t *) (key + nblocks * 4);
	jhash_t k1 = 0;
 
	switch (len & 3)
    {
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
            break;
	}
 
	hash ^= len;
	hash ^= (hash >> 16);
	hash *= 0x85ebca6b;
	hash ^= (hash >> 13);
	hash *= 0xc2b2ae35;
	hash ^= (hash >> 16);
 
	return hash;
}

jhash_t strhash( const char* key, size_t len )
{
    static const jhash_t MURMER32_SEED = 0;
    static const size_t MAX_CHARS = 32;
    return murmur3_32(key, MIN(MAX_CHARS, len), MURMER32_SEED);
}

void jstr_init_str_hash( jstr_t* jstr, const char* cstr, size_t len, jhash_t hash )
{
    assert(jstr);
    // TODO: verify the length does not exceed our max

    jstr->len = (jsize_t)len;
    jstr->hash = hash;
    if (len > BUF_SIZE)
    {
        char* buf = (char*)malloc( len * sizeof(char) );
        memcpy(buf, cstr, len * sizeof(char));
        jstr->chars = buf;
    }
    else
    {
        memcpy(jstr->buf, cstr, len * sizeof(char));
    }
}

void jstr_init_str( jstr_t* jstr, const char* cstr, size_t len )
{
    jstr_init_str_hash(jstr, cstr, len, strhash(cstr, len));
}


#pragma mark - jmap_t

//------------------------------------------------------------------------------
void jmap_init(jmap_t* map)
{
    map->blen = 0;
    map->bcap = 0;
    map->buckets = NULL;

    map->slen = 0;
    map->scap = 0;
    map->strs = NULL;
}

//------------------------------------------------------------------------------
void jmap_destroy(jmap_t* map)
{
    // clean up bucket slots
    for ( size_t i = 0; i < map->bcap; i++ )
    {
        jmapbucket_t* bucket = &map->buckets[i];
        if (bucket->slots)
        {
            free(bucket->slots); bucket->slots = NULL;
        }
    }

    // clean up buckets
    free(map->buckets); map->buckets = NULL;

    if (map->strs)
    {
        // cleanup strings
        for ( size_t i = 0; i < map->slen; i++ )
        {
            jstr_t* str = &map->strs[i];
            if (str->len > BUF_SIZE)
            {
                free(str->chars);
            }
        }
        free(map->strs); map->strs = NULL;
    }
    map->slen = map->scap = 0;
    map->blen = map->bcap = 0;
}

//------------------------------------------------------------------------------
void jmap_reserve_str( jmap_t* map, size_t len )
{
    // not found, create a new one
    if (map->slen+len >= map->scap)
    {
        map->scap = grow(map->slen+len, map->scap);
        map->strs = (jstr_t*)realloc(map->strs, sizeof(jstr_t)*map->scap);
    }
}

//------------------------------------------------------------------------------
size_t _jmap_add_str(jmap_t* map, const char* cstr, size_t len, uint32_t hash)
{
    // not found, create a new one
    jmap_reserve_str(map, 1);

    size_t idx = map->slen++;
    jstr_init_str_hash(&map->strs[idx], cstr, len, hash);
    return idx;
}

//------------------------------------------------------------------------------
void jmap_bucket_reserve( jmap_t* map, size_t idx, size_t len )
{
    assert(idx < map->bcap);
    jmapbucket_t* bucket = &map->buckets[idx];
    if ( bucket->len+len >= bucket->cap )
    {
        if (!bucket->slots) map->blen++; // empty bucket, we are about to add it, increment
        bucket->cap = grow(bucket->len+len, bucket->cap);
        bucket->slots = (size_t*)realloc( bucket->slots, sizeof(size_t) * bucket->cap );
    }
}

//------------------------------------------------------------------------------
void _jmap_add_key(jmap_t* map, uint32_t hash, size_t val)
{
    assert(map);
    assert(map->buckets);

    size_t idx = hash % map->bcap;
    jmap_bucket_reserve(map, idx, 1);
    jmapbucket_t* bucket = &map->buckets[idx];
    bucket->slots[bucket->len++] = val;
}

//------------------------------------------------------------------------------
void jmap_rehash(jmap_t* map, size_t hint)
{
    // if there is an empty hashmap, no need to check the load factor!
    if (map->bcap > 0)
    {
        float load = map->blen / (float)map->bcap;
        if (load <= JMAP_MAX_LOADFACTOR)
            return;
    }

    size_t target = ceilf(map->bcap / JMAP_IDEAL_LOADFACTOR);

    jmap_t copy = {0};
    copy.bcap = MAX( MAX(hint, 13), target);
    copy.blen = 0;

    copy.slen = map->slen;
    copy.scap = map->scap;

    copy.buckets = (jmapbucket_t*)calloc(copy.bcap, sizeof(jmapbucket_t));
    copy.strs = map->strs;

    map->slen = 0;
    map->scap = 0;
    map->strs = NULL;

    for ( size_t i = 0; i < map->bcap; i++ )
    {
        jmapbucket_t* src = &map->buckets[i];
        if (!src) continue;

        for ( size_t n = 0; n < src->len; n++ )
        {
            jstr_t* str = &copy.strs[src->slots[n]];
            _jmap_add_key(&copy, str->hash, src->slots[n]);
        }
    }

    jmap_destroy(map);
    *map = copy;
}

//------------------------------------------------------------------------------
jstr_t* jmap_get_str(jmap_t* map, size_t idx)
{
    assert(map);
    assert(idx < map->slen);
    return &map->strs[idx];
}

//------------------------------------------------------------------------------
size_t jmap_add_str(jmap_t* map, const char* cstr, size_t slen)
{
    uint32_t hash = strhash(cstr, slen);

    jmap_rehash(map, 0);

    size_t idx = hash % map->bcap;
    jmapbucket_t* bucket = &map->buckets[idx];

    size_t rt = SIZE_T_MAX;

    for ( size_t i = 0; i < bucket->len; ++i )
    {
        size_t idx = bucket->slots[i];
        jstr_t* str = &map->strs[idx];
        if (str->hash == hash && str->len == slen)
        {
            const char* chars = (str->len > BUF_SIZE) ? str->chars : str->buf;
            if (strncmp(chars, cstr, slen) == 0)
            {
                rt = idx;
                break;
            }
        }
    }

    if (rt == SIZE_T_MAX)
    {
        jmap_bucket_reserve(map, idx, 1);

        // not found, create a new one
        rt = _jmap_add_str(map, cstr, slen, hash);
        bucket->slots[bucket->len++] = rt;
    }

    return rt;
}

#pragma mark - json_t

static void json_objs_reserve( json_t* jsn, size_t res )
{
    if (!jsn->objs)
    {
        size_t cap = grow(res, 0);
        jlist_t* objs = (jlist_t*)malloc(sizeof(jlist_t) + sizeof(_jobj_t)*cap);
        objs->len = 0;
        objs->cap = cap;
        objs->json = jsn;
        jsn->objs = objs;
        return;
    }

    if (jsn->objs->len+res < jsn->objs->cap)
        return;

    size_t cap = grow(jsn->objs->len+res, jsn->objs->cap);
    jsn->objs = (jlist_t*)realloc(jsn->objs, sizeof(jlist_t) + sizeof(_jobj_t)*cap);
    jsn->objs->cap = cap;
}

static _jobj_t* json_get_obj( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert(jsn->objs);
    assert(idx < jsn->objs->len);
    return ((_jobj_t*)jsn->objs->data) + idx;
}

static size_t json_add_obj( json_t* jsn )
{
    assert(jsn);
    json_objs_reserve(jsn, 1);

    size_t idx = jsn->objs->len++;
    _jobj_t* obj = json_get_obj(jsn, idx);
    obj->cap = BUF_SIZE;
    obj->len = 0;
    obj->idx = (jsize_t)idx;
    return idx;
}

static void json_arrays_reserve( json_t* jsn, size_t res )
{
    if (!jsn->arrays)
    {
        size_t cap = grow(res, 0);
        jlist_t* arrays = (jlist_t*)malloc(sizeof(jlist_t) + sizeof(_jarray_t)*cap);
        arrays->len = 0;
        arrays->cap = cap;
        arrays->json = jsn;
        jsn->arrays = arrays;
        return;
    }

    if (jsn->arrays->len+res < jsn->arrays->cap)
        return;

    size_t cap = grow(jsn->arrays->len+res, jsn->arrays->cap);
    jsn->arrays = (jlist_t*)realloc(jsn->arrays, sizeof(jlist_t) + sizeof(_jarray_t)*cap);
    jsn->arrays->cap = cap;
}

static _jarray_t* json_get_array( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert(jsn->arrays);
    assert(idx < jsn->arrays->len);
    return ((_jarray_t*)jsn->arrays->data) + idx;
}

static size_t json_add_array( json_t* jsn )
{
    assert(jsn);
    json_arrays_reserve(jsn, 1);

    size_t idx = jsn->arrays->len++;
    _jarray_t* array = json_get_array(jsn, idx);
    array->cap = BUF_SIZE;
    array->len = 0;
    array->idx = (jsize_t)idx;
    return idx;
}

static void json_nums_reserve( json_t* jsn, size_t len )
{
    if (jsn->nums_len+len >= jsn->nums_cap)
    {
        jsn->nums_cap = grow(jsn->nums_len+len, jsn->nums_cap);
        jsn->nums = (jnum_t*)realloc(jsn->nums, jsn->nums_cap * sizeof(jnum_t));
    }
}

static size_t json_add_num( json_t* jsn, jnum_t n )
{
    assert(jsn);

    json_nums_reserve(jsn, 1);
    size_t idx = jsn->nums_len++;
    jsn->nums[idx] = n;
    return idx;
}

static size_t json_add_str( json_t* j, const char* str )
{
    assert(str);
    size_t idx = jmap_add_str(&j->strmap, str, strlen(str));
    assert (idx != SIZE_T_MAX);
    return idx;
}

#pragma mark - jobj_t

static inline _jobj_t* jobj_get_obj(jobj_t obj)
{
    assert(obj.json);
    assert(obj.idx < obj.json->objs->len);
    return json_get_obj(obj.json, obj.idx);
}

//------------------------------------------------------------------------------
static json_t* jobj_get_json( _jobj_t* obj )
{
    const void* ptr = (obj - obj->idx);
    ssize_t off1 = offsetof(jlist_t, data);
    ssize_t off2 = offsetof(jlist_t, json);
    jlist_t* b = (jlist_t*)( (char*)ptr + (off2 - off1));
    return b->json;
}

//------------------------------------------------------------------------------
void jobj_truncate( _jobj_t* obj )
{
    if (obj->len == obj->cap)
        return;

    if (obj->cap <= BUF_SIZE)
        return;

    if (obj->len <= BUF_SIZE && obj->cap > BUF_SIZE)
    {
        jkv_t* kvs = obj->kvs;
        memcpy(obj->buf, kvs, sizeof(jkv_t)*obj->len);
        free(kvs);
        obj->cap = obj->len;
        return;
    }

    obj->kvs = (jkv_t*)realloc(obj->kvs, obj->len * sizeof(jkv_t));
    obj->cap = obj->len;
}

//------------------------------------------------------------------------------
void jobj_reserve( _jobj_t* obj, size_t cap )
{
    if ( obj->len+cap < obj->cap)
        return;

    size_t prev_cap = obj->cap;
    obj->cap = (jsize_t)grow(obj->len+cap, obj->cap);

    if (prev_cap <= BUF_SIZE)
    {
        jkv_t* kvs = (jkv_t*)malloc( obj->cap * sizeof(jkv_t) );
        memcpy(kvs, obj->buf, sizeof(jkv_t) * obj->len);
        obj->kvs = kvs;
    }
    else
    {
        assert(obj->kvs);
        obj->kvs = (jkv_t*)realloc(obj->kvs, sizeof(jkv_t)*obj->cap);
    }
}

//------------------------------------------------------------------------------
jkv_t* jobj_get_kv(_jobj_t* obj, size_t idx)
{
    assert(idx < obj->len);
    assert (idx < obj->len);
    return (obj->cap > BUF_SIZE) ? &obj->kvs[idx] : &obj->buf[idx];
}

//------------------------------------------------------------------------------
jstr_t* jobj_get_key(_jobj_t* obj, size_t idx)
{
    jkv_t* kv = jobj_get_kv(obj, idx);
    assert(kv);
    json_t* jsn = jobj_get_json(obj);
    return jmap_get_str(&jsn->strmap, kv->key);
}

//------------------------------------------------------------------------------
jval_t* jobj_get_val(_jobj_t* obj, size_t idx)
{
    return &jobj_get_kv(obj, idx)->val;
}

//------------------------------------------------------------------------------
jkv_t* jobj_add_kv(_jobj_t* obj)
{
    jobj_reserve(obj, 1);
    jkv_t* kv = jobj_get_kv(obj, obj->len++);
    *kv = {0}; // clear out
    return kv;
}

//------------------------------------------------------------------------------
void jobj_add_num( jobj_t _obj, const char* key, jnum_t num )
{
    _jobj_t* obj = jobj_get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    size_t idx = json_add_num(jobj_get_json(obj), num);

    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NUM;
    kv->val.idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jobj_add_str( jobj_t _obj, const char* key, const char* str )
{
    _jobj_t* obj = jobj_get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    size_t idx = json_add_str(jobj_get_json(obj), str);

    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_STR;
    kv->val.idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jobj_add_bool( jobj_t _obj, const char* key, jbool b )
{
    _jobj_t* obj = jobj_get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    assert (kidx < MAX_KEY_IDX);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    kv->val.idx = 0;
}

//------------------------------------------------------------------------------
void jobj_add_nil( jobj_t _obj, const char* key )
{
    _jobj_t* obj = jobj_get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    assert (kidx < MAX_KEY_IDX);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;
}

//------------------------------------------------------------------------------
jarray_t jobj_add_array( jobj_t _obj, const char* key )
{
    size_t idx = json_add_array(_obj.json);
    _jobj_t* obj = jobj_get_obj(_obj);
    json_t* jsn = jobj_get_json(obj);

    size_t kidx = json_add_str(jsn, key);
    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_ARRAY;
    kv->val.idx = (uint32_t)idx;
    return (jarray_t){ jsn, idx };
}

//------------------------------------------------------------------------------
jobj_t jobj_add_obj( jobj_t _obj, const char* key )
{
    size_t idx = json_add_obj(_obj.json);

    _jobj_t* obj = jobj_get_obj(_obj);
    json_t* jsn = jobj_get_json(obj);

    size_t kidx = json_add_str(jsn, key);
    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_OBJ;
    kv->val.idx = (uint32_t)idx;

    return (jobj_t){ jsn, idx };
}

//------------------------------------------------------------------------------
static inline void jobj_print(jobj_t _root, size_t depth, FILE* f)
{
    json_fprintf(f, "{");

    _jobj_t* root = jobj_get_obj(_root);

    json_t* jsn = jobj_get_json(root);
    for ( size_t i = 0; i < root->len; ++i )
    {
        jstr_t* key = jobj_get_key(root, i);
        jval_t* val = jobj_get_val(root, i);

        json_fprintf(f, "\n");
        print_tabs(depth+1, f);
        jstr_print(key, f);
        json_fprintf(f, ": ");

        switch (val->type)
        {
            case JTYPE_NIL:
                json_fprintf(f, "nil");
                break;

            case JTYPE_STR:
                jstr_print(jmap_get_str(&jsn->strmap, val->idx), f);
                break;

            case JTYPE_NUM:
                json_fprintf(f, "%f", jsn->nums[val->idx]);
                break;

            case JTYPE_ARRAY:
                jarray_print((jarray_t){jsn, val->idx}, depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print((jobj_t){jsn, val->idx}, depth+1, f);
                break;

            case JTYPE_TRUE:
                json_fprintf(f, "true");
                break;

            case JTYPE_FALSE:
                json_fprintf(f, "false");
                break;

            default:
                break;
        }

        // trailing comma
        if (i+1 < root->len) json_fprintf(f, ",");
    }

    json_fprintf(f, "\n");
    print_tabs(depth, f);
    json_fprintf(f, "}");
}

#pragma mark - jarray_t

//------------------------------------------------------------------------------
static inline _jarray_t* jarray_get_array(jarray_t array)
{
    assert(array.json);
    return json_get_array(array.json, array.idx);
}

//------------------------------------------------------------------------------
static json_t* jarray_get_json( _jarray_t* array )
{
    const void* ptr = (array - array->idx);
    ssize_t off1 = offsetof(jlist_t, data);
    ssize_t off2 = offsetof(jlist_t, json);
    jlist_t* b = (jlist_t*)( (char*)ptr + (off2 - off1));
    return b->json;
}

//------------------------------------------------------------------------------
void jarray_truncate( _jarray_t* array )
{
    if (array->len == array->cap)
        return;

    if (array->cap <= BUF_SIZE)
        return;

    if (array->len <= BUF_SIZE && array->cap > BUF_SIZE)
    {
        jval_t* vals = array->vals;
        memcpy(array->buf, vals, sizeof(jval_t)*array->len);
        free(vals);
        array->cap = array->len;
        return;
    }

    array->vals = (jval_t*)realloc(array->vals, array->len * sizeof(jval_t));
    array->cap = array->len;
}

//------------------------------------------------------------------------------
void jarray_reserve( _jarray_t* a, size_t cap )
{
    if ( a->len+cap < a->cap )
        return;

    size_t prev_cap = a->cap;

    a->cap = (jsize_t)grow(a->len+cap, a->cap);
    if (prev_cap <= BUF_SIZE)
    {
        jval_t* vals = (jval_t*)malloc( a->cap * sizeof(jkv_t) );
        memcpy(vals, a->buf, a->len * sizeof(jval_t));
        a->vals = vals;
    }
    else
    {
        assert(a->vals);
        a->vals = (jval_t*)realloc( a->vals, a->cap * sizeof(jkv_t) );
    }
}

//------------------------------------------------------------------------------
jval_t* jarray_get_val( _jarray_t* a, size_t idx)
{
    assert(a);
    assert(idx < a->len);
    return (a->cap > BUF_SIZE) ? &a->vals[idx] : &a->buf[idx];
}

//------------------------------------------------------------------------------
jval_t* jarray_add_val( _jarray_t* a)
{
    assert(a);
    jarray_reserve(a, 1);
    jval_t* val = jarray_get_val(a, a->len++);
    *val = {0}; // clear out
    return val;
}

//------------------------------------------------------------------------------
void jarray_add_num( jarray_t _a, jnum_t num )
{
    size_t idx = json_add_num(_a.json, num);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_NUM;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_str( jarray_t _a, const char* str )
{
    size_t idx = json_add_str(_a.json, str);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_STR;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_bool( jarray_t _a, jbool b )
{
    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    val->idx = 0;
}

//------------------------------------------------------------------------------
void jarray_add_nil( jarray_t _a )
{
    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_NIL;
    val->idx = 0;
}

//------------------------------------------------------------------------------
jarray_t jarray_add_array( jarray_t _a )
{
    size_t idx = json_add_array(_a.json);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_ARRAY;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jarray_t){ _a.json, idx };
}

//------------------------------------------------------------------------------
jobj_t jarray_add_obj( jarray_t _a )
{
    size_t idx = json_add_obj(_a.json);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_OBJ;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jobj_t){ _a.json, idx };
}

//------------------------------------------------------------------------------
static inline void jarray_print(jarray_t _root, size_t depth, FILE* f)
{
    json_fprintf(f, "[");

    _jarray_t* root = jarray_get_array(_root);

    json_t* jsn = jarray_get_json(root);
    for ( size_t i = 0; i < root->len; ++i )
    {
        jval_t* val = jarray_get_val(root, i);

        json_fprintf(f, "\n");
        print_tabs(depth+1, f);

        switch (val->type)
        {
            case JTYPE_NIL:
                json_fprintf(f, "nil");
                break;

            case JTYPE_STR:
                jstr_print(jmap_get_str(&jsn->strmap, val->idx), f);
                break;

            case JTYPE_NUM:
            {
                jnum_t num = jsn->nums[val->idx];
                jnum_t fract = num - floor(num);
                json_fprintf(f, (fract > 0) ? "%f" : "%0.0f", num);
                break;
            }

            case JTYPE_ARRAY:
                jarray_print((jarray_t){jsn, val->idx}, depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print((jobj_t){jsn, val->idx}, depth+1, f);
                break;

            case JTYPE_TRUE:
                json_fprintf(f, "true");
                break;

            case JTYPE_FALSE:
                json_fprintf(f, "false");
                break;

            default:
                break;
        }

        // trailing comma
        if (i+1 < root->len) json_fprintf(f, ",");
    }

    json_fprintf(f, "\n");
    print_tabs(depth, f);
    json_fprintf(f, "]");
}

#pragma mark - jbuf_t

//------------------------------------------------------------------------------
void jbuf_init(jbuf_t* buf)
{
    assert(buf);
    buf->cap = 0;
    buf->len = 0;
    buf->ptr = NULL;
}

//------------------------------------------------------------------------------
void jbuf_destroy(jbuf_t* buf)
{
    assert(buf);
    free(buf->ptr);
    jbuf_init(buf);
}

//------------------------------------------------------------------------------
static inline void jbuf_clear( jbuf_t* buf )
{
    if (buf->ptr && buf->len > 0)
        buf->ptr[0] = '\0';

    buf->len = 0;
}

//------------------------------------------------------------------------------
static inline void jbuf_reserve( jbuf_t* buf, size_t cap )
{
    if (buf->len+cap < buf->cap)
        return;

    buf->cap = grow(buf->len+cap, buf->cap);
    buf->ptr = (char*)realloc(buf->ptr, buf->cap * sizeof(char));
    assert (buf->ptr);
}

//------------------------------------------------------------------------------
static inline void jbuf_add( jbuf_t* buf, char ch )
{
    jbuf_reserve(buf, 1);
    buf->ptr[buf->len++] = ch;
}

#pragma mark - json_t

//------------------------------------------------------------------------------
json_t* json_new()
{
    json_t* j = (json_t*)calloc(1, sizeof(json_t));

    jmap_init(&j->strmap);

    // nums
    j->nums_len = 0;
    j->nums_cap = 0;
    j->nums = NULL;

    // arrays
    j->arrays = NULL;
    j->objs = NULL;

    jbuf_init(&j->keybuf);
    jbuf_init(&j->valbuf);

    size_t idx = json_add_obj(j);
    assert(idx == 0);
    return j;
}

//------------------------------------------------------------------------------
void json_parse_buf( json_t* jsn, void* ptr, size_t len )
{
    jmap_rehash(&jsn->strmap, ceilf(len*0.01));
    json_nums_reserve(jsn, ceilf(len*0.01));
    json_arrays_reserve(jsn, ceilf(len*0.01));
    json_objs_reserve(jsn, ceilf(len*0.01));

    const char* cptr = (const char*)ptr;
    parse_obj(json_root(jsn), cptr, cptr+len);
    jbuf_destroy(&jsn->keybuf);
    jbuf_destroy(&jsn->valbuf);
}


//------------------------------------------------------------------------------
void json_parse_path( json_t* jsn, const char* path )
{
    int fd = open(path, O_RDONLY);
    assert(fd);

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        assert(false);
    }

    printf("[FILE_SIZE]: %0.1f MB\n", btomb(st.st_size) );

    void* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_SHARED|MAP_NORESERVE, fd, 0);
    posix_madvise(ptr, st.st_size, POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
    json_parse_buf(jsn, ptr, st.st_size);
    munmap(ptr, st.st_size);
}

//------------------------------------------------------------------------------
void json_print(json_t* j, FILE* f)
{
    jobj_print(json_root(j), 0, f);
    json_fprintf(f, "\n");
}

//------------------------------------------------------------------------------
void json_destroy(json_t* j)
{
    assert(j);

    // cleanup string map
    jmap_destroy(&j->strmap);

    // cleanup numbers
    free(j->nums); j->nums = NULL;

    if (j->objs)
    {
        // cleanup objects
        for ( size_t i = 0; i < j->objs->len; i++ )
        {
            _jobj_t* obj = json_get_obj(j, i);
            if (obj->cap > BUF_SIZE)
            {
                free(obj->kvs); obj->kvs = NULL;
            }
        }
        free(j->objs); j->objs = NULL;
    }

    if (j->arrays)
    {
        // cleanup arrays
        for ( size_t i = 0; i < j->arrays->len; i++ )
        {
            _jarray_t* array = json_get_array(j, i);
            if (array->cap > BUF_SIZE)
            {
                free(array->vals); array->vals = NULL;
            }
        }
        free(j->arrays); j->arrays = NULL;
    }

    // cleanup the buffers
    jbuf_destroy(&j->keybuf);
    jbuf_destroy(&j->valbuf);
}

//------------------------------------------------------------------------------
void json_free(json_t* j)
{
    json_destroy(j);
    free(j);
}

//------------------------------------------------------------------------------
jobj_t json_root(json_t* j)
{
    return (jobj_t){j, 0};
}

#pragma mark - parse

//------------------------------------------------------------------------------
template < typename T >
inline void eat_whitespace( T& beg, const T& end )
{
    for (; beg != end; ++beg )
    {
        switch(*beg)
        {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
            case '\v':
            case '\f':
                break;

            default:
                return;
        }
    }
}

//------------------------------------------------------------------------------
template < typename T >
inline jnum_t parse_sign( T& beg, const T& end )
{
    json_assert(beg != end, "unexpected end of file");
    switch (*beg)
    {
        case '-':
            ++beg;
            return -1;

        case '+':
            ++beg;
            return 1;

        default:
            return 1;
    }
}

//------------------------------------------------------------------------------
static jnum_t __ignore = 0;
template < typename T >
inline jnum_t parse_digits( T& beg, const T& end, jnum_t& places = __ignore)
{
    json_assert(beg != end, "unexpected end of file");

    jnum_t num = 0;
    for (size_t cnt = 0; beg != end; ++beg, ++cnt)
    {
        switch (*beg)
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            {
                num = num*10 + (*beg - '0');
                break;
            }

            default:
            {
                places = pow(10, cnt);
                return num;
            }
        }
    }

    json_assert(false, "unexpected end of file");
    return num;
}


//------------------------------------------------------------------------------
template < typename T >
inline jnum_t parse_number( T& beg, const T& end )
{
    json_assert(beg != end, "unexpected end of file");

    // +/-
    jnum_t sign = parse_sign(beg, end);

    // whole number
    jnum_t num = parse_digits(beg, end);

    // fraction
    switch (*beg)
    {
        case '.':
        {
            jnum_t places = 1;
            jnum_t fract = parse_digits(++beg, end, places);
            num += fract / places;
            break;
        }

        default:
            break;
    }

    // scientific notation
    switch (*beg)
    {
        case 'e':
        case 'E':
        {
            jnum_t esign = parse_sign(++beg, end);
            jnum_t digits = parse_digits(beg, end);
            num *= pow(10, esign*digits);
            break;
        }

        default:
            break;
    }

    // apply sign
    return sign * num;
}

//------------------------------------------------------------------------------
template < typename T >
inline unsigned char char_to_hex(T& beg, const T& end)
{
    json_assert( beg != end, "unexpected end of stream while parsing unicode escape");
    switch (*beg)
    {
        case '0': return 0x0;
        case '1': return 0x1;
        case '2': return 0x2;
        case '3': return 0x3;
        case '4': return 0x4;
        case '5': return 0x5;
        case '6': return 0x6;
        case '7': return 0x7;
        case '8': return 0x8;
        case '9': return 0x9;
        case 'A':
        case 'a': return 0xA;
        case 'B':
        case 'b': return 0xB;
        case 'C':
        case 'c': return 0xC;
        case 'D':
        case 'd': return 0xD;
        case 'E':
        case 'e': return 0xE;
        case 'F':
        case 'f': return 0xF;
        default: json_assert(false, "invalid unicode hex digit: '%c'", *beg);
    }
    return 0;
}

//------------------------------------------------------------------------------
static inline void utf8_encode(int32_t codepoint, jbuf_t* str )
{
    json_assert(codepoint > 0, "invalid unicode");
    if(codepoint < 0x80)
    {
        jbuf_add(str, (char)codepoint);
    }
    else if(codepoint < 0x800)
    {
        jbuf_add(str, 0xC0 + ((codepoint & 0x7C0) >> 6));
        jbuf_add(str, 0x80 + ((codepoint & 0x03F)));
    }
    else if(codepoint < 0x10000)
    {
        jbuf_add(str, 0xE0 + ((codepoint & 0xF000) >> 12));
        jbuf_add(str, 0x80 + ((codepoint & 0x0FC0) >> 6));
        jbuf_add(str, 0x80 + ((codepoint & 0x003F)));
    }
    else if(codepoint <= 0x10FFFF)
    {
        jbuf_add(str, 0xF0 + ((codepoint & 0x1C0000) >> 18));
        jbuf_add(str, 0x80 + ((codepoint & 0x03F000) >> 12));
        jbuf_add(str, 0x80 + ((codepoint & 0x000FC0) >> 6));
        jbuf_add(str, 0x80 + ((codepoint & 0x00003F)));
    }
    else
    {
        json_assert(false, "invalid unicode");
    }
}

//------------------------------------------------------------------------------
template < typename T >
inline unsigned int read_unicode_hex(T& beg, const T& end)
{
    return  char_to_hex(beg, end) << 12 |
            char_to_hex(beg, end) << 8 |
            char_to_hex(beg, end) << 4 |
            char_to_hex(beg, end);
}

//------------------------------------------------------------------------------
template < typename T >
inline void parse_unicode( jbuf_t* str, T& beg, const T& end )
{
    // U+XXXX
    unsigned int val = read_unicode_hex(beg, end);
//    json_error(val > 0, is, "\\u0000 is not allowed");

    // surrogate pair, \uXXXX\uXXXXX
    if (0xD800 <= val && val <= 0xDBFF)
    {
        json_assert(*beg == '\\', "invalid unicode"); ++beg;
        json_assert(beg != end, "unexpected end of stream");
        json_assert(*beg == 'u', "invalid unicode"); ++beg;

        // read the surrogate pair from the stream
        unsigned int val2 = read_unicode_hex(beg, end);

        // validate the value
        json_assert(val2 < 0xDC00 || val2 > 0xDFFF, "invalid unicode");
        unsigned int unicode = ((val - 0xD800) << 10) + (val2 - 0xDC00) + 0x10000;
        utf8_encode(unicode, str);
        return;
    }

    json_assert(0xDC00 > val || val > 0xDFFF, "invalid unicode");
    utf8_encode(val, str);
}


//------------------------------------------------------------------------------
template < typename T >
inline void parse_string( T& beg, const T& end, jbuf_t* str )
{
    json_assert(beg != end, "unexpected end of file");
    json_assert(*beg == '"', "not a valid string: '%c'", *beg);

    jbuf_clear(str);

    char ch;
    char prev = *beg;
    for (++beg; beg != end; ++beg )
    {
        ch = *beg;
        switch (prev)
        {
            case '\\':
            {
                switch(ch)
                {
                    case '/':
                        jbuf_add(str, '/');
                        break;
                    case 'b':
                        jbuf_add(str, '\b');
                        break;
                    case 'f':
                        jbuf_add(str, '\f');
                        break;
                    case 'n':
                        jbuf_add(str, '\n');
                        break;
                    case 'r':
                        jbuf_add(str, '\r');
                        break;
                    case 't':
                        jbuf_add(str, '\t');
                        break;
                    case 'u':
                        parse_unicode(str, beg, end);
                        break;
                    case '"':
                        jbuf_add(str, '\"');
                        break;
                    case '\\':
                        jbuf_add(str, '\\');
                        ch = 0; // reset the character
                        break;

                    default:
                        json_assert(false, "invalid escape sequence '\\%c'", ch);
                        break;
                }
                break;
            }

            default:
            {
                switch (*beg)
                {
                    case '\\':
                        break;

                    case '"':
                        ++beg;
                        jbuf_add(str, '\0');
                        return;

                    default:
                        jbuf_add(str, ch);
                        break;
                }
                break;
            }
        }
        prev = ch;
    }

    json_assert(false, "string terminated unexpectedly");
    jbuf_add(str, '\0');
}


//------------------------------------------------------------------------------
template < typename T >
inline bool parse_true( T& beg, const T& end )
{
    json_assert(*++beg == 'r' && beg != end &&
                *++beg == 'u' && beg != end &&
                *++beg == 'e' && beg != end,
        "expected literal 'true'");
    ++beg;
    return true;
}

//------------------------------------------------------------------------------
template < typename T >
inline bool parse_false( T& beg, const T& end )
{
    json_assert(*++beg == 'a' && beg != end &&
                *++beg == 'l' && beg != end &&
                *++beg == 's' && beg != end &&
                *++beg == 'e' && beg != end,
        "expected literal 'true'");
    ++beg;
    return false;
}

//------------------------------------------------------------------------------
template < typename T >
inline void* parse_null( T& beg, const T& end )
{
    json_assert(*++beg == 'u' && beg != end &&
                *++beg == 'l' && beg != end &&
                *++beg == 'l' && beg != end,
        "expected literal 'true'");
    ++beg;
    return NULL;
}

//------------------------------------------------------------------------------
template < typename T >
inline void parse_array( jarray_t a, T& beg, const T& end )
{
    json_assert(beg != end, "unexpected end of file");
    while (++beg != end)
    {
        eat_whitespace(beg, end);
        switch ( *beg )
        {
            case '{': // object
            {
                parse_obj(jarray_add_obj(a), beg, end);
                break;
            }

            case '[': // array
            {
                parse_array(jarray_add_array(a), beg, end);
                break;
            }

            case '"': // string
            {
                jbuf_t* buf = &a.json->valbuf;
                parse_string(beg, end, buf);
                jarray_add_str(a, buf->ptr);
                break;
            }

            case 't': // true
                jarray_add_bool(a, true);
                break;

            case 'f': // false
                jarray_add_bool(a, false);
                break;

            case 'n': // null
                parse_null(beg, end);
                jarray_add_nil(a);
                break;

            case '-': // number
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                jarray_add_num(a, parse_number(beg, end));
                break;

            default:
                json_assert(false, "invalid character: '%c'", *beg);
                break;
        }
        eat_whitespace(beg, end);

        // either another or the end
        switch (*beg)
        {
            case ']':
                ++beg;
                jarray_truncate(jarray_get_array(a));
                return;

            case ',':
                break;

            default:
                json_assert(false, "");
        }
    }

    json_assert(false, "unexpected end of stream while reading array");
}

//------------------------------------------------------------------------------
template < typename T >
inline void parse_obj( jobj_t obj, T& beg, const T& end )
{
    json_t* jsn = obj.json;

    json_assert(beg != end, "unexpected end of file");
    while (++beg != end)
    {
        eat_whitespace(beg, end);

        parse_string(beg, end, &jsn->keybuf);
        const char* key = jsn->keybuf.ptr;

        eat_whitespace(beg, end);
        json_assert(*beg == ':', "invalid character '%c', following key, expected: ':'", *beg); ++beg;
        eat_whitespace(beg, end);

        switch ( *beg )
        {
            case '{': // object
            {
                parse_obj(jobj_add_obj(obj, key), beg, end);
                break;
            }

            case '[': // array
            {
                parse_array(jobj_add_array(obj, key), beg, end);
                break;
            }

            case '"': // string
            {
                parse_string(beg, end, &jsn->valbuf);
                jobj_add_str(obj, key, jsn->valbuf.ptr);
                break;
            }

            case 't': // true
                parse_true(beg, end);
                jobj_add_bool(obj, key, true);
                break;

            case 'f': // false
                parse_false(beg, end);
                jobj_add_bool(obj, key, false);
                break;

            case 'n': // null
                parse_null(beg, end);
                jobj_add_nil(obj, key);
                break;

            case '-': // number
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                jobj_add_num(obj, key, parse_number(beg, end));
                break;

            default:
                json_assert(false, "invalid character: '%c'", *beg);
                break;
        }
        eat_whitespace(beg, end);

        // either another or the end
        switch (*beg)
        {
            case '}': // end of object
                ++beg;
                jobj_truncate(jobj_get_obj(obj));
                return;

            case ',': // another key
                break;

            default:
                json_assert(false, "unexpected character: '%c'", *beg);
                break;
        }
    }

    json_assert(false, "unexpected end of file");
}

#pragma mark - io

class stdfile
{
public:
    stdfile()
        : m_file(NULL)
        , m_off(0)
        , m_pos(0)
        , m_buflen(0)
    {}

    stdfile( FILE* file )
        : m_file(file)
        , m_off(0)
        , m_pos(0)
        , m_buflen(0)
    {
        assert(m_file);
        m_buflen = fread(m_buf, 1, sizeof(m_buf), m_file);
        assert(m_buflen);
    }

    stdfile( const char* path )
        : m_file(NULL)
        , m_off(0)
        , m_pos(0)
        , m_buflen(0)
    {
        m_file = fopen(path, "r");
        assert(m_file);
        m_buflen = fread(m_buf, 1, sizeof(m_buf), m_file);
        assert(m_buflen);
    }

    stdfile ( stdfile&& mv )
        : m_file(mv.m_file)
        , m_off(mv.m_off)
        , m_buflen(mv.m_buflen)
        , m_pos(mv.m_pos)
    {
        memcpy(m_buf, mv.m_buf, sizeof(m_buf));
        mv.m_file = NULL;
        mv.m_off = 0;
    }

    stdfile& operator= ( stdfile&& mv )
    {
        m_file = mv.m_file;
        m_off = mv.m_off;
        m_buflen = mv.m_buflen;
        m_pos = mv.m_pos;
        memcpy(m_buf, mv.m_buf, sizeof(m_buf));

        mv.m_file = NULL;
        mv.m_off = 0;
        return *this;
    }

    stdfile( const stdfile& ) = delete;
    stdfile& operator= ( const stdfile& ) = delete;

    ~stdfile()
    {
        if (m_file) fclose(m_file);
    }

    bool operator== ( const stdfile& rhs ) const
    {
        return (m_file == rhs.m_file && m_off == rhs.m_off);
    }

    bool operator!= ( const stdfile& rhs ) const { return !this->operator==(rhs); }

//    memfile operator ++(int) // postfix ++
//    {
//        memfile copy = *this;
//        ++*this;
//        return copy;
//    }

    stdfile& operator ++() // ++ prefix
    {
        assert(m_buflen > 0);
        if (++m_pos >= m_buflen)
        {
            m_off += m_pos;
            m_buflen = fread(m_buf, 1, sizeof(m_buf), m_file);
            m_pos = 0;

            if (m_buflen == 0)
            {
                fclose(m_file);
                m_file = NULL;
                m_off = 0;
            }
        }

        return *this;
    }

    const char& operator*() const
    {
        return m_buf[m_pos];
    }

protected:
    FILE* m_file;

    size_t m_buflen;
    size_t m_pos;
    char m_buf[4096];

    size_t m_off;
};


class memfile
{
public:
    memfile()
        : m_cur(NULL)
        , m_beg(NULL)
        , m_end(NULL)
        , m_off(0)
        , m_len(0)
        , m_fd(0)
    {}

    memfile( const char* path )
        : m_cur(NULL)
        , m_beg(NULL)
        , m_end(NULL)
        , m_off(0)
        , m_len(0)
        , m_fd(0)
    {
        m_fd = open(path, O_RDONLY);
        assert(m_fd);

        struct stat st;
        int rt = fstat(m_fd, &st);
        assert (rt == 0);
        m_len = st.st_size;

        size_t psize = getpagesize();

        size_t len = MIN(m_len, psize);
        m_beg = (char*)mmap(NULL, len, PROT_READ, MAP_SHARED, m_fd, m_off);
        m_cur = m_beg;
        m_end = m_beg + len;

        posix_madvise(m_beg, len, POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
    }

    memfile ( memfile&& mv )
        : m_cur(mv.m_cur)
        , m_beg(mv.m_beg)
        , m_end(mv.m_end)
        , m_off(mv.m_off)
        , m_len(mv.m_len)
        , m_fd(mv.m_fd)
    {
        mv.m_cur = mv.m_beg = mv.m_end = NULL;
        mv.m_off = 0;
        mv.m_len = 0;
        mv.m_fd = 0;
    }

    memfile& operator= ( memfile&& mv )
    {
        m_cur = mv.m_cur;
        m_beg = mv.m_beg;
        m_end = mv.m_end;
        m_off = mv.m_off;
        m_len = mv.m_len;
        m_fd = mv.m_fd;
        mv.m_cur = mv.m_beg = mv.m_end = NULL;
        mv.m_off = 0;
        mv.m_len = 0;
        mv.m_fd = 0;
        return *this;
    }

    memfile( const memfile& ) = delete;
    memfile& operator= ( const memfile& ) = delete;

    ~memfile()
    {
        if (m_fd) close(m_fd);
        if (m_beg) munmap(m_beg, m_end-m_beg);
    }

    bool operator== ( const memfile& rhs ) const
    {
        return (m_fd == rhs.m_fd && m_off == rhs.m_off && m_len == rhs.m_len && (m_cur-m_beg) == (rhs.m_cur-m_beg));
    }

    bool operator!= ( const memfile& rhs ) const { return !this->operator==(rhs); }

//    memfile operator ++(int) // postfix ++
//    {
//        memfile copy = *this;
//        ++*this;
//        return copy;
//    }

    memfile& operator ++() // ++ prefix
    {
        if (++m_cur == m_end)
        {
            // done with this page
            size_t len = m_end - m_beg;
            munmap(m_beg, len);
            m_off += len;

            if (m_off >= m_len)
            {
                m_cur = m_beg = m_end = NULL;
                m_len = 0;
                m_off = 0;
                m_fd = 0;
            }
            else
            {
                len = MIN(len, m_len-m_off);
                assert (len > 0);

                m_beg = (char*)mmap(NULL, len, PROT_READ, MAP_SHARED, m_fd, m_off);
                m_cur = m_beg;
                m_end = m_beg + len;

                posix_madvise(m_beg, len, POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
            }
        }
        return *this;
    }

    const char& operator*() const
    {
        return *m_cur;
    }

protected:
    int m_fd;
    size_t m_off;
    size_t m_len;
    char* m_cur;
    char* m_beg;
    char* m_end;
};

#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include<mach/mach.h>

//------------------------------------------------------------------------------
void print_mem_usage()
{
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
    printf("[RAM]: Actual: %0.1f MB Virtual: %0.1f MB\n", btomb(t_info.resident_size), btomb(t_info.virtual_size));
}

//------------------------------------------------------------------------------
void json_parse_file( json_t* jsn, FILE* file )
{
    stdfile beg = file;
    stdfile end;

    print_mem_usage();
    parse_obj(json_root(jsn), beg, end);
    print_mem_usage();
}

//------------------------------------------------------------------------------
json_t* json_load_file( const char* path )
{

#define PAGED_READ      0x1
#define STD_READ        0x2
#define ONESHOT_READ    0x4

#define READ_METHOD ONESHOT_READ

#if READ_METHOD == PAGED_READ
    printf("Parsing json using [paged mmap] method\n");

    memfile beg = path;
    memfile end;

    print_mem_usage();
    json_t* jsn = json_new();
    parse_obj(json_root(jsn), beg, end);
    print_mem_usage();

#elif READ_METHOD == STD_READ
    printf("Parsing json using [fread] method\n");

    FILE* file = fopen(path, "r");
    assert(file);

    json_t* jsn = json_new();
    print_mem_usage();
    json_parse_file(jsn, file);
    print_mem_usage();

#elif READ_METHOD == ONESHOT_READ
    printf("Parsing json using [mmap] method\n");

    json_t* jsn = json_new();
    print_mem_usage();
    json_parse_path(jsn, path);
    print_mem_usage();

#else
    #error must specify the file read method
#endif

#define PRINT_MEMORY 1
#if PRINT_MEMORY

//    printf("Len: %zu Cap: %zu Load factor: %f\n", jsn->map.len, jsn->map.cap, jsn->map.len / (double)jsn->map.cap);
//    printf("String memory: %0.1f MB\n", btomb(jsn->bytes));

    size_t total = 0;
    size_t total_reserve = 0;

    if (jsn->arrays)
    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->arrays->len; ++i )
        {
            _jarray_t* array = json_get_array(jsn, i);
            if (array->cap > BUF_SIZE)
            {
                mem += array->len * sizeof(jval_t);
                reserve += array->cap * sizeof(jval_t);
            }
        }
        mem += jsn->arrays->len * sizeof(_jarray_t);
        reserve += jsn->arrays->cap * sizeof(_jarray_t);
        printf("[ARRAY] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->arrays->len);

        total += mem;
        total_reserve += reserve;
    }

    if (jsn->objs)
    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->objs->len; ++i )
        {
            _jobj_t* obj = json_get_obj(jsn, i);
            if (obj->cap > BUF_SIZE)
            {
                mem += obj->len * sizeof(jkv_t);
                reserve += obj->cap * sizeof(jkv_t);
            }
        }
        mem += jsn->objs->len * sizeof(_jobj_t);
        reserve += jsn->objs->cap * sizeof(_jobj_t);
        printf("[OBJECT] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->objs->len);

        total += mem;
        total_reserve += reserve;
    }

    if (jsn->strmap.strs)
    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->strmap.slen; ++i )
        {
            jstr_t* str = jmap_get_str(&jsn->strmap, i);
            if (str->len > BUF_SIZE)
            {
                mem += str->len*sizeof(char);
                reserve += str->len*sizeof(char);
            }
        }
        mem += jsn->strmap.slen * sizeof(jstr_t);
        reserve += jsn->strmap.scap * sizeof(jstr_t);

        // hashmap memory usage
        mem += jsn->strmap.bcap * sizeof(jmapbucket_t);
        reserve += jsn->strmap.bcap * sizeof(jmapbucket_t);
        for ( size_t i = 0; i < jsn->strmap.bcap; ++i )
        {
            jmapbucket_t* bucket = &jsn->strmap.buckets[i];
            if (bucket->slots)
            {
                mem += bucket->len*sizeof(size_t);
                reserve += bucket->cap*sizeof(size_t);
            }
        }

        printf("[STRING] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100);

        total += mem;
        total_reserve += reserve;
    }

    {
        size_t mem = 0;
        size_t reserve = 0;
        mem += jsn->nums_len * sizeof(jnum_t);
        reserve += jsn->nums_cap * sizeof(jnum_t);

        printf("[NUMBERS] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100);

        total += mem;
        total_reserve += reserve;
    }

    printf("[TOTAL] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(total), btomb(total_reserve), total / (double)total_reserve * 100);
    print_mem_usage();
#endif

    return jsn;
}
