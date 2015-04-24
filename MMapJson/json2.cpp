//
//  json2.c
//  MMapJson
//
//  Created by Brian Howard on 4/20/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include "json2.h"
#include <math.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <errno.h>

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

#define BUF_SIZE ((size_t)4)
#define MAX_VAL_IDX 268435456 // 2^28
#define MAX_KEY_IDX UINT32_MAX

//------------------------------------------------------------------------------
static inline size_t grow( size_t min, size_t cur )
{
    static const jnum_t GROWTH_FACTOR = 1.05;
    assert(min >= cur);

    size_t size;
    size = MAX(min, BUF_SIZE*2);
    size = MAX(size, cur*GROWTH_FACTOR+2);

    assert(size > cur);
    return size;
}

//------------------------------------------------------------------------------
struct jstr_t
{
    size_t len;
    uint32_t hash;
    union
    {
        char* chars;
        char buf[BUF_SIZE];
    };
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
    uint32_t key;
    jval_t val;
};

//------------------------------------------------------------------------------
struct _jobj_t
{
    struct json_t* json;
    jsize_t cap;
    jsize_t len;
    union
    {
        jkv_t* kvs;
        jkv_t buf[BUF_SIZE];
    };
};

//------------------------------------------------------------------------------
struct _jarray_t
{
    struct json_t* json;
    jsize_t cap;
    jsize_t len;
    union
    {
        jval_t* vals;
        jval_t buf[BUF_SIZE];
    };
};

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

uint32_t strhash( const char* key, size_t len )
{
    static const uint32_t MURMER32_SEED = 0;
    static const size_t MAX_CHARS = 30;
    return murmur3_32(key, MIN(MAX_CHARS, len), MURMER32_SEED);
}

size_t json_new_str(json_t* jsn, const char* cstr, size_t len, uint32_t hash)
{
    // not found, create a new one
    size_t rt = jsn->strs.size();
    jsn->strs.emplace_back();
    jstr_t* jstr = &jsn->strs.back();

    jstr->hash = hash;
    jstr->len = len;

    char* buf = NULL;
    if (len > BUF_SIZE)
    {
        buf = jstr->chars = (char*)malloc( len * sizeof(char) );
    }
    else
    {
        buf = jstr->buf;
    }

    jsn->bytes += len;
    memcpy(buf, cstr, len * sizeof(char));
    return rt;
}

//void jmap_rebuild( jmap_t* map )
//{
//    static const double load_factor = 0.2;
//
//    size_t cap = ceil( (double)map->len * (1.0-load_factor));
//    map->buckets = (jmapbucket_t*)realloc(map->buckets, sizeof(jmapbucket_t) * cap);
//
//}

size_t json_find_str(json_t* jsn, const char* cstr)
{
    size_t slen = strlen(cstr);
    uint32_t hash = strhash(cstr, slen);

    jmap_t* map = &jsn->map;
    if (!map->buckets)
    {
        map->cap = 1024*1024;
        map->buckets = (jmapbucket_t*)calloc( sizeof(jmapbucket_t), map->cap );
    }

    size_t idx = hash % map->cap;
    jmapbucket_t* bucket = &map->buckets[idx];

    size_t rt = SIZE_T_MAX;

    for ( size_t i = 0; i < bucket->len; ++i )
    {
        size_t idx = bucket->slots[i];
        jstr_t* str = &jsn->strs[idx];
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
        size_t req = bucket->len+1;
        if (req >= bucket->cap)
        {
            bucket->cap = grow(req, bucket->cap);
            bucket->slots = (size_t*)calloc(sizeof(size_t), bucket->cap);
        }

        // not found, create a new one
        rt = json_new_str(jsn, cstr, slen, hash);

        map->len++;
        bucket->slots[bucket->len++] = rt;
    }

    return rt;
}

//------------------------------------------------------------------------------
static json_t* jarray_get_json( _jarray_t* array )
{
    return array->json;
}

static json_t* jobj_get_json( _jobj_t* obj )
{
    return obj->json;
}

static size_t json_add_obj( json_t* j )
{
    assert(j);
    size_t idx = j->objs.size();
    j->objs.push_back( (_jobj_t)
    {
        .json = j,
        .cap = BUF_SIZE,
        .len = 0,
        .buf = {}
    });
    return idx;
}

static size_t json_add_array( json_t* j )
{
    assert(j);
    size_t idx = j->arrays.size();
    j->arrays.push_back((_jarray_t)
    {
        .json = j,
        .cap = BUF_SIZE,
        .len = 0,
        .buf = {}
    });
    return idx;
}

static size_t json_add_num( json_t* j, jnum_t n )
{
    assert(j);
    size_t idx = j->nums.size();
    j->nums.push_back(n);
    return idx;
}

static size_t json_add_str( json_t* j, const char* str )
{
    size_t idx = json_find_str(j, str);
    assert (idx != SIZE_T_MAX);
    return idx;
}

static inline _jobj_t* get_obj(jobj_t obj)
{
    assert(obj.json);
    assert(obj.idx < obj.json->objs.size());
    _jobj_t* rt = &obj.json->objs[obj.idx];
    assert(jobj_get_json(rt) == obj.json);
    return rt;
}

static inline _jarray_t* get_array(jarray_t array)
{
    assert(array.json);
    assert(array.idx < array.json->arrays.size());
    _jarray_t* rt = &array.json->arrays[array.idx];
    assert(jarray_get_json(rt) == array.json);
    return rt;
}

//------------------------------------------------------------------------------
void jobj_truncate( _jobj_t* obj )
{
    if (obj->len == obj->cap)
        return;

    if (obj->cap <= BUF_SIZE)
        return;

    obj->kvs = (jkv_t*)realloc(obj->kvs, obj->len * sizeof(jkv_t));
    obj->cap = obj->len;
}

void jobj_reserve( _jobj_t* obj, size_t cap )
{
    if ( obj->len+cap < obj->cap)
        return;

    size_t prev_cap = obj->cap;
    obj->cap = (jsize_t)grow(obj->len+cap, obj->cap);

    if (prev_cap <= BUF_SIZE)
    {
        jkv_t* kvs = (jkv_t*)malloc( obj->cap * sizeof(jkv_t));
        memcpy(kvs, obj->buf, sizeof(jkv_t) * obj->len);
        obj->kvs = kvs;
    }
    else
    {
        obj->kvs = (jkv_t*)realloc(obj->kvs, sizeof(jkv_t)*obj->cap);
    }
}

jkv_t* jobj_get_kv(_jobj_t* obj, size_t idx)
{
    assert(idx < obj->len);
    assert (idx < obj->len);
    return (obj->cap > BUF_SIZE) ? &obj->kvs[idx] : &obj->buf[idx];
}

jkv_t* jobj_add_kv(_jobj_t* obj)
{
    jobj_reserve(obj, 1);
    jkv_t* kv = jobj_get_kv(obj, obj->len++);
    *kv = {0}; // clear out
    return kv;
}

void jobj_add_num( jobj_t _obj, const char* key, jnum_t num )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    size_t idx = json_add_num(jobj_get_json(obj), num);

    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NUM;
    kv->val.idx = (uint32_t)idx;
}

void jobj_add_str( jobj_t _obj, const char* key, const char* str )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    size_t idx = json_add_str(jobj_get_json(obj), str);

    assert (kidx < MAX_KEY_IDX);
    assert (idx < MAX_VAL_IDX /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_STR;
    kv->val.idx = (uint32_t)idx;
}

void jobj_add_bool( jobj_t _obj, const char* key, jbool b )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    assert (kidx < MAX_KEY_IDX);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    kv->val.idx = 0;
}

void jobj_add_nil( jobj_t _obj, const char* key )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(jobj_get_json(obj), key);
    assert (kidx < MAX_KEY_IDX);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;
}

jarray_t jobj_add_array( jobj_t _obj, const char* key )
{
    size_t idx = json_add_array(_obj.json);
    _jobj_t* obj = get_obj(_obj);
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

jobj_t jobj_add_obj( jobj_t _obj, const char* key )
{
    size_t idx = json_add_obj(_obj.json);

    _jobj_t* obj = get_obj(_obj);
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
void jarray_truncate( _jarray_t* array )
{
    if (array->len == array->cap)
        return;

    if (array->cap <= BUF_SIZE)
        return;

    array->vals = (jval_t*)realloc(array->vals, array->len * sizeof(jval_t));
    array->cap = array->len;
}

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
        a->vals = (jval_t*)realloc( a->vals, a->cap * sizeof(jkv_t) );
    }
}

jval_t* jarray_get_val( _jarray_t* a, size_t idx)
{
    assert(a);
    assert(idx < a->len);
    return (a->cap > BUF_SIZE) ? &a->vals[idx] : &a->buf[idx];
}

jval_t* jarray_add_val( _jarray_t* a)
{
    assert(a);
    jarray_reserve(a, 1);
    jval_t* val = jarray_get_val(a, a->len++);
    *val = {0}; // clear out
    return val;
}

void jarray_add_num( jarray_t _a, jnum_t num )
{
    size_t idx = json_add_num(_a.json, num);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_NUM;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;
}

void jarray_add_str( jarray_t _a, const char* str )
{
    size_t idx = json_add_str(_a.json, str);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_STR;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;
}

void jarray_add_bool( jarray_t _a, jbool b )
{
    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    val->idx = 0;
}

void jarray_add_nil( jarray_t _a )
{
    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_NIL;
    val->idx = 0;
}

jarray_t jarray_add_array( jarray_t _a )
{
    size_t idx = json_add_array(_a.json);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_ARRAY;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jarray_t){ jarray_get_json(a), idx };
}

jobj_t jarray_add_obj( jarray_t _a )
{
    size_t idx = json_add_obj(_a.json);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_OBJ;

    assert (idx < MAX_VAL_IDX /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jobj_t){ jarray_get_json(a), idx };
}

//------------------------------------------------------------------------------
static inline void jobj_print(jobj_t root, size_t depth, FILE* f);
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

static void print_tabs( size_t cnt, FILE* f)
{
    while (cnt--)
    {
        json_fprintf(f, "   ");
    }
}

static inline void jarray_print(jarray_t _root, size_t depth, FILE* f)
{
    json_fprintf(f, "[");

    _jarray_t* root = get_array(_root);

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
                jstr_print(&jsn->strs[val->idx], f);
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

static inline void jobj_print(jobj_t _root, size_t depth, FILE* f)
{
    json_fprintf(f, "{");

    _jobj_t* root = get_obj(_root);

    json_t* jsn = jobj_get_json(root);
    for ( size_t i = 0; i < root->len; ++i )
    {
        jkv_t* kv = jobj_get_kv(root, i);
        jstr_t* key = &jsn->strs[kv->key];

        json_fprintf(f, "\n");
        print_tabs(depth+1, f);
        jstr_print(key, f);
        json_fprintf(f, ": ");

        switch (kv->val.type)
        {
            case JTYPE_NIL:
                json_fprintf(f, "nil");
                break;

            case JTYPE_STR:
                jstr_print(&jsn->strs[kv->val.idx], f);
                break;

            case JTYPE_NUM:
                json_fprintf(f, "%f", jsn->nums[kv->val.idx]);
                break;

            case JTYPE_ARRAY:
                jarray_print((jarray_t){jsn, kv->val.idx}, depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print((jobj_t){jsn, kv->val.idx}, depth+1, f);
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
void jmap_init(jmap_t* map)
{
    map->cap = 0;
    map->len = 0;
    map->buckets = NULL;
}

//------------------------------------------------------------------------------
void json_print(json_t* j, FILE* f)
{
    jobj_print(json_root(j), 0, f);
    json_fprintf(f, "\n");
}

json_t* json_new()
{
    json_t* j = new json_t();

    jmap_init(&j->map);

    j->bytes = 0;
    j->buf.len = 0;
    j->buf.cap = 0;
    j->buf.buf = NULL;
    size_t idx = json_add_obj(j);
    assert(idx == 0);
    return j;
}

void json_free(json_t* j)
{
    delete j;
}

jobj_t json_root(json_t* j)
{
    return (jobj_t){j, 0};
}

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

static inline void buf_clear( buf_t* buf )
{
    if (buf->buf && buf->len > 0)
        buf->buf[0] = '\0';

    buf->len = 0;
}

static inline void buf_reserve( buf_t* buf, size_t cap )
{
    if (buf->len+cap < buf->cap)
        return;

    buf->cap = grow(buf->len+cap, buf->cap);
    buf->buf = (char*)realloc(buf->buf, buf->cap * sizeof(char));
    assert (buf->buf);
}

static inline void buf_add( buf_t* buf, char ch )
{
    buf_reserve(buf, 1);
    buf->buf[buf->len++] = ch;
}

//------------------------------------------------------------------------------
static inline void utf8_encode(int32_t codepoint, buf_t* str )
{
    json_assert(codepoint > 0, "invalid unicode");
    if(codepoint < 0x80)
    {
        buf_add(str, (char)codepoint);
    }
    else if(codepoint < 0x800)
    {
        buf_add(str, 0xC0 + ((codepoint & 0x7C0) >> 6));
        buf_add(str, 0x80 + ((codepoint & 0x03F)));
    }
    else if(codepoint < 0x10000)
    {
        buf_add(str, 0xE0 + ((codepoint & 0xF000) >> 12));
        buf_add(str, 0x80 + ((codepoint & 0x0FC0) >> 6));
        buf_add(str, 0x80 + ((codepoint & 0x003F)));
    }
    else if(codepoint <= 0x10FFFF)
    {
        buf_add(str, 0xF0 + ((codepoint & 0x1C0000) >> 18));
        buf_add(str, 0x80 + ((codepoint & 0x03F000) >> 12));
        buf_add(str, 0x80 + ((codepoint & 0x000FC0) >> 6));
        buf_add(str, 0x80 + ((codepoint & 0x00003F)));
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
inline void parse_unicode( buf_t* str, T& beg, const T& end )
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
inline void parse_string( T& beg, const T& end, buf_t* str )
{
    json_assert(beg != end, "unexpected end of file");
    json_assert(*beg == '"', "not a valid string: '%c'", *beg);

    buf_clear(str);

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
                        buf_add(str, '/');
                        break;
                    case 'b':
                        buf_add(str, '\b');
                        break;
                    case 'f':
                        buf_add(str, '\f');
                        break;
                    case 'n':
                        buf_add(str, '\n');
                        break;
                    case 'r':
                        buf_add(str, '\r');
                        break;
                    case 't':
                        buf_add(str, '\t');
                        break;
                    case 'u':
                        parse_unicode(str, beg, end);
                        break;
                    case '"':
                        buf_add(str, '\"');
                        break;
                    case '\\':
                        buf_add(str, '\\');
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
                        buf_add(str, '\0');
                        return;

                    default:
                        buf_add(str, ch);
                        break;
                }
                break;
            }
        }
        prev = ch;
    }

    json_assert(false, "string terminated unexpectedly");
    buf_add(str, '\0');
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
inline std::nullptr_t parse_null( T& beg, const T& end )
{
    json_assert(*++beg == 'u' && beg != end &&
                *++beg == 'l' && beg != end &&
                *++beg == 'l' && beg != end,
        "expected literal 'true'");
    ++beg;
    return nullptr;
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
                buf_t* buf = &a.json->buf;
                parse_string(beg, end, buf);
                jarray_add_str(a, buf->buf);
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
                jarray_truncate(get_array(a));
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

        parse_string(beg, end, &jsn->buf);
        const char* key = jsn->buf.buf;

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
                buf_t buf = {0};
                parse_string(beg, end, &buf);
                jobj_add_str(obj, key, buf.buf);
                free(buf.buf);
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
                jobj_truncate(get_obj(obj));
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

static inline jnum_t btomb(size_t bytes)
{
    return (bytes / (jnum_t)(1024*1024));
}

class stdfile
{
public:
    stdfile()
        : m_file(NULL)
        , m_off(0)
        , m_pos(0)
        , m_buflen(0)
    {}

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

        size_t len = std::min(m_len, psize);
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
                len = std::min(len, m_len-m_off);
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

void print_mem_usage()
{
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

    printf("[RAM]: Actual: %0.1f MB Virtual: %0.1f MB\n", btomb(t_info.resident_size), btomb(t_info.virtual_size));

// resident size is in t_info.resident_size;
// virtual size is in t_info.virtual_size;

//    vm_size_t page_size;
//    mach_port_t mach_port;
//    mach_msg_type_number_t count;
//    vm_statistics64_data_t vm_stats;
//
//    mach_port = mach_host_self();
//    count = sizeof(vm_stats) / sizeof(natural_t);
//    if (KERN_SUCCESS == host_page_size(mach_port, &page_size) &&
//        KERN_SUCCESS == host_statistics64(mach_port, HOST_VM_INFO,
//                                        (host_info64_t)&vm_stats, &count))
//    {
//        long long free_memory = (int64_t)vm_stats.free_count * (int64_t)page_size;
//
//        long long used_memory = ((int64_t)vm_stats.active_count +
//                                 (int64_t)vm_stats.inactive_count +
//                                 (int64_t)vm_stats.wire_count) *  (int64_t)page_size;
//
//        printf("[%0.1fMB]/[%0.1fMB]\n", btomb(used_memory), btomb(free_memory));
//    }

//    errno = 0;
//    struct rusage memory = {0};
//    getrusage(RUSAGE_SELF, &memory);
//    if(errno == EFAULT)
//        printf("Error: EFAULT\n");
//    else if(errno == EINVAL)
//        printf("Error: EINVAL\n");
//
//    printf("RAM: %0.1f MB\n", btomb(memory.ru_maxrss));
//
//    printf("Usage: %ld\n", memory.ru_ixrss);
//    printf("Usage: %ld\n", memory.ru_isrss);
//    printf("Usage: %ld\n", memory.ru_idrss);
//    printf("Max: %ld\n", memory.ru_maxrss);
}


json_t* json_load_file( const char* path )
{

#define PAGED_READ      0x1
#define STD_READ        0x2
#define ONESHOT_READ    0x4

#define READ_METHOD STD_READ

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

    stdfile beg = path;
    stdfile end;

    print_mem_usage();
    json_t* jsn = json_new();
    parse_obj(json_root(jsn), beg, end);
    print_mem_usage();

#elif READ_METHOD == ONESHOT_READ
    printf("Parsing json using [mmap] method\n");

    int fd = open(path, O_RDONLY);
    if (!fd) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        return NULL;
    }

    printf("[FILE_SIZE]: %0.1f MB\n", btomb(st.st_size) );

    print_mem_usage();
    void* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_SHARED|MAP_NORESERVE, fd, 0);
    posix_madvise(ptr, st.st_size, POSIX_MADV_SEQUENTIAL|POSIX_MADV_WILLNEED);

    print_mem_usage();

    json_t* jsn = json_new();

    const char* cptr = (const char*)ptr;
    parse_obj(json_root(jsn), cptr, cptr+st.st_size);

    print_mem_usage();

    munmap(ptr, st.st_size);

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

    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->arrays.size(); ++i )
        {
            _jarray_t* array = &jsn->arrays[i];
            if (array->cap > BUF_SIZE)
            {
                mem += array->len * sizeof(jval_t);
                reserve += array->cap * sizeof(jval_t);
            }
        }
        mem += jsn->arrays.size() * sizeof(_jarray_t);
        reserve += jsn->arrays.capacity() * sizeof(_jarray_t);
        printf("[ARRAY] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->arrays.size());

        total += mem;
        total_reserve += reserve;
    }

    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->objs.size(); ++i )
        {
            _jobj_t* obj = &jsn->objs[i];
            if (obj->cap > BUF_SIZE)
            {
                mem += obj->len * sizeof(jkv_t);
                reserve += obj->cap * sizeof(jkv_t);
            }
        }
        mem += jsn->objs.size() * sizeof(_jobj_t);
        reserve += jsn->objs.capacity() * sizeof(_jobj_t);
        printf("[OBJECT] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%] Size: %zu\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100, jsn->objs.size());

        total += mem;
        total_reserve += reserve;
    }

    {
        size_t mem = 0;
        size_t reserve = 0;
        for ( size_t i = 0; i < jsn->strs.size(); ++i )
        {
            jstr_t* str = &jsn->strs[i];
            mem += str->len;
            reserve += str->len;
        }
        mem += jsn->strs.size() * sizeof(jstr_t);
        reserve += jsn->strs.capacity() * sizeof(jstr_t);
        printf("[STRING] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100);

        total += mem;
        total_reserve += reserve;
    }

    {
        size_t mem = 0;
        size_t reserve = 0;
        mem += jsn->nums.size() * sizeof(jnum_t);
        reserve += jsn->nums.capacity() * sizeof(jnum_t);

        printf("[NUMBERS] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(mem), btomb(reserve), mem / (double)reserve * 100);

        total += mem;
        total_reserve += reserve;
    }

    printf("[TOTAL] Used: %0.1f MB, Reserved: %0.1f MB [%0.1f%%]\n", btomb(total), btomb(total_reserve), total / (double)total_reserve * 100);
#endif

    return jsn;
}
