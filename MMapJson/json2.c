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
#include <setjmp.h>

#if ( defined(__APPLE__) && defined(__MACH__) )
    #include <mach/vm_statistics.h>
    #include <mach/mach_types.h>
    #include <mach/mach_init.h>
    #include <mach/mach_host.h>
    #include <mach/mach.h>
#endif

#if defined(__unix__) || ( defined(__APPLE__) && defined(__MACH__) )
    #define JPOSIX (1)
#else
    #define JPOSIX (0)
#endif

#if JPOSIX
    #include <sys/mman.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/resource.h>
#endif

#pragma mark - macros

#ifndef MAX
    #define MAX(A,B) ({__typeof__(A) a = (A); __typeof__(B) b = (B); a>b?a:b; })
#endif

#ifndef MIN
    #define MIN(A,B) ({__typeof__(A) a = (A); __typeof__(B) b = (B); a<b?a:b; })
#endif

#define jsnprintf(BUF, BLEN, FMT, ...) { snprintf(BUF, BLEN, FMT, ## __VA_ARGS__); BUF[BLEN-1] = '\0'; }

__thread char jerr_buf[256] = "";

#ifndef json_assert
    #define json_assert(A, STR, ...) { if (!(A)) {jsnprintf(jerr_buf, sizeof(jerr_buf), "%s:%zu:%zu: " STR, ctx->src, ctx->line+1, ctx->col, ## __VA_ARGS__ ); longjmp(ctx->jerr_jmp, EXIT_FAILURE); } }
#endif

#define PRINT 1
#if PRINT
    #define json_fprintf(F, FMT, ...) fprintf(F, FMT, ## __VA_ARGS__ )
#else
    #define json_fprintf(F, FMT, ...)
#endif

#pragma mark - constants

#if __STRICT_ANSI__
    #define JINLINE
#else
    #define JINLINE static inline
#endif

#define BUF_SIZE ((size_t)6)
#define MAX_VAL_IDX 268435456 // 2^28
#define MAX_KEY_IDX UINT32_MAX

#define JMAP_MAX_LOADFACTOR 0.8f
#define JMAP_IDEAL_LOADFACTOR 0.3f

// pack short keys directly into the jkv_t struct if possible
#define PACK_KEYS 1

#define JTRUE 1
#define JFALSE 0

#define IO_BUF_SIZE 4096

#pragma mark - structs

//------------------------------------------------------------------------------
typedef uint32_t jhash_t;

//------------------------------------------------------------------------------
struct jbuf_t
{
    size_t cap;
    size_t len;
    char* ptr;
};
typedef struct jbuf_t jbuf_t;

//------------------------------------------------------------------------------
struct jcontext_t
{
    const char* beg;
    const char* end;
    char buf[IO_BUF_SIZE];
    FILE* file;

    size_t col;
    size_t line;

    char src[128];

    jmp_buf jerr_jmp;

    jbuf_t strbuf; // buffer for temporarily storing the key string
};
typedef struct jcontext_t jcontext_t;

//------------------------------------------------------------------------------
struct jmapbucket_t
{
    size_t len;
    size_t cap;
    size_t* slots;
};
typedef struct jmapbucket_t jmapbucket_t;

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
void print_memory_stats(json_t*);
JINLINE jval_t parse_val( json_t* jsn, jcontext_t* ctx );

#pragma mark - memory

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
    if (!rt) free(ptr);
    assert(rt);
    return rt;
}

#pragma mark - util

//------------------------------------------------------------------------------
void json_err_handler( const char* err )
{
    puts(err);
    abort();
}

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
JINLINE void jstr_init_str_hash( jstr_t* jstr, const char* cstr, size_t len, jhash_t hash )
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

#pragma mark - jmap_t

//------------------------------------------------------------------------------
JINLINE void jmap_init(jmap_t* map)
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
JINLINE void jmap_destroy(jmap_t* map)
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
JINLINE void jmap_reserve_str( jmap_t* map, size_t len )
{
    assert(map);

    // not found, create a new one
    if (map->slen+len <= map->scap)
        return;

    map->scap = grow(map->slen+len, map->scap);
    map->strs = (jstr_t*)jrealloc(map->strs, sizeof(jstr_t)*map->scap);
}

//------------------------------------------------------------------------------
JINLINE size_t _jmap_add_str(jmap_t* map, const char* cstr, size_t len, uint32_t hash)
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
JINLINE void jmap_bucket_reserve( jmapbucket_t* bucket, size_t len )
{
    assert(bucket);

    if ( bucket->len+len <= bucket->cap )
        return;

    // grow bucket now
    bucket->cap = grow(bucket->len+len, bucket->cap);
    bucket->slots = (size_t*)jrealloc( bucket->slots, sizeof(size_t) * bucket->cap );
}

//------------------------------------------------------------------------------
JINLINE void _jmap_add_key(jmap_t* map, uint32_t hash, size_t val)
{
    assert(map);
    assert(map->buckets);
    assert(map->bcap > 0);

    jmapbucket_t* bucket = &map->buckets[hash % map->bcap];
    jmap_bucket_reserve(bucket, 1);
    assert(bucket->slots);

    if (bucket->len == 0) map->blen++; // empty bucket, we are about to add it, increment
    bucket->slots[bucket->len++] = val;
}

////------------------------------------------------------------------------------
//void jmap_debug( jmap_t* map )
//{
//    puts("--- maps ---");
//
//    for ( size_t i = 0; i < map->bcap; i++ )
//    {
//        jmapbucket_t* bucket = &map->buckets[i];
//        for ( size_t n = 0; n < bucket->len; n++ )
//        {
//            size_t idx = bucket->slots[n];
//
//            jstr_t* str = &map->strs[idx];
//            const char* chars = (str->len > BUF_SIZE) ? str->chars : str->buf;
//            puts(chars);
//        }
//    }
//
//    puts("--- strings ---");
//    for ( size_t n = 0; n < map->slen; n++ )
//    {
//        jstr_t* str = &map->strs[n];
//        const char* chars = (str->len > BUF_SIZE) ? str->chars : str->buf;
//        puts(chars);
//    }
//}

//------------------------------------------------------------------------------
JINLINE void jmap_rehash(jmap_t* map, size_t hint)
{
    assert(map);

//    puts("----------------------------------------");
//    jmap_debug(map);
//    puts("----------------------------------------");

    // if there is an empty hashmap, no need to check the load factor!
    if (map->bcap > 0)
    {
        float load = map->blen / (float)map->bcap;
        if (load <= JMAP_MAX_LOADFACTOR)
            return;
    }

    // TODO: reuse allocations

    size_t target = ceilf(map->bcap / JMAP_IDEAL_LOADFACTOR);

    size_t max = map->bcap;
    jmapbucket_t* buckets = map->buckets;

    map->bcap = MAX( MAX(hint, 13), target);
    map->buckets = (jmapbucket_t*)jcalloc(map->bcap, sizeof(jmapbucket_t));
    map->blen = 0;

    for ( size_t i = 0; i < max; i++ )
    {
        jmapbucket_t* bucket = &buckets[i];
        for ( size_t n = 0; n < bucket->len; n++ )
        {
            size_t idx = bucket->slots[n];
            _jmap_add_key(map, map->strs[idx].hash, idx);
        }

        bucket->len = 0;
        bucket->cap = 0;
        jfree(bucket->slots); bucket->slots = NULL;
    }
    jfree(buckets); buckets = NULL;
}

//------------------------------------------------------------------------------
JINLINE jstr_t* jmap_get_str(jmap_t* map, size_t idx)
{
    assert(map);
    assert(idx < map->slen);
    return &map->strs[idx];
}

//------------------------------------------------------------------------------
JINLINE size_t jmap_find_hash(jmap_t* map, jhash_t hash, const char* cstr, size_t slen)
{
    assert(cstr);
    if (map->blen == 0) return SIZE_T_MAX;

    assert(map->bcap > 0);
    jmapbucket_t* bucket = &map->buckets[hash % map->bcap];

    for ( size_t i = 0; i < bucket->len; ++i )
    {
        // find our string
        size_t idx = bucket->slots[i];
        jstr_t* str = &map->strs[idx];

        if (str->hash == hash && str->len == slen)
        {
            const char* chars = (str->len > BUF_SIZE) ? str->chars : str->buf;
            assert(chars);

            if (strncmp(chars, cstr, slen) == 0)
            {
                return idx;
            }
        }
    }

    return SIZE_T_MAX;
}

//------------------------------------------------------------------------------
#define jmap_find_str(MAP, CSTR, SLEN) jmap_find_hash(MAP, jstr_hash(CSTR, SLEN), CSTR, SLEN)

//------------------------------------------------------------------------------
JINLINE size_t jmap_add_str(jmap_t* map, const char* cstr, size_t slen)
{
    assert(map);
    assert(cstr);

    jhash_t hash = jstr_hash(cstr, slen);

    size_t idx = jmap_find_hash(map, hash, cstr, slen);
    if (idx != SIZE_T_MAX)
        return idx;

    jmap_rehash(map, 0);

    // did not find an existing entry, create a new one
    idx = _jmap_add_str(map, cstr, slen, hash);
    _jmap_add_key(map, hash, idx);
    return idx;
}

#pragma mark - jval_t

//------------------------------------------------------------------------------
void jval_print( struct json_t* jsn, jval_t val, size_t depth, FILE* f )
{
    switch (jval_type(val))
    {
        case JTYPE_NIL:
            json_fprintf(f, "null");
            break;

        case JTYPE_STR:
            json_fprintf(f, "\"%s\"", json_get_str(jsn, val));
            break;

        case JTYPE_NUM:
            json_fprintf(f, "%f", json_get_num(jsn, val));
            break;

        case JTYPE_ARRAY:
            jarray_print(json_get_array(jsn, val), depth, f);
            break;

        case JTYPE_OBJ:
            jobj_print(json_get_obj(jsn, val), depth, f);
            break;

        case JTYPE_TRUE:
            json_fprintf(f, "true");
            break;

        case JTYPE_FALSE:
            json_fprintf(f, "false");
            break;

        default:
            assert(JFALSE); // unknown type!!!
            break;
    }
}


#pragma mark - json_t

//------------------------------------------------------------------------------
JINLINE void json_objs_reserve( json_t* jsn, size_t res )
{
    assert(jsn);
    if (jsn->objs.len+res <= jsn->objs.cap)
        return;

    jsn->objs.cap = grow(jsn->objs.len+res, jsn->objs.cap);
    jsn->objs.ptr = (_jobj_t*)jrealloc(jsn->objs.ptr, jsn->objs.cap * sizeof(_jobj_t) );
}

//------------------------------------------------------------------------------
JINLINE _jobj_t* _json_get_obj( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert( idx < jsn->objs.len );
    return &jsn->objs.ptr[idx];
}

//------------------------------------------------------------------------------
JINLINE size_t json_add_obj( json_t* jsn )
{
    assert(jsn);
    json_objs_reserve(jsn, 1);

    size_t idx = jsn->objs.len++;
    _jobj_t* obj = _json_get_obj(jsn, idx);
    assert(obj);
    obj->cap = BUF_SIZE;
    obj->len = 0;
    return idx;
}

//------------------------------------------------------------------------------
JINLINE void json_arrays_reserve( json_t* jsn, size_t res )
{
    assert(jsn);
    if (jsn->arrays.len+res <= jsn->arrays.cap)
        return;

    jsn->arrays.cap = grow(jsn->arrays.len+res, jsn->arrays.cap);
    jsn->arrays.ptr = (_jarray_t*)jrealloc(jsn->arrays.ptr, jsn->arrays.cap * sizeof(_jarray_t) );
}

//------------------------------------------------------------------------------
JINLINE _jarray_t* _json_get_array( json_t* jsn, size_t idx )
{
    assert(jsn);
    assert(idx < jsn->arrays.len);
    return ((_jarray_t*)jsn->arrays.ptr) + idx;
}

//------------------------------------------------------------------------------
JINLINE size_t json_add_array( json_t* jsn )
{
    assert(jsn);
    json_arrays_reserve(jsn, 1);

    size_t idx = jsn->arrays.len++;
    _jarray_t* array = _json_get_array(jsn, idx);
    assert(array);
    array->cap = BUF_SIZE;
    array->len = 0;
    return idx;
}

//------------------------------------------------------------------------------
JINLINE void json_nums_reserve( json_t* jsn, size_t len )
{
    assert(jsn);
    if (jsn->nums.len+len <= jsn->nums.cap)
        return;

    jsn->nums.cap = grow(jsn->nums.len+len, jsn->nums.cap);
    jsn->nums.ptr = (jnum_t*)jrealloc(jsn->nums.ptr, jsn->nums.cap * sizeof(jnum_t));
}

//------------------------------------------------------------------------------
JINLINE size_t json_add_num( json_t* jsn, jnum_t n )
{
    assert(jsn);
    json_nums_reserve(jsn, 1);
    size_t idx = jsn->nums.len++;
    jsn->nums.ptr[idx] = n;
    return idx;
}

//------------------------------------------------------------------------------
JINLINE size_t json_add_strl( json_t* jsn, const char* str, size_t slen )
{
    assert(jsn);
    assert(str);
    size_t idx = jmap_add_str(&jsn->strmap, str, slen);
    assert (idx != SIZE_T_MAX);
    return idx;
}
//------------------------------------------------------------------------------
const char* json_get_str( json_t* jsn, jval_t val )
{
    if (!jval_is_str(val)) return NULL;

    jstr_t* jstr = jmap_get_str(&jsn->strmap, val.idx);
    if (!jstr) return NULL;

    return (jstr->len > BUF_SIZE) ? jstr->chars : jstr->buf;
}

//------------------------------------------------------------------------------
jnum_t json_get_num( json_t* jsn, jval_t val )
{
    return jval_is_num(val) ? jsn->nums.ptr[val.idx] : 0;
}

//------------------------------------------------------------------------------
jbool_t json_get_bool( json_t* jsn, jval_t val )
{
    switch (jval_type(val))
    {
        case JTYPE_TRUE:
            return JTRUE;

        case JTYPE_FALSE:
        default:
            return JFALSE;
    }
}

//------------------------------------------------------------------------------
jobj_t json_get_obj( json_t* jsn, jval_t val )
{
    if (!jval_is_obj(val)) return (jobj_t){.json=NULL, .idx=0};
    assert( _json_get_obj(jsn, val.idx) );
    return (jobj_t){.json=jsn, .idx=val.idx};
}

//------------------------------------------------------------------------------
jarray_t json_get_array( json_t* jsn, jval_t val )
{
    if (!jval_is_array(val)) return (jarray_t){.json=NULL, .idx=0};
    assert( _json_get_array(jsn, val.idx) );
    return (jarray_t){.json=jsn, .idx=val.idx};
}

#pragma mark - jobj_t

//------------------------------------------------------------------------------
JINLINE _jobj_t* jobj_get_obj(jobj_t obj)
{
    json_t* jsn = jobj_get_json(obj);
    assert(jsn);
    assert(obj.idx < jsn->objs.len);
    return _json_get_obj(jsn, obj.idx);
}

//------------------------------------------------------------------------------
size_t jobj_len( jobj_t obj )
{
    _jobj_t* _obj = jobj_get_obj(obj);
    assert(_obj);
    return _obj->len;
}

//------------------------------------------------------------------------------
const char* jobj_get(jobj_t obj, size_t idx, jval_t* val)
{
    assert(val);

    _jobj_t* _obj = jobj_get_obj(obj);
    json_t* jsn = jobj_get_json(obj);

    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs : _obj->buf;

    *val = kvs[idx].val;
    if (kvs[idx].val.type & ~JTYPE_MASK)
    {
        return kvs[idx].kstr;
    }

    jstr_t* jstr = jmap_get_str(&jsn->strmap, kvs[idx]._key);
    assert(jstr);
    return (jstr->len > BUF_SIZE) ? jstr->chars : jstr->buf;
}

//------------------------------------------------------------------------------
JINLINE void jobj_truncate( jobj_t o )
{
    _jobj_t* obj = jobj_get_obj(o);
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
JINLINE void _jobj_reserve( _jobj_t* obj, size_t cap )
{
    assert(obj);
    if ( obj->len+cap <= obj->cap)
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
void jobj_reserve( jobj_t obj, size_t cap )
{
    _jobj_reserve(jobj_get_obj(obj), cap);
}

//------------------------------------------------------------------------------
JINLINE jkv_t* _jobj_get_kv(_jobj_t* obj, size_t idx)
{
    assert(obj);
    assert(idx < obj->len);
    return (obj->cap > BUF_SIZE) ? &obj->kvs[idx] : &obj->buf[idx];
}

//------------------------------------------------------------------------------
jval_t* _jobj_get_val(_jobj_t* obj, size_t idx)
{
    return &_jobj_get_kv(obj, idx)->val;
}

//------------------------------------------------------------------------------
#define jobj_add_key(OBJ, KEY) jobj_add_keyl(OBJ, KEY, strlen(KEY))
JINLINE jkv_t* jobj_add_keyl( jobj_t o, const char* key, size_t klen )
{
    json_t* jsn = jobj_get_json(o);
    _jobj_t* obj = jobj_get_obj(o);

    assert(obj);
    assert(key);

    _jobj_reserve(obj, 1);
    jkv_t* kv = _jobj_get_kv(obj, obj->len++);
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;

#if PACK_KEYS
    if (klen < sizeof(kv->kstr) )
    {
        kv->_key = 0;
        strncpy(kv->kstr, key, sizeof(kv->kstr));
        kv->val.type |= ~JTYPE_MASK;
    }
    else
#endif
    {
        size_t kidx = json_add_strl(jsn, key, klen);
        assert (kidx < MAX_KEY_IDX);
        kv->_key = (uint32_t)kidx;
    }
    return kv;
}

//------------------------------------------------------------------------------
#define jobj_add_kval(OBJ,KEY,VAL) jkv_set_val(jobj_add_key(OBJ, KEY), VAL)
JINLINE void jkv_set_val(jkv_t* kv, jval_t val )
{
    assert(kv);
    kv->val.type = (kv->val.type & ~JTYPE_MASK) | (val.type & JTYPE_MASK);
    kv->val.idx = val.idx;
}

//------------------------------------------------------------------------------
JINLINE void jobj_add_kv(jobj_t obj, const char* key, uint32_t type, size_t idx)
{
    assert(idx < MAX_VAL_IDX /* 2^28 */);
    assert((type & ~JTYPE_MASK) == 0);
    jobj_add_kval(obj, key, ((jval_t){type, (uint32_t)idx}));
}

//------------------------------------------------------------------------------
void jobj_add_num( jobj_t obj, const char* key, jnum_t num )
{
    assert(key);
    size_t idx = json_add_num(jobj_get_json(obj), num);
    jobj_add_kv(obj, key, JTYPE_NUM, idx);
}

//------------------------------------------------------------------------------
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen )
{
    assert(key);
    assert(str);
    size_t idx = json_add_strl(jobj_get_json(obj), str, slen);
    jobj_add_kv(obj, key, JTYPE_STR, idx);
}

//------------------------------------------------------------------------------
void jobj_add_bool( jobj_t obj, const char* key, jbool_t b )
{
    assert(key);
    uint32_t type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    jobj_add_kv(obj, key, type, 0);
}

//------------------------------------------------------------------------------
void jobj_add_nil( jobj_t obj, const char* key )
{
    assert(key);
    jobj_add_kv(obj, key, JTYPE_NIL, 0);
}

//------------------------------------------------------------------------------
jarray_t jobj_add_array( jobj_t obj, const char* key )
{
    assert(key);
    json_t* jsn = jobj_get_json(obj);
    size_t idx = json_add_array(jsn);
    jobj_add_kv(obj, key, JTYPE_ARRAY, idx);
    return (jarray_t){ jsn, idx };
}

//------------------------------------------------------------------------------
jobj_t jobj_add_obj( jobj_t obj, const char* key )
{
    assert(key);
    json_t* jsn = jobj_get_json(obj);
    size_t idx = json_add_obj(jsn);
    jobj_add_kv(obj, key, JTYPE_OBJ, idx);
    return (jobj_t){ jsn, idx };
}

//------------------------------------------------------------------------------
size_t jobj_find_shortstr( jobj_t obj, const char* key, size_t klen )
{
    assert (klen < 4);

    _jobj_t* _obj = jobj_get_obj(obj);
    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs : _obj->buf;
    for ( size_t i = 0; i < _obj->len; i++ )
    {
        jkv_t* kv = &kvs[i];
        if ( (kv->val.type & ~JTYPE_MASK) == 0 )
            continue;

        if (strcmp(kv->kstr, key) == 0)
        {
            // found it!
            return i;
        }
    }
    return SIZE_T_MAX;

}

//------------------------------------------------------------------------------
size_t jobj_findl( jobj_t obj, const char* key, size_t klen )
{
    // check for short strings, these do not go into the hash table and must be
    // searched manually
    if (klen < 4) return jobj_find_shortstr(obj, key, klen);

    // not a short string! Proceed with search.
    json_t* jsn = jobj_get_json(obj);
    _jobj_t* _obj = jobj_get_obj(obj);

    // check the hashtable for our string, if it's not there it's no where!
    size_t idx = jmap_find_str(&jsn->strmap, key, klen);
    if (idx == SIZE_T_MAX)
    {
        return idx;
    }

    // we now know the correct index for our key, search the object to find a
    // matching index.
    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs : _obj->buf;
    for ( size_t i = 0; i < _obj->len; i++ )
    {
        jkv_t* kv = &kvs[i];

        // can't be a short string (we already checked), if this one is skip it.
        if ( (kv->val.type & ~JTYPE_MASK) > 0 )
            continue;

        // found a match!!!
        if (kv->_key == idx)
        {
            return i;
        }
    }

    return SIZE_T_MAX;
}

//------------------------------------------------------------------------------
void jobj_print(jobj_t obj, size_t depth, FILE* f)
{
    json_fprintf(f, "{\n");

    json_t* jsn = jobj_get_json(obj);
    size_t len = jobj_len(obj);
    for ( size_t i = 0; i < len; i++ )
    {
        jval_t val;
        const char* key = jobj_get(obj, i, &val);

        print_tabs(depth+1, f);
        json_fprintf(f, "\"%s\": ", key);
        jval_print(jsn, val, depth+1, f);
        json_fprintf(f, (i+1 == len) ?  "\n" : ",\n" );
    }

    print_tabs(depth, f);
    json_fprintf(f, "}");
}

#pragma mark - jarray_t

//------------------------------------------------------------------------------
JINLINE _jarray_t* _jarray_get_array(jarray_t array)
{
    assert(array.json);
    return _json_get_array(array.json, array.idx);
}

//------------------------------------------------------------------------------
size_t jarray_len(jarray_t a)
{
    return _jarray_get_array(a)->len;
}

//------------------------------------------------------------------------------
JINLINE void jarray_truncate( jarray_t a )
{
   _jarray_t* array = _jarray_get_array(a);
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
JINLINE void _jarray_reserve( _jarray_t* a, size_t cap )
{
    assert(a);

    if ( a->len+cap <= a->cap )
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
void jarray_reserve( jarray_t a, size_t cap )
{
    _jarray_reserve(_jarray_get_array(a), cap);
}

//------------------------------------------------------------------------------
JINLINE jval_t* _jarray_get_val( _jarray_t* a, size_t idx)
{
    assert(a);
    assert(idx < a->len);
    return (a->cap > BUF_SIZE) ? &a->vals[idx] : &a->buf[idx];
}

//------------------------------------------------------------------------------
jval_t* _jarray_add_val( _jarray_t* a)
{
    assert(a);
    _jarray_reserve(a, 1);
    jval_t* val = _jarray_get_val(a, a->len++);
    *val = (jval_t)
    {
        .type = 0,
        .idx = 0
    };
    return val;
}

//------------------------------------------------------------------------------
jval_t jarray_get(jarray_t a, size_t idx)
{
    jval_t* val = _jarray_get_val(_jarray_get_array(a), idx);
    assert(val);
    return *val;
}

//------------------------------------------------------------------------------
void jarray_add_num( jarray_t _a, jnum_t num )
{
    size_t idx = json_add_num(_a.json, num);

    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_NUM;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_strl( jarray_t _a, const char* str, size_t slen )
{
    assert(str);
    size_t idx = json_add_strl(_a.json, str, slen);

    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_STR;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;
}

//------------------------------------------------------------------------------
void jarray_add_bool( jarray_t _a, jbool_t b )
{
    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    val->idx = 0;
}

//------------------------------------------------------------------------------
void jarray_add_nil( jarray_t _a )
{
    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_NIL;
    val->idx = 0;
}

//------------------------------------------------------------------------------
jarray_t jarray_add_array( jarray_t _a )
{
    size_t idx = json_add_array(_a.json);

    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_ARRAY;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;

    return (jarray_t){ _a.json, idx };
}

//------------------------------------------------------------------------------
jobj_t jarray_add_obj( jarray_t _a )
{
    size_t idx = json_add_obj(_a.json);

    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_OBJ;

    assert (idx < MAX_VAL_IDX);
    val->idx = (uint32_t)idx;

    return (jobj_t){ _a.json, idx };
}

//------------------------------------------------------------------------------
void jarray_print( jarray_t array, size_t depth, FILE* f )
{
    json_fprintf(f, "[\n");
    json_t* jsn = jarray_get_json(array);
    size_t len = jarray_len(array);
    for ( size_t i = 0; i < len; i++ )
    {
        print_tabs(depth+1, f);
        jval_print(jsn, jarray_get(array, i), depth+1, f);

        json_fprintf(f, (i+1 == len) ?  "\n" : ",\n" );
    }
    print_tabs(depth, f);
    json_fprintf(f, "]");
}

#pragma mark - jbuf_t

//------------------------------------------------------------------------------
JINLINE void jbuf_init(jbuf_t* buf)
{
    assert(buf);
    buf->cap = 0;
    buf->len = 0;
    buf->ptr = NULL;
}

//------------------------------------------------------------------------------
JINLINE void jbuf_destroy(jbuf_t* buf)
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
    if (buf->len+cap <= buf->cap)
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
void json_init( json_t* jsn )
{
    assert(jsn);
    jmap_init(&jsn->strmap);

    // nums
    jsn->nums.cap = 0;
    jsn->nums.len = 0;
    jsn->nums.ptr = NULL;

    // arrays
    jsn->arrays.cap = 0;
    jsn->arrays.len = 0;
    jsn->arrays.ptr = NULL;

    // objs
    jsn->objs.cap = 0;
    jsn->objs.len = 0;
    jsn->objs.ptr = NULL;

    size_t idx = json_add_obj(jsn);
    assert(idx == 0);
}

//------------------------------------------------------------------------------
json_t* json_new()
{
    json_t* jsn = (json_t*)jcalloc(1, sizeof(json_t));
    json_init(jsn);
    return jsn;
}

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
    jfree(jsn->nums.ptr); jsn->nums.ptr = NULL;

    // cleanup objects
    for ( size_t i = 0; i < jsn->objs.len; i++ )
    {
        _jobj_t* obj = _json_get_obj(jsn, i);
        if (obj->cap > BUF_SIZE)
        {
            jfree(obj->kvs); obj->kvs = NULL;
        }
    }
    jfree(jsn->objs.ptr); jsn->objs.ptr = NULL;

    // cleanup arrays
    for ( size_t n = 0; n < jsn->arrays.len; n++ )
    {
        _jarray_t* array = _json_get_array(jsn, n);
        if (array->cap > BUF_SIZE)
        {
            jfree(array->vals); array->vals = NULL;
        }
    }
    jfree(jsn->arrays.ptr); jsn->arrays.ptr = NULL;
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

#pragma mark - jcontext_t

//------------------------------------------------------------------------------
JINLINE void jcontext_init(jcontext_t* ctx)
{
    assert(ctx);
    ctx->beg = NULL;
    ctx->end = NULL;
    ctx->file = NULL;
    ctx->col = 0;
    ctx->line = 0;
    *ctx->src = '\0';
    *ctx->buf = '\0';
    jbuf_init(&ctx->strbuf);
}

//------------------------------------------------------------------------------
JINLINE void jcontext_init_buf(jcontext_t* ctx, void* buf, size_t len)
{
    assert(buf);
    jcontext_init(ctx);
    ctx->beg = (char*)buf;
    ctx->end = ctx->beg + len;
}

//------------------------------------------------------------------------------
JINLINE void jcontext_set_src(jcontext_t* ctx, const char* src)
{
    if (!src)
    {
        *ctx->src = '\0';
        return;
    }

    strncpy(ctx->src, src, sizeof(ctx->src));
    ctx->src[sizeof(ctx->src)-1] = '\0';
}

//------------------------------------------------------------------------------
JINLINE void jcontext_init_file(jcontext_t* ctx, FILE* file)
{
    assert(file);
    jcontext_init(ctx);
    ctx->file = file;
}

//------------------------------------------------------------------------------
JINLINE void jcontext_destroy(jcontext_t* ctx)
{
    assert(ctx);
    jbuf_destroy(&ctx->strbuf);
}

#pragma mark - parse

//------------------------------------------------------------------------------
JINLINE int jpeek( jcontext_t* ctx )
{
    if (ctx->file && ctx->beg == ctx->end) // need to read more data from file?
    {
        // read file into buffer
        size_t len = fread(ctx->buf, 1, IO_BUF_SIZE, ctx->file);
        if (len == 0 && feof(ctx->file) != 0)
        {
            json_assert(ferror(ctx->file) == 0, "error reading file contents: '%s'", strerror(errno));
        }
        ctx->beg = ctx->buf;
        ctx->end = ctx->beg + len;
    }

    if (ctx->beg == ctx->end)
    {
        return EOF;
    }
    return *ctx->beg;
}

//------------------------------------------------------------------------------
JINLINE int jnext( jcontext_t* ctx )
{
    ++ctx->col;
    ++ctx->beg;
    return jpeek(ctx);
}

//------------------------------------------------------------------------------
JINLINE void parse_whitespace( jcontext_t* ctx )
{
    for ( int ch = jpeek(ctx); ch >= 0; ch = jnext(ctx) )
    {
        switch(ch)
        {
            case ' ':
            case '\t':
            case '\r':
            case '\v':
            case '\f':
                break;

            case '\n':
                ctx->col = 0;
                ctx->line++;
                break;

            default:
                return;
        }
    }
}

//------------------------------------------------------------------------------
JINLINE unsigned char char_to_hex(jcontext_t* ctx, int ch)
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
        default: json_assert(JFALSE, "invalid unicode hex digit: '%c'", ch);
    }
    return 0;
}

//------------------------------------------------------------------------------
JINLINE void utf8_encode(jcontext_t* ctx, int32_t codepoint, jbuf_t* str )
{
    json_assert(codepoint > 0, "invalid unicode: %d", codepoint);
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
        json_assert(JFALSE, "invalid unicode: %d", codepoint);
    }
}

//------------------------------------------------------------------------------
JINLINE void parse_literal(json_t* jsn, jcontext_t* ctx, const char* str)
{
    int ch = ch = jnext(ctx);
    for ( const char* s = ++str; *s; s++, ch = jnext(ctx))
    {
        json_assert(ch == *s, "expected string literal: '%s'", str);
    }
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_sign( jcontext_t* ctx )
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
JINLINE jnum_t parse_digitsp( jcontext_t* ctx, size_t* places)
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

    json_assert(JFALSE, "unexpected end of file");
    return num;
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_digits( jcontext_t* ctx )
{
    size_t places = 0;
    return parse_digitsp(ctx, &places);
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_num( jcontext_t* ctx )
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
JINLINE unsigned int parse_unicode_hex(jcontext_t* ctx)
{
    return char_to_hex(ctx, jpeek(ctx)) << 12 |
           char_to_hex(ctx, jnext(ctx)) << 8 |
           char_to_hex(ctx, jnext(ctx)) << 4 |
           char_to_hex(ctx, jnext(ctx));
}

//------------------------------------------------------------------------------
JINLINE void parse_unicode2( jbuf_t* str, jcontext_t* ctx )
{
    // U+XXXX
    unsigned int val = parse_unicode_hex(ctx);
//    json_error(val > 0, is, "\\u0000 is not allowed");

    // surrogate pair, \uXXXX\uXXXXX
    if (0xD800 <= val && val <= 0xDBFF)
    {
        json_assert(jnext(ctx) == '\\', "invalid unicode");
        json_assert(jnext(ctx) == 'u', "invalid unicode");

        // read the surrogate pair from the stream
        unsigned int val2 = parse_unicode_hex(ctx);

        // validate the value
        json_assert(val2 < 0xDC00 || val2 > 0xDFFF, "invalid unicode");
        unsigned int unicode = ((val - 0xD800) << 10) + (val2 - 0xDC00) + 0x10000;
        utf8_encode(ctx, unicode, str);
        return;
    }

    json_assert(0xDC00 > val || val > 0xDFFF, "invalid unicode");
    utf8_encode(ctx, val, str);
}

//------------------------------------------------------------------------------
JINLINE void parse_str(jbuf_t* str, jcontext_t* ctx)
{
    int prev = jpeek(ctx);
    json_assert(prev == '"', "Expected a String, found: '%c'", prev);

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
                        json_assert(JFALSE, "invalid escape sequence '\\%c'", ch);
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

    json_assert(JFALSE, "string terminated unexpectedly");
}

//------------------------------------------------------------------------------
JINLINE void parse_array(jarray_t array, jcontext_t* ctx)
{
    int prev = jpeek(ctx); jnext(ctx);
    json_assert(prev == '[', "Expected an array, found: '%c'", prev);
    json_t* jsn = array.json;

    while ( JTRUE )
    {
        parse_whitespace(ctx);

        jval_t val = parse_val(jsn, ctx);
        *_jarray_add_val(_jarray_get_array(array)) = val;

        parse_whitespace(ctx);

        int ch = jpeek(ctx);
        switch(ch)
        {
            case ',':
                jnext(ctx);
                break;

            case ']':
                jnext(ctx);
                jarray_truncate(array);
                return;

            default:
                json_assert(JFALSE, "expected ',' or ']' when parsing array: '%c'", ch);
        }
    }
}

//------------------------------------------------------------------------------
JINLINE void parse_obj(jobj_t obj, jcontext_t* ctx)
{
    int prev = jpeek(ctx); jnext(ctx);
    json_assert(prev == '{', "Expected an object, found: '%c'", prev);
    json_t* jsn = jobj_get_json(obj);

    while ( JTRUE )
    {
        parse_whitespace(ctx);

        // get the key
        parse_str(&ctx->strbuf, ctx);
        const char* key = ctx->strbuf.ptr;

        jkv_t* kv = jobj_add_keyl(obj, key, ctx->strbuf.len);

        parse_whitespace(ctx);
        json_assert(jpeek(ctx) == ':', "expected ':' after key: '%s', found '%c'", key, jpeek(ctx)); jnext(ctx);
        parse_whitespace(ctx);

        jkv_set_val(kv, parse_val(jsn, ctx));

        parse_whitespace(ctx);

        int ch = jpeek(ctx);
        switch (ch)
        {
            case ',':
                jnext(ctx);
                break;

            case '}':
                jnext(ctx);
                jobj_truncate(obj);
                return;

            default:
                json_assert(JFALSE, "expected ',' or '}' when parsing object: '%c'", ch);
        }
    }
}

//------------------------------------------------------------------------------
JINLINE jval_t parse_val( json_t* jsn, jcontext_t* ctx )
{
    int ch = jpeek(ctx);
    switch(ch)
    {
        case '{': // obj
        {
            size_t idx = json_add_obj(jsn);
            parse_obj((jobj_t){jsn, idx}, ctx);
            return (jval_t){JTYPE_OBJ, (uint32_t)idx};
        }

        case '[': // array
        {
            size_t idx = json_add_array(jsn);
            parse_array((jarray_t){jsn, idx}, ctx);
            return (jval_t){JTYPE_ARRAY, (uint32_t)idx};
        }

        case '"': // string
        {
            jbuf_t* buf = &ctx->strbuf;
            parse_str(buf, ctx);
            return (jval_t){JTYPE_STR, (uint32_t)json_add_strl(jsn, buf->ptr, buf->len)};
        }

        case 't': // true
            parse_literal(jsn, ctx, "true");
            return (jval_t){JTYPE_TRUE, 0};

        case 'f': // false
            parse_literal(jsn, ctx, "false");
            return (jval_t){JTYPE_FALSE, 0};

        case 'n': // null
            parse_literal(jsn, ctx, "null");
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
            jnum_t num = parse_num(ctx);
            return (jval_t){JTYPE_NUM, (uint32_t)json_add_num(jsn, num)};
        }

        default:
            json_assert(JFALSE, "invalid value: expectected: object, array, number, string, true, false, or null.");
    }

    return (jval_t){JTYPE_NIL, 0};
}


#pragma mark - io

//------------------------------------------------------------------------------
void print_mem_usage()
{
#if ( defined(__APPLE__) && defined(__MACH__) )
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);
    printf("[RAM]: Actual: %0.1f MB Virtual: %0.1f MB\n", btomb(t_info.resident_size), btomb(t_info.virtual_size));
#endif

}

//------------------------------------------------------------------------------
int json_parse_file( json_t* jsn, jcontext_t* ctx )
{
    assert(jsn);
    assert(ctx);

    if (setjmp(ctx->jerr_jmp) == 0)
    {
        int ch = jpeek(ctx);
        switch (ch)
        {
            case '{':
                parse_obj(json_root(jsn), ctx);
                break;

            default:
                break;
        }
        ctx->buf[0] = '\0';
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

//------------------------------------------------------------------------------
JINLINE const char* _json_load_file(json_t* jsn, const char* src, FILE* file)
{
    assert(jsn);
    if (!file) return "file is null";

    jcontext_t ctx;
    jcontext_init_file(&ctx, file);
    jcontext_set_src(&ctx, src);

    print_mem_usage();
    if (json_parse_file(jsn, &ctx) != 0)
    {
        json_destroy(jsn); jsn = NULL;
    }

    jcontext_destroy(&ctx);

    print_mem_usage();
    print_memory_stats(jsn);

    return *jerr_buf ? jerr_buf : NULL;
}

//------------------------------------------------------------------------------
JINLINE const char* _json_load_buf(json_t* jsn, const char* src, void* buf, size_t blen)
{
    assert(jsn);
    assert(buf);

    jcontext_t ctx;
    jcontext_init_buf(&ctx, buf, blen);
    jcontext_set_src(&ctx, src);

    // pre-allocate data based on estimate size
    size_t est = grow(ceilf(blen*0.01), 0);

    jmap_rehash(&jsn->strmap, est);
    json_nums_reserve(jsn, est);
    json_arrays_reserve(jsn, est);
    json_objs_reserve(jsn, est);

    print_mem_usage();
    if (json_parse_file(jsn, &ctx) != 0)
    {
        json_destroy(jsn); jsn = NULL;
    }

    jcontext_destroy(&ctx);

    print_mem_usage();

    if (jsn)
    {
        print_memory_stats(jsn);
    }

    return *jerr_buf ? jerr_buf : NULL;
}

//------------------------------------------------------------------------------
const char* json_load_file(json_t* jsn, FILE* file)
{
#if __LINUX__
    int fd = fileno(file);
    char src[128] = "?";
    char fdpath[128];
    jsnprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
    readlink(fdpath, src, sizeof(src));

#elif ( defined(__APPLE__) && defined(__MACH__) )

    int fd = fileno(file);
    char src[PATH_MAX];
    if (fcntl(fd, F_GETPATH, src) < 0)
    {
        strcpy(src, "?");
    }
#endif

    return _json_load_file(jsn, src, file);
}

//------------------------------------------------------------------------------
const char* json_load_buf(json_t* jsn, void* buf, size_t blen)
{
    char src[128];
    jsnprintf(src, sizeof(src), "%p", buf);
    return _json_load_buf(jsn, src, buf, blen);
}

//------------------------------------------------------------------------------
const char* json_load_path(json_t* jsn, const char* path)
{
    assert(jsn);
    assert(path);

#if JPOSIX
    int fd = open(path, O_RDONLY);
    if (!fd) return "could not read file";

    struct stat st;
    int rt = fstat(fd, &st);
    if (rt != 0)
    {
        close(fd);
        return "could not read file";
    }

    size_t len = st.st_size;
    if (len == 0)
    {
        close(fd);
        return "could not parse empty file";
    }

    void* mem = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    const char* err = _json_load_buf(jsn, path, mem, len);
    munmap(mem, len);

    close(fd);

#else
    FILE* file = fopen(path, "r");
    if (!file) return "could not open file for read";
    const char* err = _json_load_file(jsn, path, file);
    fclose(file);
#endif

    print_mem_usage();

    return err;
}

//------------------------------------------------------------------------------
void print_memory_stats(json_t* jsn)
{
    assert(jsn);
#define PRINT_MEMORY 1
#if PRINT_MEMORY
    size_t total = 0;
    size_t total_reserve = 0;

    if (jsn->arrays.len > 0)
    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->arrays.len; ++i )
        {
            _jarray_t* array = _json_get_array(jsn, i);
            if (array->cap > BUF_SIZE)
            {
                mem += array->len * sizeof(jval_t);
                reserve += array->cap * sizeof(jval_t);
            }
        }
        mem += jsn->arrays.len * sizeof(_jarray_t);
        reserve += jsn->arrays.cap * sizeof(_jarray_t);
        printf("[ARRAY] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->arrays.len);

        total += mem;
        total_reserve += reserve;
    }

    if (jsn->objs.len > 0)
    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->objs.len; ++i )
        {
            _jobj_t* obj = _json_get_obj(jsn, i);
            if (obj->cap > BUF_SIZE)
            {
                mem += obj->len * sizeof(jkv_t);
                reserve += obj->cap * sizeof(jkv_t);
            }
        }
        mem += jsn->objs.len * sizeof(_jobj_t);
        reserve += jsn->objs.cap * sizeof(_jobj_t);
        printf("[OBJECT] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->objs.len);

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
        for ( size_t n = 0; n < jsn->strmap.bcap; ++n )
        {
            jmapbucket_t* bucket = &jsn->strmap.buckets[n];
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
        mem += jsn->nums.len * sizeof(jnum_t);
        reserve += jsn->nums.cap * sizeof(jnum_t);

        printf("[NUMBERS] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100);

        total += mem;
        total_reserve += reserve;
    }

    printf("[TOTAL] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(total), btomb(total_reserve), total / (double)total_reserve * 100);
    print_mem_usage();
#endif
}
