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

#define STD_READ        0x1
#define ONESHOT_READ    0x2
#define PRINT_MEMORY    1
#define READ_METHOD     STD_READ

#pragma mark - constants

#define JINLINE static inline

#define BUF_SIZE ((size_t)6)
#define MAX_VAL_IDX 268435456 // 2^28
#define MAX_KEY_IDX UINT32_MAX

#define JMAP_MAX_LOADFACTOR 0.8f
#define JMAP_IDEAL_LOADFACTOR 0.3f

// pack short keys directly into the jkv_t struct if possible
#define PACK_KEYS 1

static const jbool jtrue = 1;
static const jbool jfalse = 0;

#pragma mark - structs

typedef uint32_t jhash_t;

//------------------------------------------------------------------------------
JINLINE void* jcalloc( size_t s, size_t n)
{
    assert(s>0 && n>0);
    void* ptr = calloc(s, n);
    assert(ptr);
    return ptr;
}

//------------------------------------------------------------------------------
JINLINE void jfree( void* ptr )
{
    free(ptr);
}

//------------------------------------------------------------------------------
JINLINE void* jmalloc( size_t s )
{
    assert(s > 0);
    void* ptr = malloc(s);
    assert(ptr);
    return ptr;
}

//------------------------------------------------------------------------------
JINLINE void* jrealloc( void* ptr, size_t s )
{
    void* rt = realloc(ptr, s);
    assert(rt);
    return rt;
}

//------------------------------------------------------------------------------
struct jstr_t
{
    jsize_t len;
    jhash_t hash;
    union
    {
        char* chars;
        char buf[BUF_SIZE+1];
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
    union
    {
        uint32_t _key;
        char kstr[4]; // used to pack short keys into the value directly
    };
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
JINLINE void _jarray_print(_jarray_t* root, size_t depth, FILE* f);
size_t jmap_add_str(jmap_t* map, const char* cstr, size_t slen);
void print_memory_stats(json_t*);

#pragma mark - util

//------------------------------------------------------------------------------
JINLINE void print_tabs( size_t cnt, FILE* f)
{
    while (cnt--)
    {
        json_fprintf(f, "   ");
    }
}

//------------------------------------------------------------------------------
JINLINE jnum_t btomb(size_t bytes)
{
    return (bytes / (jnum_t)(1024*1024));
}

//------------------------------------------------------------------------------
JINLINE size_t grow( size_t min, size_t cur )
{
    static const jnum_t GROWTH_FACTOR = 1.1;
    assert(min >= cur);

    size_t size = MAX(13, min);
    size = MAX(size, BUF_SIZE*2);
    size = MAX(size, cur*GROWTH_FACTOR+2);
    assert(size > cur);
    return size;
}

#pragma mark - jstr_t

//------------------------------------------------------------------------------
JINLINE jhash_t murmur3_32(const char *key, size_t len, jhash_t seed)
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

//------------------------------------------------------------------------------
JINLINE jhash_t jstr_hash( const char* key, size_t len )
{
    static const jhash_t MURMER32_SEED = 0;
    static const size_t MAX_CHARS = 32;
    return murmur3_32(key, MIN(MAX_CHARS, len), MURMER32_SEED);
}

//------------------------------------------------------------------------------
void jstr_init_str_hash( jstr_t* jstr, const char* cstr, size_t len, jhash_t hash )
{
    assert(jstr);
    assert(cstr);
    // TODO: verify the length does not exceed our max

    jstr->len = (jsize_t)len;
    jstr->hash = hash;
    if (len > BUF_SIZE)
    {
        char* buf = (char*)jmalloc( len * sizeof(char) + 1 );
        memcpy(buf, cstr, len * sizeof(char));
        buf[len] = '\0';
        jstr->chars = buf;
    }
    else
    {
        memcpy(jstr->buf, cstr, len * sizeof(char));
        jstr->buf[len] = '\0';
    }
}

//------------------------------------------------------------------------------
void jstr_init_str( jstr_t* jstr, const char* cstr, size_t len )
{
    assert(jstr);
    assert(cstr);
    jstr_init_str_hash(jstr, cstr, len, jstr_hash(cstr, len));
}

#pragma mark - jmap_t

//------------------------------------------------------------------------------
void jmap_init(jmap_t* map)
{
    assert(map);

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
    assert(map);

    // clean up bucket slots
    for ( size_t i = 0; i < map->bcap; i++ )
    {
        jmapbucket_t* bucket = &map->buckets[i];
        if (bucket->slots)
        {
            jfree(bucket->slots); bucket->slots = NULL;
        }
    }

    // clean up buckets
    jfree(map->buckets); map->buckets = NULL;

    if (map->strs)
    {
        // cleanup strings
        for ( size_t i = 0; i < map->slen; i++ )
        {
            jstr_t* str = &map->strs[i];
            if (str->len > BUF_SIZE)
            {
                jfree(str->chars);
            }
        }
        jfree(map->strs); map->strs = NULL;
    }
    map->slen = map->scap = 0;
    map->blen = map->bcap = 0;
}

//------------------------------------------------------------------------------
void jmap_reserve_str( jmap_t* map, size_t len )
{
    assert(map);

    // not found, create a new one
    if (map->slen+len >= map->scap)
    {
        map->scap = grow(map->slen+len, map->scap);
        map->strs = (jstr_t*)jrealloc(map->strs, sizeof(jstr_t)*map->scap);
    }
}

//------------------------------------------------------------------------------
size_t _jmap_add_str(jmap_t* map, const char* cstr, size_t len, uint32_t hash)
{
    assert(map);
    assert(cstr);

    // not found, create a new one
    jmap_reserve_str(map, 1);

    size_t idx = map->slen++;
    jstr_init_str_hash(&map->strs[idx], cstr, len, hash);
    return idx;
}

//------------------------------------------------------------------------------
void jmap_bucket_reserve( jmap_t* map, size_t idx, size_t len )
{
    assert(map);
    assert(idx < map->bcap);

    jmapbucket_t* bucket = &map->buckets[idx];
    if ( bucket->len+len >= bucket->cap )
    {
        if (!bucket->slots) map->blen++; // empty bucket, we are about to add it, increment
        bucket->cap = grow(bucket->len+len, bucket->cap);
        bucket->slots = (size_t*)jrealloc( bucket->slots, sizeof(size_t) * bucket->cap );
    }
}

//------------------------------------------------------------------------------
void _jmap_add_key(jmap_t* map, uint32_t hash, size_t val)
{
    assert(map);
    assert(map->buckets);
    assert(map->bcap > 0);

    size_t idx = hash % map->bcap;
    jmap_bucket_reserve(map, idx, 1);
    jmapbucket_t* bucket = &map->buckets[idx];
    bucket->slots[bucket->len++] = val;
}

//------------------------------------------------------------------------------
void jmap_rehash(jmap_t* map, size_t hint)
{
    assert(map);

    // if there is an empty hashmap, no need to check the load factor!
    if (map->bcap > 0)
    {
        float load = map->blen / (float)map->bcap;
        if (load <= JMAP_MAX_LOADFACTOR)
            return;
    }

    // TODO: reuse allocations

    size_t target = ceilf(map->bcap / JMAP_IDEAL_LOADFACTOR);

    jmap_t copy = {0};
    copy.bcap = MAX( MAX(hint, 13), target);
    copy.blen = 0;

    copy.slen = map->slen;
    copy.scap = map->scap;

    copy.buckets = (jmapbucket_t*)jcalloc(copy.bcap, sizeof(jmapbucket_t));
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
    assert(map);
    assert(cstr);

    jhash_t hash = jstr_hash(cstr, slen);

    jmap_rehash(map, 0);

    assert(map->bcap > 0);
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
            assert(chars);
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

//------------------------------------------------------------------------------
static void json_objs_reserve( json_t* jsn, size_t res )
{
    assert(jsn);
    if (!jsn->objs)
    {
        size_t cap = grow(res, 0);
        jlist_t* objs = (jlist_t*)jmalloc(sizeof(jlist_t) + sizeof(_jobj_t)*cap);
        objs->len = 0;
        objs->cap = cap;
        objs->json = jsn;
        jsn->objs = objs;
        return;
    }

    if (jsn->objs->len+res < jsn->objs->cap)
        return;

    size_t cap = grow(jsn->objs->len+res, jsn->objs->cap);
    jsn->objs = (jlist_t*)jrealloc(jsn->objs, sizeof(jlist_t) + sizeof(_jobj_t)*cap);
    jsn->objs->cap = cap;
}

//------------------------------------------------------------------------------
static _jobj_t* json_get_obj( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert(jsn->objs);
    assert(idx < jsn->objs->len);
    return ((_jobj_t*)jsn->objs->data) + idx;
}

//------------------------------------------------------------------------------
static size_t json_add_obj( json_t* jsn )
{
    assert(jsn);
    json_objs_reserve(jsn, 1);
    assert(jsn->objs);

    size_t idx = jsn->objs->len++;
    _jobj_t* obj = json_get_obj(jsn, idx);
    assert(obj);
    obj->cap = BUF_SIZE;
    obj->len = 0;
    obj->idx = (jsize_t)idx;
    return idx;
}

//------------------------------------------------------------------------------
static void json_arrays_reserve( json_t* jsn, size_t res )
{
    assert(jsn);
    if (!jsn->arrays)
    {
        size_t cap = grow(res, 0);
        jlist_t* arrays = (jlist_t*)jmalloc(sizeof(jlist_t) + sizeof(_jarray_t)*cap);
        arrays->len = 0;
        arrays->cap = cap;
        arrays->json = jsn;
        jsn->arrays = arrays;
        return;
    }

    if (jsn->arrays->len+res < jsn->arrays->cap)
        return;

    size_t cap = grow(jsn->arrays->len+res, jsn->arrays->cap);
    jsn->arrays = (jlist_t*)jrealloc(jsn->arrays, sizeof(jlist_t) + sizeof(_jarray_t)*cap);
    jsn->arrays->cap = cap;
}

//------------------------------------------------------------------------------
static _jarray_t* json_get_array( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert(jsn->arrays);
    assert(idx < jsn->arrays->len);
    return ((_jarray_t*)jsn->arrays->data) + idx;
}

//------------------------------------------------------------------------------
static size_t json_add_array( json_t* jsn )
{
    assert(jsn);
    json_arrays_reserve(jsn, 1);
    assert(jsn->arrays);

    size_t idx = jsn->arrays->len++;
    _jarray_t* array = json_get_array(jsn, idx);
    array->cap = BUF_SIZE;
    array->len = 0;
    array->idx = (jsize_t)idx;
    return idx;
}

//------------------------------------------------------------------------------
static void json_nums_reserve( json_t* jsn, size_t len )
{
    assert(jsn);
    if (jsn->nums_len+len >= jsn->nums_cap)
    {
        jsn->nums_cap = grow(jsn->nums_len+len, jsn->nums_cap);
        jsn->nums = (jnum_t*)jrealloc(jsn->nums, jsn->nums_cap * sizeof(jnum_t));
    }
}

//------------------------------------------------------------------------------
static size_t json_add_num( json_t* jsn, jnum_t n )
{
    assert(jsn);
    json_nums_reserve(jsn, 1);
    size_t idx = jsn->nums_len++;
    jsn->nums[idx] = n;
    return idx;
}

//------------------------------------------------------------------------------
static size_t json_add_strl( json_t* jsn, const char* str, size_t slen )
{
    assert(jsn);
    assert(str);
    size_t idx = jmap_add_str(&jsn->strmap, str, slen);
    assert (idx != SIZE_T_MAX);
    return idx;
}

//------------------------------------------------------------------------------
size_t json_add_str( json_t* jsn, const char* str )
{
    return json_add_strl(jsn, str, strlen(str));
}

#pragma mark - jobj_t

//------------------------------------------------------------------------------
JINLINE _jobj_t* jobj_get_obj(jobj_t obj)
{
    assert(obj.json);
    assert(obj.idx < obj.json->objs->len);
    return json_get_obj(obj.json, obj.idx);
}

//------------------------------------------------------------------------------
static json_t* jobj_get_json( _jobj_t* obj )
{
    assert(obj);
    const void* ptr = (obj - obj->idx);
    ssize_t off1 = offsetof(jlist_t, data);
    ssize_t off2 = offsetof(jlist_t, json);
    jlist_t* b = (jlist_t*)( (char*)ptr + (off2 - off1));
    assert(b->json);
    return b->json;
}

//------------------------------------------------------------------------------
void jobj_truncate( _jobj_t* obj )
{
    assert(obj);
    if (obj->len == obj->cap)
        return;

    if (obj->cap <= BUF_SIZE)
        return;

    if (obj->len <= BUF_SIZE && obj->cap > BUF_SIZE)
    {
        jkv_t* kvs = obj->kvs;
        memcpy(obj->buf, kvs, sizeof(jkv_t)*obj->len);
        jfree(kvs);
        obj->cap = obj->len;
        return;
    }

    obj->kvs = (jkv_t*)jrealloc(obj->kvs, obj->len * sizeof(jkv_t));
    obj->cap = obj->len;
}

//------------------------------------------------------------------------------
void jobj_reserve( _jobj_t* obj, size_t cap )
{
    assert(obj);
    if ( obj->len+cap < obj->cap)
        return;

    size_t prev_cap = obj->cap;
    obj->cap = (jsize_t)grow(obj->len+cap, obj->cap);

    if (prev_cap <= BUF_SIZE)
    {
        jkv_t* kvs = (jkv_t*)jmalloc( obj->cap * sizeof(jkv_t) );
        memcpy(kvs, obj->buf, sizeof(jkv_t) * obj->len);
        obj->kvs = kvs;
    }
    else
    {
        assert(obj->kvs);
        obj->kvs = (jkv_t*)jrealloc(obj->kvs, sizeof(jkv_t)*obj->cap);
    }
}

//------------------------------------------------------------------------------
jkv_t* jobj_get_kv(_jobj_t* obj, size_t idx)
{
    assert(obj);
    assert(idx < obj->len);
    return (obj->cap > BUF_SIZE) ? &obj->kvs[idx] : &obj->buf[idx];
}

//------------------------------------------------------------------------------
jval_t* jobj_get_val(_jobj_t* obj, size_t idx)
{
    return &jobj_get_kv(obj, idx)->val;
}

//------------------------------------------------------------------------------
void jobj_add_kval(_jobj_t* obj, const char* key, jval_t val )
{
    assert(obj);
    assert(key);

    jobj_reserve(obj, 1);
    jkv_t* kv = jobj_get_kv(obj, obj->len++);

    size_t klen = strlen(key);
#if PACK_KEYS
    if (klen <= 3)
    {
        kv->_key = 0;
        memcpy(kv->kstr, key, klen);
        kv->kstr[klen] = '\0';
        val.type |= ~JTYPE_MASK;
    }
    else
#endif
    {
        size_t kidx = json_add_strl(jobj_get_json(obj), key, klen);
        assert (kidx < MAX_KEY_IDX);
        kv->_key = (uint32_t)kidx;
    }
    kv->val = val;
}

//------------------------------------------------------------------------------
void jobj_add_kv(_jobj_t* obj, const char* key, uint32_t type, size_t idx)
{
    assert(idx < MAX_VAL_IDX /* 2^28 */);
    assert((type & ~JTYPE_MASK) == 0);
    jobj_add_kval(obj, key, (jval_t){type, (uint32_t)idx});
}

//------------------------------------------------------------------------------
void jobj_add_num( jobj_t obj, const char* key, jnum_t num )
{
    assert(key);
    size_t idx = json_add_num(obj.json, num);
    jobj_add_kv(jobj_get_obj(obj), key, JTYPE_NUM, idx);
}

//------------------------------------------------------------------------------
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen )
{
    assert(key);
    assert(str);
    size_t idx = json_add_strl(obj.json, str, slen);
    jobj_add_kv(jobj_get_obj(obj), key, JTYPE_STR, idx);
}

//------------------------------------------------------------------------------
void jobj_add_str( jobj_t obj, const char* key, const char* str )
{
    assert(key);
    assert(str);
    jobj_add_strl(obj, key, str, strlen(str));
}

//------------------------------------------------------------------------------
void jobj_add_bool( jobj_t obj, const char* key, jbool b )
{
    assert(key);
    uint32_t type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    jobj_add_kv(jobj_get_obj(obj), key, type, 0);
}

//------------------------------------------------------------------------------
void jobj_add_nil( jobj_t obj, const char* key )
{
    assert(key);
    jobj_add_kv(jobj_get_obj(obj), key, JTYPE_NIL, 0);
}

//------------------------------------------------------------------------------
jarray_t jobj_add_array( jobj_t obj, const char* key )
{
    assert(key);
    size_t idx = json_add_array(obj.json);
    jobj_add_kv(jobj_get_obj(obj), key, JTYPE_ARRAY, idx);
    return (jarray_t){ obj.json, idx };
}

//------------------------------------------------------------------------------
jobj_t jobj_add_obj( jobj_t obj, const char* key )
{
    assert(key);
    size_t idx = json_add_obj(obj.json);
    jobj_add_kv(jobj_get_obj(obj), key, JTYPE_OBJ, idx);
    return (jobj_t){ obj.json, idx };
}

//------------------------------------------------------------------------------
JINLINE void* jobj_get_next(_jobj_t* obj, const char** key, size_t idx, int* type)
{
    assert(obj);
    assert(key);
    assert(type);

    json_t* jsn = jobj_get_json(obj);
    assert(jsn);

    jkv_t* kv = jobj_get_kv(obj, idx);
#if PACK_KEYS
    if ( (kv->val.type & ~JTYPE_MASK) > 0)
    {
        *key = kv->kstr;
    }
    else
#endif
    {
        jstr_t* jkey = jmap_get_str(&jsn->strmap, kv->_key);
        *key = (jkey->len > BUF_SIZE) ? jkey->chars : jkey->buf;
    }

    jval_t* val = &kv->val;
    *type = val->type & JTYPE_MASK;

    switch (*type)
    {
        case JTYPE_STR:
        {
            jstr_t* str = jmap_get_str(&jsn->strmap, val->idx);
            return (str->len > BUF_SIZE) ? str->chars : str->buf;
        }

        case JTYPE_NUM:
            return &jsn->nums[val->idx];

        case JTYPE_ARRAY:
            return json_get_array(jsn, val->idx);

        case JTYPE_OBJ:
            return json_get_obj(jsn, val->idx);

        case JTYPE_TRUE:
        case JTYPE_FALSE:
        case JTYPE_NIL:
        default:
            return NULL;
    }
}

//------------------------------------------------------------------------------
JINLINE void _jobj_print(_jobj_t* root, size_t depth, FILE* f)
{
    assert(root);
    assert(f);

    json_fprintf(f, "{");
    for ( size_t i = 0; i < root->len; ++i )
    {
        const char* key = NULL;
        int type = 0;
        void* val = jobj_get_next(root, &key, i, &type);

        json_fprintf(f, "\n");
        print_tabs(depth+1, f);
        json_fprintf(f, "\"%s\": ", key);
        switch (type)
        {
            case JTYPE_NIL:
                json_fprintf(f, "null");
                break;

            case JTYPE_STR:
                json_fprintf(f, "\"%s\"", (const char*)val);
                break;

            case JTYPE_NUM:
                json_fprintf(f, "%f", *(jnum_t*)val);
                break;

            case JTYPE_ARRAY:
                _jarray_print((_jarray_t*)val, depth+1, f);
                break;

            case JTYPE_OBJ:
                _jobj_print((_jobj_t*)val, depth+1, f);
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

//------------------------------------------------------------------------------
JINLINE void jobj_print(jobj_t _root, size_t depth, FILE* f)
{
    _jobj_print(jobj_get_obj(_root), depth, f);
}

#pragma mark - jarray_t

//------------------------------------------------------------------------------
JINLINE _jarray_t* jarray_get_array(jarray_t array)
{
    assert(array.json);
    return json_get_array(array.json, array.idx);
}

//------------------------------------------------------------------------------
static json_t* jarray_get_json( _jarray_t* array )
{
    assert(array);
    const void* ptr = (array - array->idx);
    ssize_t off1 = offsetof(jlist_t, data);
    ssize_t off2 = offsetof(jlist_t, json);
    jlist_t* b = (jlist_t*)( (char*)ptr + (off2 - off1));
    return b->json;
}

//------------------------------------------------------------------------------
void jarray_truncate( _jarray_t* array )
{
    assert(array);

    if (array->len == array->cap)
        return;

    if (array->cap <= BUF_SIZE)
        return;

    if (array->len <= BUF_SIZE && array->cap > BUF_SIZE)
    {
        jval_t* vals = array->vals;
        memcpy(array->buf, vals, sizeof(jval_t)*array->len);
        jfree(vals);
        array->cap = array->len;
        return;
    }

    array->vals = (jval_t*)jrealloc(array->vals, array->len * sizeof(jval_t));
    array->cap = array->len;
}

//------------------------------------------------------------------------------
void jarray_reserve( _jarray_t* a, size_t cap )
{
    assert(a);

    if ( a->len+cap < a->cap )
        return;

    size_t prev_cap = a->cap;

    a->cap = (jsize_t)grow(a->len+cap, a->cap);
    if (prev_cap <= BUF_SIZE)
    {
        jval_t* vals = (jval_t*)jmalloc( a->cap * sizeof(jval_t) );
        memcpy(vals, a->buf, a->len * sizeof(jval_t));
        a->vals = vals;
    }
    else
    {
        assert(a->vals);
        a->vals = (jval_t*)jrealloc( a->vals, a->cap * sizeof(jval_t) );
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
    *val = (jval_t)
    {
        .type = 0,
        .idx = 0
    };
    return val;
}

//------------------------------------------------------------------------------
void jarray_add_num( jarray_t _a, jnum_t num )
{
    size_t idx = json_add_num(_a.json, num);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_NUM;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_strl( jarray_t _a, const char* str, size_t slen )
{
    assert(str);
    size_t idx = json_add_strl(_a.json, str, slen);

    _jarray_t* a = jarray_get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_STR;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_str( jarray_t _a, const char* str )
{
    jarray_add_strl(_a, str, strlen(str));
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

    assert (idx < MAX_VAL_IDX);
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

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;

    return (jobj_t){ _a.json, idx };
}

//------------------------------------------------------------------------------
JINLINE void _jarray_print(_jarray_t* root, size_t depth, FILE* f)
{
    assert(root);
    assert(f);

    json_fprintf(f, "[");
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
            {
                jstr_t* str = jmap_get_str(&jsn->strmap, val->idx);
                assert(str);
                json_fprintf(f, "\"%s\"", (str->len > BUF_SIZE) ? str->chars : str->buf);
                break;
            }

            case JTYPE_NUM:
            {
                jnum_t num = jsn->nums[val->idx];
                jnum_t fract = num - floor(num);
                json_fprintf(f, (fract > 0) ? "%f" : "%0.0f", num);
                break;
            }

            case JTYPE_ARRAY:
                _jarray_print(json_get_array(jsn, val->idx), depth+1, f);
                break;

            case JTYPE_OBJ:
                _jobj_print(json_get_obj(jsn, val->idx), depth+1, f);
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
    jfree(buf->ptr);
    jbuf_init(buf);
}

//------------------------------------------------------------------------------
JINLINE void jbuf_clear( jbuf_t* buf )
{
    assert(buf);
    if (buf->ptr && buf->cap > 0) buf->ptr[0] = '\0';
    buf->len = 0;
}

//------------------------------------------------------------------------------
JINLINE void jbuf_reserve( jbuf_t* buf, size_t cap )
{
    assert(buf);
    if (buf->len+cap < buf->cap)
        return;

    buf->cap = grow(buf->len+cap, buf->cap);
    buf->ptr = (char*)jrealloc(buf->ptr, buf->cap * sizeof(char));
    assert (buf->ptr);
}

//------------------------------------------------------------------------------
JINLINE void jbuf_add( jbuf_t* buf, char ch )
{
    assert(buf);
    jbuf_reserve(buf, 1);
    buf->ptr[buf->len++] = ch;
}

#pragma mark - json_t

//------------------------------------------------------------------------------
json_t* json_new()
{
    json_t* jsn = (json_t*)jcalloc(1, sizeof(json_t));

    jmap_init(&jsn->strmap);

    // nums
    jsn->nums_len = 0;
    jsn->nums_cap = 0;
    jsn->nums = NULL;

    // arrays
    jsn->arrays = NULL;
    jsn->objs = NULL;

    jbuf_init(&jsn->keybuf);
    jbuf_init(&jsn->valbuf);

    size_t idx = json_add_obj(jsn);
    assert(idx == 0);
    return jsn;
}

////------------------------------------------------------------------------------
//void json_parse_path( json_t* jsn, const char* path )
//{
//    assert(jsn);
//    assert(path);
//
//    int fd = open(path, O_RDONLY);
//    if (!fd) return;
//
//    struct stat st;
//    if (fstat(fd, &st) != 0)
//    {
//        close(fd);
//        return;
//    }
//
//    printf("[FILE_SIZE]: %0.1f MB\n", btomb(st.st_size) );
//
//    void* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_SHARED|MAP_NORESERVE, fd, 0);
//    assert(ptr);
//
//    posix_madvise(ptr, st.st_size, POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);
//    json_parse_buf(jsn, ptr, st.st_size);
//    munmap(ptr, st.st_size);
//}

//------------------------------------------------------------------------------
void json_print(json_t* jsn, FILE* f)
{
    assert(jsn);
    assert(f);
    jobj_print(json_root(jsn), 0, f);
    json_fprintf(f, "\n");
}

//------------------------------------------------------------------------------
void json_destroy(json_t* jsn)
{
    assert(jsn);

    // cleanup string map
    jmap_destroy(&jsn->strmap);

    // cleanup numbers
    jfree(jsn->nums); jsn->nums = NULL;

    if (jsn->objs)
    {
        // cleanup objects
        for ( size_t i = 0; i < jsn->objs->len; i++ )
        {
            _jobj_t* obj = json_get_obj(jsn, i);
            if (obj->cap > BUF_SIZE)
            {
                jfree(obj->kvs); obj->kvs = NULL;
            }
        }
        jfree(jsn->objs); jsn->objs = NULL;
    }

    if (jsn->arrays)
    {
        // cleanup arrays
        for ( size_t i = 0; i < jsn->arrays->len; i++ )
        {
            _jarray_t* array = json_get_array(jsn, i);
            if (array->cap > BUF_SIZE)
            {
                jfree(array->vals); array->vals = NULL;
            }
        }
        jfree(jsn->arrays); jsn->arrays = NULL;
    }

    // cleanup the buffers
    jbuf_destroy(&jsn->keybuf);
    jbuf_destroy(&jsn->valbuf);
}

//------------------------------------------------------------------------------
void json_free(json_t* jsn)
{
    json_destroy(jsn);
    jfree(jsn);
}

//------------------------------------------------------------------------------
jobj_t json_root(json_t* jsn)
{
    assert(jsn);
    return (jobj_t){jsn, 0};
}

#pragma mark - parse

//--------------------------------------------------------------------------
struct jread_mem_t
{
    char* beg;
    char* end;
};

//--------------------------------------------------------------------------
JINLINE int jpeek_MEM( void* ctx )
{
    struct jread_mem_t* file = (struct jread_mem_t*)ctx;
    if (file->beg == file->end) return EOF;
    return *file->beg;
}

//--------------------------------------------------------------------------
JINLINE int jnext_MEM( void* ctx )
{
    struct jread_mem_t* file = (struct jread_mem_t*)ctx;
    return *++file->beg;
}

//--------------------------------------------------------------------------
#define IO_BUF_SIZE 4096
struct jread_file_t
{
    FILE* file;
    char buf[IO_BUF_SIZE];
    size_t pos;
    size_t len;
};

//--------------------------------------------------------------------------
JINLINE int jpeek_FILE( void* ctx )
{
    struct jread_file_t* f = (struct jread_file_t*)ctx;
    if (f->len == SIZE_T_MAX)
    {
        return EOF;
    }

    if (f->pos >= f->len)
    {
        f->len = fread(f->buf, 1, IO_BUF_SIZE, f->file);
        f->pos = 0;
    }
    return f->buf[f->pos];
}

//--------------------------------------------------------------------------
JINLINE int jnext_FILE( void* ctx )
{
    struct jread_file_t* f = (struct jread_file_t*)ctx;
    ++f->pos;
    return jpeek_FILE(ctx);
}

#define jpeek jpeek_MEM
#define jnext jnext_MEM

//------------------------------------------------------------------------------
JINLINE void parse_whitespace( void* ctx )
{
    for ( int ch = jpeek(ctx); ch >= 0; ch = jnext(ctx) )
    {
        switch(ch)
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
JINLINE unsigned char char_to_hex(int ch)
{
    switch (ch)
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
        default: json_assert(jfalse, "invalid unicode hex digit: '%c'", ch);
    }
    return 0;
}

//------------------------------------------------------------------------------
JINLINE void utf8_encode(int32_t codepoint, jbuf_t* str )
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
        json_assert(jfalse, "invalid unicode");
    }
}

//------------------------------------------------------------------------------
JINLINE void parse_literal(json_t* jsn, void* f, const char* str)
{
    int ch = ch = jnext(f);
    for ( const char* s = ++str; *s; s++, ch = jnext(f))
    {
        json_assert(ch == *s, "expected string literal: '%s'", str);
    }
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_sign( void* ctx )
{
    int ch = jpeek(ctx);
    switch (ch)
    {
        case '-':
            jnext(ctx);
            return -1;

        case '+':
            jnext(ctx);
            return 1;

        default:
            return 1;
    }
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_digitsp( void* ctx, size_t* places)
{
    jnum_t num = 0;
    for ( int ch = jpeek(ctx), cnt = 0; ch >= 0; ch = jnext(ctx), ++cnt )
    {
        switch (ch)
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
                num = num*10 + (ch - '0');
                break;
            }

            default:
            {
                *places = pow(10, cnt);
                return num;
            }
        }
    }

    json_assert(jfalse, "unexpected end of file");
    return num;
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_digits( void* ctx )
{
    size_t places = 0;
    return parse_digitsp(ctx, &places);
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_num( void* ctx )
{
    // +/-
    jnum_t sign = parse_sign(ctx);

    // whole number
    jnum_t num = parse_digits(ctx);

    // fraction
    switch (jpeek(ctx))
    {
        case '.':
        {
            jnext(ctx);
            size_t places = 1;
            jnum_t fract = parse_digitsp(ctx, &places);
            num += fract / places;
            break;
        }

        default:
            break;
    }

    // scientific notation
    switch (jpeek(ctx))
    {
        case 'e':
        case 'E':
        {
            jnext(ctx);
            jnum_t esign = parse_sign(ctx);
            jnum_t digits = parse_digits(ctx);
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
JINLINE unsigned int parse_unicode_hex(void* f)
{
    return char_to_hex(jpeek(f)) << 12 |
           char_to_hex(jnext(f)) << 8 |
           char_to_hex(jnext(f)) << 4 |
           char_to_hex(jnext(f));
}

//------------------------------------------------------------------------------
JINLINE void parse_unicode2( jbuf_t* str, void* f )
{
    // U+XXXX
    unsigned int val = parse_unicode_hex(f);
//    json_error(val > 0, is, "\\u0000 is not allowed");

    // surrogate pair, \uXXXX\uXXXXX
    if (0xD800 <= val && val <= 0xDBFF)
    {
        json_assert(jnext(f) == '\\', "invalid unicode");
        json_assert(jnext(f) == 'u', "invalid unicode");

        // read the surrogate pair from the stream
        unsigned int val2 = parse_unicode_hex(f);

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
void parse_str(jbuf_t* str, void* ctx)
{
    int prev = jpeek(ctx);
    json_assert(prev == '"', "valid strings must start with a '\"' character");

    jbuf_clear(str);

    for (int ch = jnext(ctx); ch >= 0; ch = jnext(ctx) )
    {
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
                        parse_unicode2(str, ctx);
                        break;
                    case '"':
                        jbuf_add(str, '\"');
                        break;
                    case '\\':
                        jbuf_add(str, '\\');
                        ch = 0; // reset the character
                        break;

                    default:
                        json_assert(jfalse, "invalid escape sequence '\\%c'", ch);
                        break;
                }
                break;
            }

            default:
            {
                switch (ch)
                {
                    case '\\':
                        break;

                    case '"':
                        jnext(ctx);
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

    json_assert(jfalse, "string terminated unexpectedly");
}

jval_t parse_val( json_t* jsn, void* f );

//------------------------------------------------------------------------------
void parse_array(jarray_t array, void* ctx)
{
    json_assert(jpeek(ctx) == '[', ""); jnext(ctx);
    json_t* jsn = array.json;

    while ( 1 )
    {
        parse_whitespace(ctx);

        jval_t val = parse_val(jsn, ctx);
        *jarray_add_val(jarray_get_array(array)) = val;

        parse_whitespace(ctx);

        int ch = jpeek(ctx);
        switch(ch)
        {
            case ',':
                jnext(ctx);
                break;

            case ']':
                jnext(ctx);
                return;

            default:
                json_assert(jfalse, "expected ',' or ']' when parsing array: '%c'", ch);
        }
    }
}

//------------------------------------------------------------------------------
void parse_obj(jobj_t obj, void* ctx)
{
    json_assert(jpeek(ctx) == '{', ""); jnext(ctx);
    json_t* jsn = obj.json;

    while (1)
    {
        parse_whitespace(ctx);

        // get the key
        parse_str(&jsn->keybuf, ctx);
        const char* key = jsn->keybuf.ptr;

        parse_whitespace(ctx);
        json_assert(jpeek(ctx) == ':', "expected ':' after key: '%s', found '%c'", key, jpeek(ctx)); jnext(ctx);
        parse_whitespace(ctx);

        jval_t val = parse_val(jsn, ctx);
        jobj_add_kval(jobj_get_obj(obj), key, val);

        parse_whitespace(ctx);

        int ch = jpeek(ctx);
        switch (ch)
        {
            case ',':
                jnext(ctx);
                break;

            case '}':
                jnext(ctx);
                return;

            default:
                json_assert(jfalse, "expected ',' or '}' when parsing object: '%c'", ch);
        }
    }
}

//------------------------------------------------------------------------------
jval_t parse_val( json_t* jsn, void* f )
{
    int ch = jpeek(f);
    switch(ch)
    {
        case '{': // obj
        {
            size_t idx = json_add_obj(jsn);
            parse_obj((jobj_t){jsn, idx}, f);
            return (jval_t){JTYPE_OBJ, (uint32_t)idx};
        }

        case '[': // array
        {
            size_t idx = json_add_array(jsn);
            parse_array((jarray_t){jsn, idx}, f);
            return (jval_t){JTYPE_ARRAY, (uint32_t)idx};
        }

        case '"': // string
        {
            jbuf_t* buf = &jsn->valbuf;
            parse_str(buf, f);
            return (jval_t){JTYPE_STR, (uint32_t)json_add_strl(jsn, buf->ptr, buf->len)};
        }

        case 't': // true
            parse_literal(jsn, f, "true");
            return (jval_t){JTYPE_TRUE, 0};

        case 'f': // false
            parse_literal(jsn, f, "false");
            return (jval_t){JTYPE_FALSE, 0};

        case 'n': // null
            parse_literal(jsn, f, "null");
            return (jval_t){JTYPE_NIL, 0};

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
        {
            jnum_t num = parse_num(f);
            return (jval_t){JTYPE_NUM, (uint32_t)json_add_num(jsn, num)};
        }

        default:
            json_assert(jfalse, "");
    }

    return (jval_t){JTYPE_NIL, 0};
}


#pragma mark - io

#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <mach/mach.h>

//------------------------------------------------------------------------------
void print_mem_usage()
{
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
    printf("[RAM]: Actual: %0.1f MB Virtual: %0.1f MB\n", btomb(t_info.resident_size), btomb(t_info.virtual_size));
}

//------------------------------------------------------------------------------
void json_parse_file( json_t* jsn, void* ctx )
{
    print_mem_usage();

    assert(jsn);
    assert(ctx);

    int ch = jpeek(ctx);
    switch (ch)
    {
        case '{':
            parse_obj(json_root(jsn), ctx);
            break;

        default:
            break;
    }

    print_mem_usage();
}

//------------------------------------------------------------------------------
json_t* json_load_file( const char* path )
{
#if READ_METHOD == STD_READ
    printf("Parsing json using [fread] method\n");

    FILE* file = fopen(path, "r");
    assert(file);

    struct jread_file_t jf;
    jf.file = file;
    jf.len = 0;
    jf.pos = 0;

    json_t* jsn = json_new();
    print_mem_usage();
    json_parse_file(jsn, &jf);
    print_mem_usage();

#elif READ_METHOD == ONESHOT_READ
    printf("Parsing json using [mmap] method\n");

    int fd = open(path, O_RDONLY);
    assert(fd);

    struct stat st;
    int rt = fstat(fd, &st);
    assert( rt == 0 );
    size_t len = st.st_size;

    struct jread_mem_t mem;
    mem.beg = (char*)mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    mem.end = mem.beg + len;

    json_t* jsn = json_new();

    jmap_rehash(&jsn->strmap, ceilf(len*0.01));
    json_nums_reserve(jsn, ceilf(len*0.01));
    json_arrays_reserve(jsn, ceilf(len*0.01));
    json_objs_reserve(jsn, ceilf(len*0.01));

    print_mem_usage();
    json_parse_file(jsn, &mem);
    print_mem_usage();

    jbuf_destroy(&jsn->keybuf);
    jbuf_destroy(&jsn->valbuf);

    munmap(mem.beg, len);

#else
    #error must specify the file read method
#endif


#if PRINT_MEMORY
    print_memory_stats(jsn);
#endif
    return jsn;
}

//------------------------------------------------------------------------------
void print_memory_stats(json_t* jsn)
{
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
}
