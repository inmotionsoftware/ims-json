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

#ifndef MAX
    #define MAX(A,B) (A)>(B)?(A):(B)
#endif

#ifndef MIN
    #define MIN(A,B) (A)<(B)?(A):(B)
#endif

#ifndef json_assert
    #define json_assert(A, STR, ...) { if (!(A)) {printf(STR "\n", ## __VA_ARGS__ ); abort();} }
#endif

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
    uint32_t key;
    jval_t val;
};

//------------------------------------------------------------------------------
struct _jobj_t
{
    struct json_t* json;
    size_t cap;
    size_t len;
    jkv_t* kvs;
    jkv_t buf[10];
};

//------------------------------------------------------------------------------
struct _jarray_t
{
    struct json_t* json;
    size_t cap;
    size_t len;
    jval_t* vals;
    jval_t buf[10];
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
//int jstr_cmp(jstr_t* str, const char* cstr)
//{
//    // TODO
//    char* buf = (str->chars) ? str->chars : str->buf;
//    for ( size_t i = 0; i < str->len; ++i, cstr++ )
//    {
//        if (*buf != *cstr)
//        {
//            return *buf - *cstr;
//        }
//    }
//
//    if (cstr) return -1;
//    return 0;
//}
//
//int jstr_ncmp(jstr_t* str, const char* cstr, size_t _len)
//{
//    // TODO
//    size_t len = MIN(str->len, _len);
//    char* buf = (str->chars) ? str->chars : str->buf;
//    for ( size_t i = 0; i < len; ++i, cstr++ )
//    {
//        if (*buf != *cstr)
//        {
//            return *buf - *cstr;
//        }
//    }
//
//    if (_len == str->len) return 0;
//
//    return str->len > _len ? -1 : 1;
//}

//------------------------------------------------------------------------------
static size_t json_add_obj( json_t* j )
{
    assert(j);
    size_t idx = j->objs.size();
    j->objs.push_back( (_jobj_t)
    {
        .json = j,
        .cap = 10,
        .len = 0,
        .kvs = NULL
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
        .cap = 10,
        .len = 0,
        .vals = NULL
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
    assert(j && str);
    static const uint32_t MURMER32_SEED = 0;

    size_t idx = j->strs.size();
    j->strs.emplace_back();
    jstr_t* jstr = &j->strs.back();

    assert(str);
    size_t len = strlen(str);
    uint32_t hash = murmur3_32(str, MIN(30, len), MURMER32_SEED);
    jstr->hash = hash;
    jstr->len = len;

    char* buf = NULL;
    if (len > 10)
    {
        buf = jstr->chars = (char*)malloc( len );
    }
    else
    {
        buf = jstr->buf;
    }

    memcpy(buf, str, len * sizeof(char));
    return idx;
}

static inline _jobj_t* get_obj(jobj_t obj)
{
    assert(obj.json);
    assert(obj.idx < obj.json->objs.size());
    _jobj_t* rt = &obj.json->objs[obj.idx];
    assert(rt->json == obj.json);
    return rt;
}

static inline _jarray_t* get_array(jarray_t array)
{
    assert(array.json);
    assert(array.idx < array.json->arrays.size());
    _jarray_t* rt = &array.json->arrays[array.idx];
    assert(rt->json == array.json);
    return rt;
}

//------------------------------------------------------------------------------
void jobj_reserve( _jobj_t* obj, size_t cap )
{
    if ( obj->len+cap < obj->cap)
        return;

    assert( (obj->len < 10 && !obj->kvs) || (obj->kvs && obj->len >= 10) );
    cap = MAX(obj->len+cap, obj->cap*1.2+1);

    jkv_t* kvs = (jkv_t*)realloc( obj->kvs, cap * sizeof(jkv_t) );
    assert (kvs);

    // transition from our internal stack buffer to a heap buffer
    if (!obj->kvs)
    {
        memcpy(kvs, obj->buf, obj->len * sizeof(jkv_t));
    }
    obj->kvs = kvs;
    obj->cap = cap;
}

jkv_t* jobj_get_kv(_jobj_t* obj, size_t idx)
{
    assert(idx < obj->len);
    assert (idx < obj->len);
    assert( (obj->len < 10 && !obj->kvs) || (obj->kvs && obj->len >= 10) );
    return (obj->kvs) ? &obj->kvs[idx] : &obj->buf[idx];
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

    size_t kidx = json_add_str(obj->json, key);
    size_t idx = json_add_num(obj->json, num);

    assert (kidx < 4294967296);
    assert (idx < 268435456 /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NUM;
    kv->val.idx = (uint32_t)idx;
}

void jobj_add_str( jobj_t _obj, const char* key, const char* str )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(obj->json, key);
    size_t idx = json_add_str(obj->json, str);

    assert (kidx < 4294967296);
    assert (idx < 268435456 /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_STR;
    kv->val.idx = (uint32_t)idx;
}

void jobj_add_bool( jobj_t _obj, const char* key, jbool b )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(obj->json, key);
    assert (kidx < 4294967296);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = (b) ? JTYPE_TRUE : JTYPE_FALSE;
    kv->val.idx = 0;
}

void jobj_add_nil( jobj_t _obj, const char* key )
{
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(obj->json, key);
    assert (kidx < 4294967296);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;
}

jarray_t jobj_add_array( jobj_t _obj, const char* key )
{
    size_t idx = json_add_array(_obj.json);
    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(obj->json, key);
    assert (kidx < 4294967296);
    assert (idx < 268435456 /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_ARRAY;
    kv->val.idx = (uint32_t)idx;
    return (jarray_t){ obj->json, idx };
}

jobj_t jobj_add_obj( jobj_t _obj, const char* key )
{
    size_t idx = json_add_obj(_obj.json);

    _jobj_t* obj = get_obj(_obj);

    size_t kidx = json_add_str(obj->json, key);
    assert (kidx < 4294967296);
    assert (idx < 268435456 /* 2^28 */);

    jkv_t* kv = jobj_add_kv(obj);
    kv->key = (uint32_t)kidx;
    kv->val.type = JTYPE_OBJ;
    kv->val.idx = (uint32_t)idx;

    return (jobj_t){ obj->json, idx };
}

//------------------------------------------------------------------------------
void jarray_reserve( _jarray_t* a, size_t cap )
{
    if ( a->len+cap < a->cap )
        return;

    assert( (a->len < 10 && !a->vals) || (a->vals && a->len >= 10) );
    cap = MAX(a->len+cap, a->cap*1.2+1);

    jval_t* vals = (jval_t*)realloc( a->vals, cap * sizeof(jkv_t) );
    assert(vals);

    // transition from our internal stack buffer to a heap buffer
    if (!a->vals)
    {
        memcpy(vals, a->buf, a->len * sizeof(jval_t));
    }
    a->vals = vals;
    a->cap = cap;
}

jval_t* jarray_get_val( _jarray_t* a, size_t idx)
{
    assert(a);
    assert(idx < a->len);
    assert( (a->len < 10 && !a->vals) || (a->len >= 10 && a->vals) );
    return (a->vals) ? &a->vals[idx] : &a->buf[idx];
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

    assert (idx < 268435456 /* 2^28 */);
    val->idx = (uint32_t)idx;
}

void jarray_add_str( jarray_t _a, const char* str )
{
    size_t idx = json_add_str(_a.json, str);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_STR;

    assert (idx < 268435456 /* 2^28 */);
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

    assert (idx < 268435456 /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jarray_t){ a->json, idx };
}

jobj_t jarray_add_obj( jarray_t _a )
{
    size_t idx = json_add_obj(_a.json);

    _jarray_t* a = get_array(_a);
    jval_t* val = jarray_add_val(a);
    val->type = JTYPE_OBJ;

    assert (idx < 268435456 /* 2^28 */);
    val->idx = (uint32_t)idx;

    return (jobj_t){ a->json, idx };
}

#define PRINT 0
#if PRINT
    #define json_fprintf(F, FMT, ...) fprintf(F, FMT, ## __VA_ARGS__ )
#else
    #define json_fprintf(F, FMT, ...)
#endif

//------------------------------------------------------------------------------
static inline void jobj_print(jobj_t root, size_t depth, FILE* f);
static inline void jstr_print( jstr_t* str, FILE* f )
{
    json_fprintf(f, "\"");

#if PRINT
    const char* chars = (str->chars) ? str->chars : str->buf;
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

    json_t* j = root->json;
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
                jstr_print(&j->strs[val->idx], f);
                break;

            case JTYPE_NUM:
            {
                jnum_t num = j->nums[val->idx];
                jnum_t fract = num - floor(num);
                json_fprintf(f, (fract > 0) ? "%f" : "%0.0f", num);
                break;
            }

            case JTYPE_ARRAY:
                jarray_print((jarray_t){root->json, val->idx}, depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print((jobj_t){root->json, val->idx}, depth+1, f);
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

    json_t* j = root->json;
    for ( size_t i = 0; i < root->len; ++i )
    {
        jkv_t* kv = jobj_get_kv(root, i);
        jstr_t* key = &j->strs[kv->key];

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
                jstr_print(&j->strs[kv->val.idx], f);
                break;

            case JTYPE_NUM:
                json_fprintf(f, "%f", j->nums[kv->val.idx]);
                break;

            case JTYPE_ARRAY:
                jarray_print((jarray_t){root->json, kv->val.idx}, depth+1, f);
                break;

            case JTYPE_OBJ:
                jobj_print((jobj_t){root->json, kv->val.idx}, depth+1, f);
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
    json_fprintf(f, "{");
}

//------------------------------------------------------------------------------
void json_print(json_t* j, FILE* f)
{
    jobj_print(json_root(j), 0, f);
}

json_t* json_new()
{
    json_t* j = new json_t();
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
inline void eat_whitespace( T& beg, T end )
{
    for (; beg != end; beg++ )
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
inline jnum_t parse_sign( T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");
    switch (*beg)
    {
        case '-':
            beg++;
            return -1;

        case '+':
            beg++;
            return 1;

        default:
            return 1;
    }
}

//------------------------------------------------------------------------------
static jnum_t __ignore = 0;
template < typename T >
inline jnum_t parse_digits( T& beg, T end, jnum_t& places = __ignore)
{
    json_assert(beg != end, "unexpected end of file");

    jnum_t num = 0;
    T start = beg;
    for (; beg != end; beg++)
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
                places = pow(10, std::distance(beg, start));
                return num;
            }
        }
    }

    json_assert(false, "unexpected end of file");
    return num;
}


//------------------------------------------------------------------------------
template < typename T >
inline jnum_t parse_number( T& beg, T end )
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
inline unsigned char char_to_hex(T& beg, T end)
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

    cap = MAX(buf->len+cap+1, buf->cap*1.2+2);
    buf->cap = MAX(25, cap);
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
inline unsigned int read_unicode_hex(T& beg, T end)
{
    return  char_to_hex(beg, end) << 12 |
            char_to_hex(beg, end) << 8 |
            char_to_hex(beg, end) << 4 |
            char_to_hex(beg, end);
}

//------------------------------------------------------------------------------
template < typename T >
inline void parse_unicode( buf_t* str, T& beg, T end )
{
    // U+XXXX
    unsigned int val = read_unicode_hex(beg, end);
//    json_error(val > 0, is, "\\u0000 is not allowed");

    // surrogate pair, \uXXXX\uXXXXX
    if (0xD800 <= val && val <= 0xDBFF)
    {
        json_assert(*beg++ == '\\', "invalid unicode");
        json_assert(beg != end, "unexpected end of stream");
        json_assert(*beg++ == 'u', "invalid unicode");

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
inline void parse_string( T& beg, T end, buf_t* str )
{
    json_assert(beg != end, "unexpected end of file");
    json_assert(*beg == '"', "not a valid string");

    buf_clear(str);

    char ch;
    for (char prev = *beg++; beg != end; beg++ )
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
inline bool parse_true( T& beg, T end )
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
inline bool parse_false( T& beg, T end )
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
inline std::nullptr_t parse_null( T& beg, T end )
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
inline void parse_array( jarray_t a, T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");
    while (beg++ != end)
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
                beg++;
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
inline void parse_obj( jobj_t obj, T& beg, T end )
{
    json_t* jsn = obj.json;

    json_assert(beg != end, "unexpected end of file");
    while (beg++ != end)
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

json_t* json_load_file( const char* path )
{
    int fd = open(path, O_RDONLY);
    if (!fd)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        close(fd);
        return NULL;
    }

    void* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE|MAP_NORESERVE, fd, 0);

    json_t* jsn = json_new();

    const char* cptr = (const char*)ptr;
    parse_obj(json_root(jsn), cptr, cptr+st.st_size);

    munmap(ptr, st.st_size);

    return jsn;
}
