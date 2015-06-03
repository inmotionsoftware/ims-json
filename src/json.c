/*!
    @file json.c
    @author Brian Howard
    @copyright
    Copyright (c) 2015 InMotion Software, LLC.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/

#include "json.h"
#include <math.h>
#include <errno.h>
#include <memory.h>
#include <limits.h>
#include <stddef.h>
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#pragma mark - macros

// check to see if we have posix support
#if (!defined(_WIN32) && (defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))))
    #include <unistd.h>
    #include <fcntl.h>
    #if (_POSIX_VERSION >= 199506L)
        #define J_USE_POSIX 1
    #endif
#endif

#define json_do_err(CTX) longjmp(ctx->jerr_jmp, EXIT_FAILURE)
//#define json_do_err(CTX) abort()

#pragma mark - constants

const char JVER[32] = "0.9.0.1";

#define JINLINE static __inline

#define BUF_SIZE ((size_t)6)
#define MAX_VAL_IDX 268435456 // 2^28
#define MAX_KEY_IDX UINT32_MAX

#define JMAP_MAX_LOADFACTOR 0.8f
#define JMAP_IDEAL_LOADFACTOR 0.3f

#define IO_BUF_SIZE 4096
#define JMAX_SRC_STR 128

#pragma mark - structs

//------------------------------------------------------------------------------
typedef uint32_t jhash_t;
typedef uint32_t jsize_t;

//------------------------------------------------------------------------------
struct jbuf_t
{
    size_t cap;
    size_t len;
    char* ptr;
};
typedef struct jbuf_t jbuf_t;

//------------------------------------------------------------------------------
struct jprint_t
{
    print_func print;
    void* udata;
    char tab[10];
    size_t ntab;
    char newline[4];
    size_t nnewline;
    char space[2];
    size_t nspace;
    size_t nbytes;
    jbool_t esc_uni;
    jmp_buf jerr_jmp;
};
typedef struct jprint_t jprint_t;

//------------------------------------------------------------------------------
struct jcontext_t
{
    const char* beg;
    const char* end;
    char buf[IO_BUF_SIZE];
    FILE* file;

    jerr_t* err;

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
    } str;
};
typedef struct jstr_t jstr_t;

//------------------------------------------------------------------------------
struct jkv_t
{
    union
    {
        uint32_t kidx;
        char kstr[4]; // used to pack short keys into the value directly
    } key;
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
        jkv_t* ptr;
        jkv_t buf[BUF_SIZE];
    } kvs;
};
typedef struct _jobj_t _jobj_t;

//------------------------------------------------------------------------------
struct _jarray_t
{
    jsize_t cap;
    jsize_t len;
    union
    {
        jval_t* ptr;
        jval_t buf[BUF_SIZE];
    } vals;
};
typedef struct _jarray_t _jarray_t;

#pragma mark - function prototypes

//------------------------------------------------------------------------------
JINLINE jval_t parse_val( json_t* jsn, jcontext_t* ctx );
JINLINE void _jobj_print(jprint_t* ctx, jobj_t obj, size_t depth);
JINLINE void _jarray_print(jprint_t* ctx, jarray_t array, size_t depth );

#pragma mark - memory

//------------------------------------------------------------------------------
JINLINE void* jcalloc( size_t s, size_t n )
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

//#define jmins(A,B) ({__typeof__(A) a = A; __typeof__(B) b = B; (a<b)?a:b; })
//#define jmaxs(A,B) ({__typeof__(A) a = A; __typeof__(B) b = B; (a>b)?a:b; })
//#define jsnprintf(BUF, BLEN, FMT, ...) { snprintf(BUF, BLEN, FMT, ## __VA_ARGS__); BUF[BLEN-1] = '\0'; }
//#define json_assert(A, STR, ...) { if (!(A)) {jsnprintf(ctx->err->msg, sizeof(ctx->err->msg), "" STR, ## __VA_ARGS__ ); json_do_err(ctx); } }

//------------------------------------------------------------------------------
JINLINE size_t jmins( size_t m1, size_t m2 ) { return (m1<m2) ? m1 : m2; }

//------------------------------------------------------------------------------
JINLINE size_t jmaxs( size_t m1, size_t m2 ) { return (m1>m2) ? m1 : m2; }

//------------------------------------------------------------------------------
JINLINE void FILE_get_path(FILE* file, char* buf, size_t blen )
{
    assert(buf && blen);

#if __LINUX__
    int fd = fileno(file);
    char fdpath[JMAX_SRC_STR];
    jsnprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
    readlink(fdpath, buf, blen);

#elif ( defined(__APPLE__) && defined(__MACH__) )

    int fd = fileno(file);
    char src[PATH_MAX];
    if (fcntl(fd, F_GETPATH, src) == 0)
    {
        strncpy(buf, src, blen);
    }
    else
    {
        strncpy(buf, "?", blen);
    }
#else
    *buf = '\0';
#endif
}

//------------------------------------------------------------------------------
JINLINE int jsnprintf( char* buf, size_t blen, const char* fmt, ... )
{
    assert(blen > 0);

    va_list args;
    va_start(args, fmt);
    int rt = vsnprintf(buf, blen, fmt, args);
    va_end(args);
    buf[blen-1] = '\0';
    return rt;
}

//------------------------------------------------------------------------------
#define json_assert(...) jcontext_assert(ctx, __VA_ARGS__)
#define json_passert(...) jcontext_passert(ctx, __VA_ARGS__)

//------------------------------------------------------------------------------
JINLINE void jcontext_fmt_msg(jcontext_t* ctx, const char* fmt, va_list args)
{
    assert(ctx);
    assert(fmt);
    assert(ctx->err);
    const size_t len = sizeof(ctx->err->msg);
    vsnprintf(ctx->err->msg, len, fmt, args);
    ctx->err->msg[len-1] = '\0';
}

//------------------------------------------------------------------------------
JINLINE void jcontext_assert(jcontext_t* ctx, jbool_t b, const char* fmt, ...)
{
    if (b) return;

    va_list args;
    va_start(args, fmt);
    jcontext_fmt_msg(ctx, fmt, args);
    va_end(args);

    json_do_err(ctx);
}

//------------------------------------------------------------------------------
JINLINE void jcontext_passert(jcontext_t* ctx, jbool_t b, const char* fmt, ...)
{
    if (b) return;

    va_list args;
    va_start(args, fmt);
    jcontext_fmt_msg(ctx, fmt, args);
    va_end(args);

    assert(ctx);
    assert(ctx->err);

    // error happened at previous location!
    if (ctx->err->line != ctx->err->pline)
    {
        ctx->err->col = ctx->err->pcol;
        ctx->err->line = ctx->err->pline;
    }
    json_do_err(ctx);
}

//------------------------------------------------------------------------------
JINLINE jbool_t is_prime( size_t x )
{
    size_t o = 4;
    for (size_t i = 5; JTRUE; i += o)
    {
        size_t q = x / i;
        if (q < i) return JTRUE;
        if (x == q * i) return JFALSE;
        o ^= 6;
    }
    return JTRUE;
}

//------------------------------------------------------------------------------
JINLINE size_t next_prime(size_t x)
{
    // find small prime numbers
    switch (x)
    {
        case 0:
        case 1:
        case 2: return 2;
        case 3: return 3;
        case 4:
        case 5: return 5;
        case 6:
        case 7: return 7;
        case 8:
        case 9:
        case 10:
        case 11: return 11;
        case 12:
        case 13: return 13;
        case 14:
        case 15:
        case 16:
        case 17: return 17;
        case 18:
        case 19: return 19;
        case 20:
        case 21:
        case 22:
        case 23: return 23;
        case 24:
        case 25:
        case 26:
        case 27:
        case 28:
        case 29: return 29;
        default:
        {
            // calculate the nearest prime number larger than our current num
            size_t k = x / 6;
            size_t i = x - 6 * k;
            size_t o = i < 2 ? 1 : 5;
            x = 6 * k + o;
            for (i = (3 + o) / 2; !is_prime(x); x += i)
            {
                i ^= 6;
            }
            return x;
        }
    }
}

//------------------------------------------------------------------------------
JINLINE int utf8_bytes( int ch )
{
    if (ch < 0x80)
    {
        return 1;
    }
    else if (ch < 0xC2) // overlong 2-byte sequence
    {
        return 0;
    }
    else if ( (ch & 0xE0) == 0xC0 ) // 2-byte sequence
    {
        return 2;
    }
    else if ( (ch & 0xF0) == 0xE0 ) // 3-byte sequence
    {
        return 3;
    }
    else if ( (ch & 0xF8) == 0xF0 ) // 4-byte sequence
    {
        return 4;
    }
    return 0;
}

//------------------------------------------------------------------------------
JINLINE const char* utf8_codepoint( const char* str, uint32_t* _codepoint )
{
    *_codepoint = 0; // initialize the codepoint

    int ch1 = *str++ & 0xFF;
    switch( utf8_bytes(ch1) )
    {
        case 0: // invalid
        {
            return NULL;
        }

        case 1: // 1 byte
        {
            *_codepoint = ch1;
            return str;
        }

        case 2: // 2 bytes
        {
            int ch2 = *str++ & 0xFF;
            if ((ch1 & 0xC0) != 0x80) return NULL;
            *_codepoint = (ch1 << 6) + ch2 - 0x3080;
            return str;
        }

        case 3: // 3 bytes
        {
            int ch2 = *str++ & 0xFF;
            int ch3 = *str++ & 0xFF;
            uint32_t cp = (ch1 << 12) + (ch2 << 6) + ch3 - 0xE2080;

            if ((ch2 & 0xC0) != 0x80) return NULL;
            if ((ch3 & 0xC0) != 0x80) return NULL;
            if (ch1 == 0xE0 && ch2 < 0xA0) return NULL; // overlong
            if (cp >= 0xD800 && cp <= 0xDFFF) return NULL;

            *_codepoint = cp;
            return str;
        }

        case 4: // 4 bytes
        {
            int ch2 = *str++ & 0xFF;
            int ch3 = *str++ & 0xFF;
            int ch4 = *str++ & 0xFF;
            uint32_t cp = (ch1 << 18) + (ch2 << 12) + (ch3 << 6) + ch4 - 0x3C82080;

            if ((ch2 & 0xC0) != 0x80) return NULL;
            if ((ch3 & 0xC0) != 0x80) return NULL;
            if ((ch4 & 0xC0) != 0x80) return NULL;
            if (ch1 == 0xF0 && ch2 < 0x90) return NULL; // overlong
            if (cp >= 0xD800 && cp <= 0xDFFF) return NULL; // surrogate pair
            if (cp > 0x10FFFF) return NULL; // > U+10FFFF

            *_codepoint = cp;
            return str;
        }

        default:
            return NULL;
    }
    return NULL;
}

//------------------------------------------------------------------------------
JINLINE size_t grow( size_t min, size_t cur )
{
    static const jnum_t GROWTH_FACTOR = 1.618; // (1+sqrt(5))/2 == Golden Ratio
    static const size_t MAX_GROWTH = 32*1024*1024;
    static const size_t MIN_ALLOC = 13;

    assert(min >= cur);
    size_t size = jmaxs(jmaxs(MIN_ALLOC, min), jmins( (size_t)(cur*GROWTH_FACTOR+2), cur+MAX_GROWTH));
    assert(size > cur);
    return size;
}

#pragma mark - jprint_t
//------------------------------------------------------------------------------
JINLINE void jprint_init( jprint_t* p, const char* tabs, const char* newline, const char* space )
{
    assert(p);
    strncpy(p->newline, newline, sizeof(p->newline)-1);
    strncpy(p->tab, tabs, sizeof(p->tab)-1);
    strncpy(p->space, space, sizeof(p->space)-1);

    p->nspace = strlen(space);
    p->nnewline = strlen(newline);
    p->ntab = strlen(tabs);
    p->nbytes = 0;
    p->udata = NULL;
    p->print = NULL;
    p->esc_uni = JTRUE;
}

//------------------------------------------------------------------------------
JINLINE void jprint_init_flags( jprint_t* ctx, int flags, print_func func, void* udata )
{
    static const char TAB[]         = "    ";
    static const char NEWLINE[]     = "\n";
    static const char NEWLINE_WIN[] = "\r\n";
    static const char SPACE[]       = " ";

    if (flags & JPRINT_PRETTY)
    {
        jprint_init(ctx, TAB, (flags&JPRINT_NEWLINE_WIN) ? NEWLINE_WIN : NEWLINE, SPACE);
    }
    else
    {
        jprint_init(ctx, "", "", "");
    }

    ctx->esc_uni = flags & JPRINT_ESC_UNI;
    ctx->udata = udata;
    ctx->print = func;
}

//------------------------------------------------------------------------------
JINLINE void jprint_write( jprint_t* ctx, const void* ptr, size_t n )
{
    if (n == 0) return;
    assert(ctx);
    assert(ctx->print);
    assert(ptr);
    size_t m = ctx->print(ctx->udata, ptr, n);
    if (m != n) json_do_err(ctx);
    ctx->nbytes += m;
}

//------------------------------------------------------------------------------
JINLINE void jprint_fmt(jprint_t* ctx, const char* fmt, ...)
{
    char buf[255];
    size_t blen = sizeof(buf);

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, blen, fmt, args);
    va_end(args);

    buf[blen-1] = '\0';
    assert(len <= blen);
    jprint_write(ctx, buf, len);
}

//------------------------------------------------------------------------------
JINLINE void jprint_tab(jprint_t* ctx)
{
    jprint_write(ctx, ctx->tab, ctx->ntab);
}

//------------------------------------------------------------------------------
JINLINE void jprint_space(jprint_t* ctx)
{
    jprint_write(ctx, ctx->space, ctx->nspace);
}

//------------------------------------------------------------------------------
JINLINE void jprint_char(jprint_t* ctx, char ch)
{
    jprint_write(ctx, &ch, 1);
}

#define jprint_const(CTX,STR) jprint_write(CTX, "" STR, sizeof(STR)-1)

//------------------------------------------------------------------------------
JINLINE void jprint_tabs(jprint_t* ctx, size_t ntabs )
{
    while (ntabs--)
    {
        jprint_tab(ctx);
    }
}

//------------------------------------------------------------------------------
JINLINE void jprint_newline(jprint_t* ctx)
{
    jprint_write(ctx, ctx->newline, ctx->nnewline);
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

	for (size_t i = 0; i < nblocks; i++)
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
    return murmur3_32(key, jmins(MAX_CHARS, len), MURMER32_SEED);
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
        jstr->str.chars = buf;
    }
    else
    {
        memcpy(jstr->str.buf, cstr, len * sizeof(char));
        jstr->str.buf[len] = '\0';
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
                jfree(str->str.chars);
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

//------------------------------------------------------------------------------
JINLINE void jmap_rehash(jmap_t* map, size_t hint)
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
    size_t _target = (size_t)ceilf(map->bcap / JMAP_IDEAL_LOADFACTOR);
    size_t target = next_prime(_target);

    size_t max = map->bcap;
    jmapbucket_t* buckets = map->buckets;

    map->bcap = jmaxs( jmaxs(hint, 13), target);
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
JINLINE jmem_t jmap_get_mem( jmap_t* map )
{
    jmem_t mem = {0,0};

    for ( size_t i = 0; i < map->slen; i++ )
    {
        jstr_t* str = &map->strs[i];
        if (str->len > BUF_SIZE)
        {
            mem.used += str->len;
            mem.reserved += str->len;
        }
    }
    mem.used += map->slen * sizeof(jstr_t);
    mem.reserved += map->scap * sizeof(jstr_t);

    for ( size_t n = 0; n < map->bcap; n++ )
    {
        jmapbucket_t* bucket = &map->buckets[n];
        mem.used += bucket->len * sizeof(*bucket->slots);
        mem.reserved += bucket->cap * sizeof(*bucket->slots);
    }

    mem.used += map->bcap * sizeof(jmapbucket_t);
    mem.reserved += map->bcap * sizeof(jmapbucket_t);

    return mem;
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
    if (map->blen == 0) return SIZE_MAX;

    assert(map->bcap > 0);
    jmapbucket_t* bucket = &map->buckets[hash % map->bcap];

    for ( size_t i = 0; i < bucket->len; ++i )
    {
        // find our string
        size_t idx = bucket->slots[i];
        jstr_t* str = &map->strs[idx];

        if (str->hash == hash && str->len == slen)
        {
            const char* chars = (str->len > BUF_SIZE) ? str->str.chars : str->str.buf;
            assert(chars);

            if (strncmp(chars, cstr, slen) == 0)
            {
                return idx;
            }
        }
    }

    return SIZE_MAX;
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
    if (idx != SIZE_MAX)
        return idx;

    jmap_rehash(map, 0);

    // did not find an existing entry, create a new one
    idx = _jmap_add_str(map, cstr, slen, hash);
    _jmap_add_key(map, hash, idx);
    return idx;
}

#pragma mark - jval_t

//------------------------------------------------------------------------------
JINLINE void json_print_str( jprint_t* ctx, const char* str )
{
    jprint_char(ctx, '\"');
    for ( int ch = *str&0xFF; ch; ch = *++str&0xFF )
    {
        switch (ch)
        {
            case '\\':
                jprint_const(ctx, "\\\\");
                break;

            case '"':
                jprint_const(ctx, "\\\"");
                break;

            case '\r':
                jprint_const(ctx, "\\r");
                break;

            case '\n':
                jprint_const(ctx, "\\n");
                break;

            case '\f':
                jprint_const(ctx, "\\f");
                break;

            case '\t':
                jprint_const(ctx, "\\t");
                break;

            case '/':
            default:
            {
                if (!ctx->esc_uni)
                {
                    jprint_char(ctx, (char)ch);
                    break;
                }

                uint32_t codepoint;
                const char* next = utf8_codepoint(str, &codepoint);
                assert(next);

                if (next != str)
                {
                    jprint_fmt(ctx, "\\u%X", codepoint);
                    str = next;
                }
                else
                {
                    jprint_char(ctx, (char)ch);
                }
                break;
            }
        }
    }

    jprint_char(ctx, '\"');
}

//------------------------------------------------------------------------------
void _jval_print( jprint_t* ctx, struct json_t* jsn, jval_t val, size_t depth )
{
    switch (jval_type(val))
    {
        case JTYPE_NIL:
            jprint_const(ctx, "null");
            break;

        case JTYPE_STR:
            json_print_str(ctx, json_get_str(jsn, val));
            break;

        case JTYPE_INT:
            jprint_fmt(ctx, "%lld", json_get_int(jsn, val));
            break;

        case JTYPE_NUM:
            jprint_fmt(ctx, "%g", json_get_num(jsn, val));
            break;

        case JTYPE_ARRAY:
            _jarray_print(ctx, json_get_array(jsn, val), depth);
            break;

        case JTYPE_OBJ:
            _jobj_print(ctx, json_get_obj(jsn, val), depth);
            break;

        case JTYPE_TRUE:
            jprint_const(ctx, "true");
            break;

        case JTYPE_FALSE:
            jprint_const(ctx, "false");
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

    if (jval_is_nil(jsn->root))
    {
        jsn->root = (jval_t){JTYPE_OBJ, (uint32_t)idx};
    }
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

    if (jval_is_nil(jsn->root))
    {
        jsn->root = (jval_t){JTYPE_ARRAY, (uint32_t)idx};
    }
    return idx;
}

//------------------------------------------------------------------------------
JINLINE void json_ints_reserve( json_t* jsn, size_t len )
{
    assert(jsn);
    if (jsn->ints.len+len <= jsn->ints.cap)
        return;

    jsn->ints.cap = grow(jsn->ints.len+len, jsn->ints.cap);
    jsn->ints.ptr = (jint_t*)jrealloc(jsn->ints.ptr, jsn->ints.cap * sizeof(jint_t));
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
JINLINE size_t json_add_int( json_t* jsn, jint_t n )
{
    assert(jsn);
    json_ints_reserve(jsn, 1);
    size_t idx = jsn->ints.len++;
    jsn->ints.ptr[idx] = n;
    return idx;
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
    assert (idx != SIZE_MAX);
    return idx;
}
//------------------------------------------------------------------------------
const char* json_get_str( json_t* jsn, jval_t val )
{
    if (!jval_is_str(val)) return NULL;

    jstr_t* jstr = jmap_get_str(&jsn->strmap, val.idx);
    if (!jstr) return NULL;

    return (jstr->len > BUF_SIZE) ? jstr->str.chars : jstr->str.buf;
}

//------------------------------------------------------------------------------
jint_t json_get_int( json_t* jsn, jval_t val )
{
    switch (jval_type(val))
    {
        case JTYPE_NUM:
            return (jint_t)jsn->nums.ptr[val.idx];

        case JTYPE_INT:
            return jsn->ints.ptr[val.idx];

        default:
            return 0;
    }
}

//------------------------------------------------------------------------------
jnum_t json_get_num( json_t* jsn, jval_t val )
{
    switch (jval_type(val))
    {
        case JTYPE_NUM:
            return jsn->nums.ptr[val.idx];

        case JTYPE_INT:
            return (jnum_t)jsn->ints.ptr[val.idx];

        default:
            return 0;
    }
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
    if (!jval_is_obj(val)) return JNULL_OBJ;
    assert( _json_get_obj(jsn, val.idx) );
    return (jobj_t){.json=jsn, .idx=val.idx};
}

//------------------------------------------------------------------------------
jarray_t json_get_array( json_t* jsn, jval_t val )
{
    if (!jval_is_array(val)) return JNULL_ARRAY;
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

    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs.ptr : _obj->kvs.buf;

    *val = kvs[idx].val;
    if (kvs[idx].val.type & ~JTYPE_MASK)
    {
        return kvs[idx].key.kstr;
    }

    jstr_t* jstr = jmap_get_str(&jsn->strmap, kvs[idx].key.kidx);
    assert(jstr);
    return (jstr->len > BUF_SIZE) ? jstr->str.chars : jstr->str.buf;
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
        jkv_t* kvs = obj->kvs.ptr;
        memcpy(obj->kvs.buf, kvs, sizeof(jkv_t)*obj->len);
        jfree(kvs);
        obj->cap = obj->len;
        return;
    }

    obj->kvs.ptr = (jkv_t*)jrealloc(obj->kvs.ptr, obj->len * sizeof(jkv_t));
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
        memcpy(kvs, obj->kvs.buf, sizeof(jkv_t) * obj->len);
        obj->kvs.ptr = kvs;
    }
    else
    {
        assert(obj->kvs.ptr);
        obj->kvs.ptr = (jkv_t*)jrealloc(obj->kvs.ptr, sizeof(jkv_t)*obj->cap);
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
    return (obj->cap > BUF_SIZE) ? &obj->kvs.ptr[idx] : &obj->kvs.buf[idx];
}

//------------------------------------------------------------------------------
JINLINE jval_t* _jobj_get_val(_jobj_t* obj, size_t idx)
{
    return (idx < obj->len) ? &_jobj_get_kv(obj, idx)->val : NULL;
}

//------------------------------------------------------------------------------
#define jobj_add_key(OBJ, KEY) jobj_add_keyl(OBJ, KEY, strlen(KEY))

//------------------------------------------------------------------------------
JINLINE size_t jobj_add_keyl( jobj_t o, const char* key, size_t klen )
{
    json_t* jsn = jobj_get_json(o);
    _jobj_t* obj = jobj_get_obj(o);

    assert(obj);
    assert(key);

    size_t idx = obj->len++;

    _jobj_reserve(obj, 1);
    jkv_t* kv = _jobj_get_kv(obj, idx);
    kv->val.type = JTYPE_NIL;
    kv->val.idx = 0;

    if (klen < sizeof(kv->key.kstr) )
    {
        kv->key.kidx = 0;
        strncpy(kv->key.kstr, key, sizeof(kv->key.kstr));
        kv->val.type |= ~JTYPE_MASK;
    }
    else
    {
        size_t kidx = json_add_strl(jsn, key, klen);
        assert (kidx < MAX_KEY_IDX);
        kv->key.kidx = (uint32_t)kidx;
    }
    return idx;
}

//------------------------------------------------------------------------------
#define jobj_add_kval(OBJ,KEY,VAL) jkv_set_val(OBJ, jobj_add_key(OBJ, KEY), VAL)

//------------------------------------------------------------------------------
JINLINE void jkv_set_val(jobj_t o, size_t idx, jval_t val )
{
    _jobj_t* obj = jobj_get_obj(o);
    jkv_t* kv = _jobj_get_kv(obj, idx);
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
void jobj_add_int( jobj_t obj, const char* key, jint_t num )
{
    assert(key);
    size_t idx = json_add_int(jobj_get_json(obj), num);
    jobj_add_kv(obj, key, JTYPE_INT, idx);
}

//------------------------------------------------------------------------------
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen )
{
    assert(key);
    assert(str);

    // TODO: validate the string as a valid UTF8 sequence!

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
jval_t jobj_get_val(jobj_t obj, size_t idx)
{
    jval_t* val = _jobj_get_val(jobj_get_obj(obj), idx);
    return val ? *val : JNULL_VAL;
}

//------------------------------------------------------------------------------
JINLINE size_t jobj_find_shortstr( jobj_t obj, const char* key, size_t klen )
{
    assert (klen < 4);

    _jobj_t* _obj = jobj_get_obj(obj);
    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs.ptr : _obj->kvs.buf;
    for ( size_t i = 0; i < _obj->len; i++ )
    {
        jkv_t* kv = &kvs[i];
        if ( (kv->val.type & ~JTYPE_MASK) == 0 )
            continue;

        if (strcmp(kv->key.kstr, key) == 0)
        {
            // found it!
            return i;
        }
    }
    return SIZE_MAX;
}

//------------------------------------------------------------------------------
size_t jobj_findl_next_idx( jobj_t obj, size_t next, const char* key, size_t klen )
{
    // check for short strings, these do not go into the hash table and must be
    // searched manually
    if (klen < 4) return jobj_find_shortstr(obj, key, klen);

    // not a short string! Proceed with search.
    json_t* jsn = jobj_get_json(obj);
    _jobj_t* _obj = jobj_get_obj(obj);

    // check the hashtable for our string, if it's not there it's no where!
    size_t idx = jmap_find_str(&jsn->strmap, key, klen);
    if (idx == SIZE_MAX)
    {
        return idx;
    }

    // we now know the correct index for our key, search the object to find a
    // matching index.
    jkv_t* kvs = (_obj->cap > BUF_SIZE) ? _obj->kvs.ptr : _obj->kvs.buf;
    for ( size_t i = next; i < _obj->len; i++ )
    {
        jkv_t* kv = &kvs[i];

        // can't be a short string (we already checked), if this one is skip it.
        if ( (kv->val.type & ~JTYPE_MASK) > 0 )
            continue;

        // found a match!!!
        if (kv->key.kidx == idx)
        {
            return i;
        }
    }

    return SIZE_MAX;
}

//------------------------------------------------------------------------------
JINLINE void _jobj_print(jprint_t* ctx, jobj_t obj, size_t depth)
{
    jprint_char(ctx, '{');
    jprint_newline(ctx);

    json_t* jsn = jobj_get_json(obj);
    size_t len = jobj_len(obj);
    for ( size_t i = 0; i < len; i++ )
    {
        jval_t val;
        const char* key = jobj_get(obj, i, &val);

        jprint_tabs(ctx, depth+1);
        json_print_str(ctx, key);
        jprint_char(ctx, ':');
        jprint_space(ctx);
        _jval_print(ctx, jsn, val, depth+1);
        if (i+1 != len) jprint_char(ctx, ',');
        jprint_newline(ctx);
    }

    jprint_tabs(ctx, depth);
    jprint_char(ctx, '}');
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
        jval_t* vals = array->vals.ptr;
        memcpy(array->vals.buf, vals, sizeof(jval_t)*array->len);
        jfree(vals);
        array->cap = array->len;
        return;
    }

    array->vals.ptr = (jval_t*)jrealloc(array->vals.ptr, array->len * sizeof(jval_t));
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
        memcpy(vals, a->vals.buf, a->len * sizeof(jval_t));
        a->vals.ptr = vals;
    }
    else
    {
        assert(a->vals.ptr);
        a->vals.ptr = (jval_t*)jrealloc( a->vals.ptr, a->cap * sizeof(jval_t) );
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
    return (a->cap > BUF_SIZE) ? &a->vals.ptr[idx] : &a->vals.buf[idx];
}

//------------------------------------------------------------------------------
JINLINE jval_t* _jarray_add_val( _jarray_t* a)
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
void jarray_add_int( jarray_t _a, jint_t num )
{
    size_t idx = json_add_int(_a.json, num);

    _jarray_t* a = _jarray_get_array(_a);
    jval_t* val = _jarray_add_val(a);
    val->type = JTYPE_INT;

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
JINLINE void _jarray_print( jprint_t* ctx, jarray_t array, size_t depth )
{
    jprint_char(ctx, '[');
    jprint_newline(ctx);
    json_t* jsn = jarray_get_json(array);
    size_t len = jarray_len(array);
    for ( size_t i = 0; i < len; i++ )
    {
        jprint_tabs(ctx, depth+1);
        _jval_print(ctx, jsn, jarray_get(array, i), depth+1);
        if (i+1 != len) jprint_char(ctx, ',');
        jprint_newline(ctx);
    }
    jprint_tabs(ctx, depth);
    jprint_char(ctx, ']');
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
JINLINE size_t jbuf_write( jbuf_t* buf, const void* ptr, size_t n )
{
    jbuf_reserve(buf, n);
    memcpy(buf->ptr+buf->len, ptr, n);
    buf->len += n;
    return n;
}

//------------------------------------------------------------------------------
JINLINE void jbuf_add( jbuf_t* buf, char ch )
{
    assert(buf);
    jbuf_reserve(buf, 1);
    buf->ptr[buf->len++] = ch;
}

//------------------------------------------------------------------------------
JINLINE int jbuf_add_unicode(jbuf_t* str, int32_t codepoint )
{
    if (codepoint == 0)
    {
        return EXIT_FAILURE;
    }
    else if(codepoint < 0x80)
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
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#pragma mark - json_t

//------------------------------------------------------------------------------
json_t* json_init( json_t* jsn )
{
    if (!jsn) return NULL;

    jmap_init(&jsn->strmap);

    // nums
    jsn->nums.cap = 0;
    jsn->nums.len = 0;
    jsn->nums.ptr = NULL;

    // ints
    jsn->ints.cap = 0;
    jsn->ints.len = 0;
    jsn->ints.ptr = NULL;

    // arrays
    jsn->arrays.cap = 0;
    jsn->arrays.len = 0;
    jsn->arrays.ptr = NULL;

    // objs
    jsn->objs.cap = 0;
    jsn->objs.len = 0;
    jsn->objs.ptr = NULL;

    jsn->root = (jval_t){JTYPE_NIL, 0};

    return jsn;
}

//------------------------------------------------------------------------------
void json_clear( json_t* jsn )
{
    json_destroy(jsn);
    json_init(jsn);
}

//------------------------------------------------------------------------------
JINLINE size_t _json_write_file(void* file, const void* ptr, size_t n)
{
#if J_USE_POSIX
    const char* cptr = (const char*)ptr;
    for ( size_t i = 0; i < n; i++ )
    {
        int ch = cptr[i];
        if (putc_unlocked(ch, (FILE*)file) == EOF)
        {
            return i;
        }
    }
    return n;
#else
    return n * fwrite(ptr, n, 1, (FILE*)file);
#endif
}

//------------------------------------------------------------------------------
JINLINE size_t _json_write_buf(void* buf, const void* ptr, size_t n)
{
    return jbuf_write((jbuf_t*)buf, ptr, n);
}

//------------------------------------------------------------------------------
void jval_print(json_t* jsn, jval_t val, int flags, print_func p, void* udata)
{
    assert(jsn);
    jprint_t ctx;
    jprint_init_flags(&ctx, flags, p, udata);

    _jval_print(&ctx, jsn, val, 0);
}

//------------------------------------------------------------------------------
void jarray_print(jarray_t array, int flags, print_func p, void* udata)
{
    jprint_t ctx;
    jprint_init_flags(&ctx, flags, p, udata);

    _jarray_print(&ctx, array, 0);
}

//------------------------------------------------------------------------------
void jobj_print(jobj_t obj, int flags, print_func p, void* udata)
{
    jprint_t ctx;
    jprint_init_flags(&ctx, flags, p, udata);

    _jobj_print(&ctx, obj, 0);
}

//------------------------------------------------------------------------------
size_t json_print(json_t* jsn, int flags, print_func p, void* udata)
{
    assert(jsn);
    jprint_t ctx;
    jprint_init_flags(&ctx, flags, p, udata);

    if (setjmp(ctx.jerr_jmp) == 0)
    {
        // special case if we don't have any root
        jval_t root = json_root(jsn);
        if (!jval_is_nil(root))
        {
            _jval_print(&ctx, jsn, root, 0);
        }
        else
        {
            jprint_char(&ctx, '{');
            jprint_newline(&ctx);
            jprint_char(&ctx, '}');
        }
        return ctx.nbytes;
    }
    else
    {
        return 0;
    }
}

//------------------------------------------------------------------------------
char* json_to_str(json_t* jsn, int flags)
{
    assert(jsn);

    jbuf_t buf;
    jbuf_init(&buf);

    if (json_print(jsn, flags, _json_write_buf, &buf) == 0)
    {
        jbuf_destroy(&buf);
        return NULL;
    }
    return buf.ptr;
}

//------------------------------------------------------------------------------
size_t json_print_path(json_t* jsn, int flags, const char* path)
{
    assert(jsn);
    assert(path);
    FILE* file = fopen(path, "w");
    if (!file)
    {
        // TODO: error
        return 0;
    }
    size_t rt = json_print_file(jsn, flags, file);
    fclose(file);
    return rt;
}

//------------------------------------------------------------------------------
size_t json_print_file(json_t* jsn, int flags, FILE* f)
{
    assert(jsn);
    assert(f);

    size_t rt;
#if J_USE_POSIX
    flockfile(f);
    rt = json_print(jsn, flags, _json_write_file, f);
    funlockfile(f);
#else
    rt = json_print(jsn, flags, _json_write_file, f);
#endif

    fflush(f);
    return rt;
}

//------------------------------------------------------------------------------
void json_destroy(json_t* jsn)
{
    assert(jsn);

    // cleanup string map
    jmap_destroy(&jsn->strmap);

    // cleanup numbers
    jfree(jsn->nums.ptr); jsn->nums.ptr = NULL;

    // cleanup integers
    jfree(jsn->ints.ptr); jsn->ints.ptr = NULL;

    // cleanup objects
    for ( size_t i = 0; i < jsn->objs.len; i++ )
    {
        _jobj_t* obj = _json_get_obj(jsn, i);
        if (obj->cap > BUF_SIZE)
        {
            jfree(obj->kvs.ptr); obj->kvs.ptr = NULL;
        }
    }
    jfree(jsn->objs.ptr); jsn->objs.ptr = NULL;
    jsn->objs.len = jsn->objs.cap = 0;

    // cleanup arrays
    for ( size_t n = 0; n < jsn->arrays.len; n++ )
    {
        _jarray_t* array = _json_get_array(jsn, n);
        if (array->cap > BUF_SIZE)
        {
            jfree(array->vals.ptr); array->vals.ptr = NULL;
        }
    }
    jfree(jsn->arrays.ptr); jsn->arrays.ptr = NULL;
    jsn->arrays.len = jsn->arrays.cap = 0;
}

//------------------------------------------------------------------------------
void json_free(json_t* jsn)
{
    json_destroy(jsn);
    jfree(jsn);
}

//------------------------------------------------------------------------------
jobj_t json_root_obj( json_t* jsn )
{
    assert(jsn);

    // no root yet, let's set it now
    switch(jval_type(jsn->root))
    {
        case JTYPE_NIL:
            json_add_obj(jsn);
            break;

        case JTYPE_OBJ:
            break;

        default:
            assert(JFALSE);
            break;
    }

    assert(jsn->objs.len > 0);
    return (jobj_t){jsn, jsn->root.idx};
}

//------------------------------------------------------------------------------
jarray_t json_root_array( json_t* jsn )
{
    assert(jsn);

    // no root yet, let's set it now
    switch(jval_type(jsn->root))
    {
        case JTYPE_NIL:
            json_add_array(jsn);
            break;

        case JTYPE_ARRAY:
            break;

        default:
            assert(JFALSE);
            break;
    }

    assert(jsn->arrays.len > 0);
    return (jarray_t){jsn, jsn->root.idx};
}

#pragma mark - jcontext_t

//------------------------------------------------------------------------------
JINLINE void jcontext_init(jcontext_t* ctx)
{
    assert(ctx);
    ctx->beg = NULL;
    ctx->end = NULL;
    ctx->file = NULL;
    ctx->err = NULL;
    *ctx->buf = '\0';
    jbuf_init(&ctx->strbuf);
}

//------------------------------------------------------------------------------
JINLINE void jcontext_init_buf(jcontext_t* ctx, const void* buf, size_t len)
{
    assert(buf);
    jcontext_init(ctx);
    ctx->beg = (const char*)buf;
    ctx->end = ctx->beg + len;
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
JINLINE void jcontext_read_file( jcontext_t* ctx )
{
    if (ctx->beg != ctx->end) return;

    // read file into buffer
    size_t len = fread(ctx->buf, 1, IO_BUF_SIZE, ctx->file);
    if (len == 0 && feof(ctx->file) != 0)
    {
        json_assert(ferror(ctx->file) == 0, "error reading file contents: '%s'", strerror(errno));
    }
    ctx->beg = ctx->buf;
    ctx->end = ctx->beg + len;
}

//------------------------------------------------------------------------------
JINLINE int jcontext_peek( jcontext_t* ctx )
{
    if (ctx->beg == ctx->end)
    {
        return EOF;
    }
    return *ctx->beg & 0xFF;
}

//------------------------------------------------------------------------------
JINLINE int jcontext_next( jcontext_t* ctx )
{
    ++ctx->err->col;
    ++ctx->err->off;
    ++ctx->beg;
    if (ctx->file)
    {
        jcontext_read_file(ctx);
    }
    return jcontext_peek(ctx);
}

//------------------------------------------------------------------------------
JINLINE uint32_t jcontext_read_utf8( jcontext_t* ctx )
{
    int ch1 = jcontext_peek(ctx);
    switch( utf8_bytes(ch1) )
    {
        case 0: // invalid
        {
            return 0;
        }

        case 1: // 1 byte
        {
            return ch1;
        }

        case 2: // 2 bytes
        {
            int ch2 = jcontext_next(ctx);
            if ((ch2 & 0xC0) != 0x80) return 0;
            return (ch1 << 6) + ch2 - 0x3080;
        }

        case 3: // 3 bytes
        {
            int ch2 = jcontext_next(ctx);
            int ch3 = jcontext_next(ctx);
            uint32_t cp = (ch1 << 12) + (ch2 << 6) + ch3 - 0xE2080;

            if ((ch2 & 0xC0) != 0x80) return 0;
            if ((ch3 & 0xC0) != 0x80) return 0;
            if (ch1 == 0xE0 && ch2 < 0xA0) return 0; // overlong
            if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
            return cp;
        }

        case 4: // 4 bytes
        {
            int ch2 = jcontext_next(ctx);
            int ch3 = jcontext_next(ctx);
            int ch4 = jcontext_next(ctx);
            uint32_t cp = (ch1 << 18) + (ch2 << 12) + (ch3 << 6) + ch4 - 0x3C82080;

            if ((ch2 & 0xC0) != 0x80) return 0;
            if ((ch3 & 0xC0) != 0x80) return 0;
            if ((ch4 & 0xC0) != 0x80) return 0;
            if (ch1 == 0xF0 && ch2 < 0x90) return 0; // overlong
            if (cp >= 0xD800 && cp <= 0xDFFF) return 0; // surrogate pair
            if (cp > 0x10FFFF) return 0; // > U+10FFFF
            return cp;
        }

        default:
            return 0;
    }

    return 0;
}

//------------------------------------------------------------------------------
JINLINE void parse_whitespace( jcontext_t* ctx )
{
    ctx->err->pline = ctx->err->line;
    ctx->err->pcol = ctx->err->col;
    for ( int ch = jcontext_peek(ctx); ch >= 0; ch = jcontext_next(ctx) )
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
                ctx->err->col = 0;
                ctx->err->line++;
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
JINLINE jnum_t parse_sign( jcontext_t* ctx )
{
    int ch = jcontext_peek(ctx);
    switch (ch)
    {
        case '-':
            jcontext_next(ctx);
            return -1;

        case '+':
            jcontext_next(ctx);
            return 1;

        default:
            return 1;
    }
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_digitsp( jcontext_t* ctx, size_t* places)
{
    jnum_t num = 0;
    for ( int ch = jcontext_peek(ctx), cnt = 0; ch >= 0; ch = jcontext_next(ctx), ++cnt )
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
                *places = (size_t)pow(10.0, (double)cnt);
                return num;
            }
        }
    }

    json_assert(JFALSE, "unexpected end of file");
    return num;
}

//------------------------------------------------------------------------------
JINLINE jnum_t parse_num( jcontext_t* ctx, jint_t* _int )
{
    jbool_t isint = JTRUE;

    // +/-
    jnum_t sign = parse_sign(ctx);

    // whole number
    size_t p = 0;
    jnum_t num = parse_digitsp(ctx, &p);
    json_assert( (num == 0 && p == 10) || (num*10 >= p), "leading zero on integer value");

    // fraction
    switch (jcontext_peek(ctx))
    {
        case '.':
        {
            isint = JFALSE;
            jcontext_next(ctx);

            size_t places = 0;
            jnum_t fract = parse_digitsp(ctx, &places);
            json_assert(places > 1, "number truncated after '.'");

            num += fract / places;
            break;
        }

        default:
            break;
    }

    // scientific notation
    switch (jcontext_peek(ctx))
    {
        case 'e':
        case 'E':
        {
            isint = JFALSE;
            jcontext_next(ctx);
            jnum_t esign = parse_sign(ctx);

            size_t places = 0;
            jnum_t digits = parse_digitsp(ctx, &places);
            json_assert(places > 1, "number truncated after 'e'");

            num *= pow(10, esign*digits);
            break;
        }

        default:
            break;
    }

    jnum_t d = sign * num;
    json_assert(!isnan(d) && !isinf(d), "numeric overflow");

    if (isint)
    {
        json_assert(d<=LLONG_MAX && d>=LLONG_MIN, "integer overflow");
        *_int = (jint_t)d;
    }
    return d;
}

//------------------------------------------------------------------------------
JINLINE unsigned int parse_unicode_hex(jcontext_t* ctx)
{
    return char_to_hex(ctx, jcontext_next(ctx)) << 12 |
           char_to_hex(ctx, jcontext_next(ctx)) << 8 |
           char_to_hex(ctx, jcontext_next(ctx)) << 4 |
           char_to_hex(ctx, jcontext_next(ctx));
}

//------------------------------------------------------------------------------
JINLINE void parse_unicode2( jbuf_t* str, jcontext_t* ctx )
{
    json_assert(jcontext_peek(ctx) == 'u', "not a valid unicode sequence");

    // UTF-8 escape sequence
    // format is: \uXXXX
    unsigned int val = parse_unicode_hex(ctx);

    // not a surrogate pair, process it as a single sequence
    if (val < 0xD800 || val > 0xDBFF)
    {
        json_assert(0xDC00 > val || val > 0xDFFF, "invalid utf8 codepoint");
        json_assert(jbuf_add_unicode(str, val) == 0, "invalid utf8 codepoint: 0x%X", val);
        return;
    }

    // Support for UTF-16 style surrogate pairs!!!
    // This is technically not UTF-8 but many other json libs seem to support it.
    // Format is: \uXXXX\uXXXXX

    // Check the neighbor to make sure it's a \u sequence
    json_assert(jcontext_next(ctx) == '\\', "invalid unicode");
    json_assert(jcontext_next(ctx) == 'u', "invalid unicode");

    // read the surrogate pair from the stream
    unsigned int val2 = parse_unicode_hex(ctx);

    // validate the value
    json_assert(val2 >= 0xDC00 && val2 <= 0xDFFF, "invalid utf8 codepoint: 0x%X", val2);
    unsigned int unicode = ((val - 0xD800) << 10) + (val2 - 0xDC00) + 0x10000;
    json_assert(jbuf_add_unicode(str, unicode) == 0, "invalid utf8 codepoint: 0x%X", unicode);

}

//------------------------------------------------------------------------------
JINLINE void parse_str(jbuf_t* str, jcontext_t* ctx)
{
    int prev = jcontext_peek(ctx);
    json_assert(prev == '"', "Expected a String, found: '%c'", prev);

    jbuf_clear(str);

    for (int ch = jcontext_next(ctx); ch >= 0; ch = jcontext_next(ctx) )
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
                    case '\0':
                        jbuf_add(str, (char)ch);
                        json_assert(JFALSE, "NUL character in string: '%s\\0'", str->ptr);
                        break;

                    // control characters not allowed
                    case '\f':
                    case '\b':
                    case '\n':
                    case '\r':
                    case '\t':
                    case '/':
                        jbuf_add(str, '\0');
                        json_assert(JFALSE, "control character 0x%X found in string: '%s'", ch, str->ptr);
                        break;

                    case '\\':
                        break;

                    case '"':
                        jcontext_next(ctx);
                        jbuf_add(str, '\0');
                        return;

                    default:
                    {
                        if (ch < 0x80)
                        {
                            jbuf_add(str, (char)ch);
                        }
                        else
                        {
                            uint32_t cp = jcontext_read_utf8(ctx);
                            json_assert(cp != 0, "invalid utf8 codepoint");
                            jbuf_add_unicode(str, cp);
                        }
                        break;
                    }
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
    int prev = jcontext_peek(ctx); jcontext_next(ctx);
    json_assert(prev == '[', "Expected an array, found: '%c'", prev);
    json_t* jsn = array.json;

    size_t count = jarray_len(array);
    while ( JTRUE )
    {
        size_t len = jarray_len(array);

        parse_whitespace(ctx);
        switch(jcontext_peek(ctx))
        {
            case ',':
            {
                json_passert(len == ++count, "expected value after ','");
                jcontext_next(ctx);
                break;
            }

            case ']':
            {
                json_passert( len == 0 || (len-count) == 1, "trailing ',' not allowed");
                jcontext_next(ctx);
                jarray_truncate(array);
                return;
            }

            default:
            {
                json_passert(len == count, "missing ',' separator");
                jval_t val = parse_val(jsn, ctx);
                *_jarray_add_val(_jarray_get_array(array)) = val;
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------
JINLINE void parse_obj(jobj_t obj, jcontext_t* ctx)
{
    int prev = jcontext_peek(ctx); jcontext_next(ctx);
    json_assert(prev == '{', "Expected an object, found: '%c'", prev);
    json_t* jsn = jobj_get_json(obj);

    size_t count = jobj_len(obj);
    while ( JTRUE )
    {
        size_t len = jobj_len(obj);

        parse_whitespace(ctx);
        switch (jcontext_peek(ctx))
        {
            case ',':
            {
                json_passert(len == ++count, "expected key/value after ','");
                jcontext_next(ctx);
                break;
            }

            case '}':
            {
                json_passert( len == 0 || (len-count) == 1, "trailing ',' not allowed");
                jcontext_next(ctx);
                jobj_truncate(obj);
                return;
            }

            default:
            {
                json_passert(len == count, "missing ',' separator");

                // parse key
                parse_str(&ctx->strbuf, ctx);
                const char* key = ctx->strbuf.ptr;
                size_t kvidx = jobj_add_keyl(obj, key, ctx->strbuf.len);

                parse_whitespace(ctx);

                // parse separator
                int ch = jcontext_peek(ctx);
                json_passert(ch == ':', "expected separator ':' after key \"%s\", found '%c' instead.", key, ch);
                jcontext_next(ctx);

                parse_whitespace(ctx);

                // parse the value
                jkv_set_val(obj, kvidx, parse_val(jsn, ctx));
                break;
            }
        }
    }
}

//------------------------------------------------------------------------------
JINLINE jval_t parse_val( json_t* jsn, jcontext_t* ctx )
{
    int ch = jcontext_peek(ctx);
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
            json_assert( jcontext_next(ctx) == 'r', "expected literal 'true'");
            json_assert( jcontext_next(ctx) == 'u', "expected literal 'true'");
            json_assert( jcontext_next(ctx) == 'e', "expected literal 'true'"); jcontext_next(ctx);
            return (jval_t){JTYPE_TRUE, 0};

        case 'f': // false
            json_assert( jcontext_next(ctx) == 'a', "expected literal 'false'");
            json_assert( jcontext_next(ctx) == 'l', "expected literal 'false'");
            json_assert( jcontext_next(ctx) == 's', "expected literal 'false'");
            json_assert( jcontext_next(ctx) == 'e', "expected literal 'false'"); jcontext_next(ctx);
            return (jval_t){JTYPE_FALSE, 0};

        case 'n': // null
            json_assert( jcontext_next(ctx) == 'u', "expected literal 'null'");
            json_assert( jcontext_next(ctx) == 'l', "expected literal 'null'");
            json_assert( jcontext_next(ctx) == 'l', "expected literal 'null'"); jcontext_next(ctx);
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
            jint_t intval;
            jnum_t num = parse_num(ctx, &intval);

            // TODO support for integer types
            if (num == intval)
            {
                json_assert(num<=LLONG_MAX && num>=LLONG_MIN, "integer overflow");
                return (jval_t){JTYPE_INT, (uint32_t)json_add_int(jsn, intval)};
            }
            else
            {
                return (jval_t){JTYPE_NUM, (uint32_t)json_add_num(jsn, num)};
            }
        }

        default:
            json_assert(JFALSE, "invalid value: expectected: object, array, number, string, true, false, or null.");
    }

    return (jval_t){JTYPE_NIL, 0};
}


#pragma mark - io

//------------------------------------------------------------------------------
void jerr_init( jerr_t* err )
{
    assert(err);
    err->col = 0;
    err->pcol = 0;
    err->pline = 0;
    err->off = 0;
    err->line = 0;
    err->src[0] = '\0';
    err->msg[0] = '\0';
}

//------------------------------------------------------------------------------
void jerr_init_src( jerr_t* err, const char* src )
{
    jerr_init(err);

    if (!src)
    {
        *err->src = '\0';
        return;
    }

    strncpy(err->src, src, sizeof(err->src));
    err->src[sizeof(err->src)-1] = '\0';
}

//------------------------------------------------------------------------------
void jerr_set_msg( jerr_t* err, const char* msg )
{
    assert(err);
    assert(msg);

    const size_t len = sizeof(err->msg);
    strncpy(err->msg, msg, len);
    err->msg[len-1] = '\0';
}

//------------------------------------------------------------------------------
int json_parse( json_t* jsn, jcontext_t* ctx )
{
    assert(jsn);
    assert(ctx);

    // clear out the json doc before loading again
    if ( jsn->arrays.len > 0 || jsn->objs.len > 0 )
    {
        json_clear(jsn);
    }

    if (ctx->beg == ctx->end)
    {
        jerr_set_msg(ctx->err, "json document is empty");
        return EXIT_FAILURE;
    }

    if (setjmp(ctx->jerr_jmp) == 0)
    {
        switch(jcontext_peek(ctx))
        {
            case '{':
                parse_val(jsn, ctx);
                break;

            case '[':
                parse_val(jsn, ctx);
                break;

            default:
                json_assert(JFALSE, "json must start with an object or array");
                break;
        }

        ctx->buf[0] = '\0';

        if (jcontext_peek(ctx) != EOF)
        {
            parse_whitespace(ctx);
            int ch = jcontext_peek(ctx);
            json_assert(ch == EOF, "unexpected character '%c' trailing json", ch);
        }

        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}

//------------------------------------------------------------------------------
JINLINE int _json_load_file(json_t* jsn, const char* src, FILE* file, jerr_t* err)
{
    assert(jsn);
    assert(err);
    if (!file)
    {
        jerr_init_src(err, src);
        jerr_set_msg(err, "file descriptor is null");
        return 1;
    }

    jcontext_t ctx;
    jcontext_init_file(&ctx, file);
    jcontext_read_file(&ctx);

    jerr_init_src(err, src);
    ctx.err = err;

    int status = json_parse(jsn, &ctx);
    if (status != 0)
    {
        json_destroy(jsn); jsn = NULL;
    }

    jcontext_destroy(&ctx);
    return status;
}

//------------------------------------------------------------------------------
JINLINE int _json_load_buf(json_t* jsn, const char* src, const void* buf, size_t blen, jerr_t* err)
{
    assert(jsn);
    assert(buf);

    jcontext_t ctx;
    jcontext_init_buf(&ctx, buf, blen);

    jerr_init_src(err, src);
    ctx.err = err;

    // pre-allocate data based on estimate size
    size_t est = grow( (size_t)ceilf(blen*0.01f), 0);

    jmap_rehash(&jsn->strmap, est);
    json_nums_reserve(jsn, est);
    json_ints_reserve(jsn, est);
    json_arrays_reserve(jsn, est);
    json_objs_reserve(jsn, est);

    int status = json_parse(jsn, &ctx);
    if (status != 0)
    {
        json_clear(jsn);
    }

    jcontext_destroy(&ctx);
    return status;
}

//------------------------------------------------------------------------------
int json_load_file(json_t* jsn, FILE* file, jerr_t* err)
{
    char buf[JMAX_SRC_STR];
    FILE_get_path(file, buf, JMAX_SRC_STR);
    return _json_load_file(jsn, buf, file, err);
}

//------------------------------------------------------------------------------
int json_load_buf(json_t* jsn, const void* buf, size_t blen, jerr_t* err)
{
    char src[JMAX_SRC_STR];
    jsnprintf(src, sizeof(src), "%p", buf);
    return _json_load_buf(jsn, src, buf, blen, err);
}

//------------------------------------------------------------------------------
int json_load_path(json_t* jsn, const char* path, jerr_t* err)
{
    assert(jsn);
    assert(path);

    FILE* file = fopen(path, "r");
    if (!file)
    {
        jerr_init_src(err, path);
        jerr_set_msg(err, "could not read file");
        return 1;
    }

    int status = _json_load_file(jsn, path, file, err);
    fclose(file);

    return status;
}

//------------------------------------------------------------------------------
JINLINE jmem_t json_mem_arrays( json_t* jsn )
{
    jmem_t mem = {0,0};

    for ( size_t i = 0; i < jsn->arrays.len; i++ )
    {
        _jarray_t* a = _json_get_array(jsn, i);
        if (a->cap > BUF_SIZE)
        {
            mem.used += a->len * sizeof(jval_t);
            mem.reserved += a->cap * sizeof(jval_t);
        }
    }
    mem.used += sizeof(_jarray_t) * jsn->arrays.len;
    mem.reserved += sizeof(_jarray_t) * jsn->arrays.cap;
    return mem;
}

//------------------------------------------------------------------------------
JINLINE jmem_t json_mem_nums( json_t* jsn )
{
    jmem_t mem = {0,0};
    mem.used += sizeof(jnum_t) * jsn->nums.len;
    mem.reserved += sizeof(jnum_t) * jsn->nums.cap;
    return mem;
}


//------------------------------------------------------------------------------
JINLINE jmem_t json_mem_ints( json_t* jsn )
{
    jmem_t mem = {0,0};
    mem.used += sizeof(jint_t) * jsn->ints.len;
    mem.reserved += sizeof(jint_t) * jsn->ints.cap;
    return mem;
}

//------------------------------------------------------------------------------
JINLINE jmem_t json_mem_objs( json_t* jsn )
{
    jmem_t mem = {0,0};
    for ( size_t i = 0; i < jsn->objs.len; i++ )
    {
        _jobj_t* a = _json_get_obj(jsn, i);
        if (a->cap > BUF_SIZE)
        {
            mem.used += a->len * sizeof(jkv_t);
            mem.reserved += a->cap * sizeof(jkv_t);
        }
    }
    mem.used += sizeof(_jobj_t) * jsn->objs.len;
    mem.reserved += sizeof(_jobj_t) * jsn->objs.cap;
    return mem;
}

//------------------------------------------------------------------------------
jmem_stats_t json_get_mem(json_t* jsn)
{
    jmem_stats_t stats;
    memset(&stats, 0, sizeof(stats));

    stats.nums = json_mem_nums(jsn);
    stats.ints = json_mem_ints(jsn);
    stats.arrays = json_mem_arrays(jsn);
    stats.objs = json_mem_objs(jsn);
    stats.strs = jmap_get_mem(&jsn->strmap);

    stats.total.used = stats.nums.used + stats.ints.used + stats.arrays.used + stats.objs.used + stats.strs.used;
    stats.total.reserved = stats.nums.reserved + stats.ints.reserved + stats.arrays.reserved + stats.objs.reserved + stats.strs.reserved;

    return stats;
}

//------------------------------------------------------------------------------
JINLINE void compile_macros()
{
    json_t* jsn = json_new();
    jval_t root = json_root(jsn);

    jobj_t obj = json_get_obj(jsn, root);

    jarray_t array = jobj_find_array(obj, "");
    jval_t val = jarray_get(array, 0);
    const char* key = "";
    const char* str = "";
    jerr_t err;
    FILE* file = NULL;
    size_t idx = 0;

    jobj_find(obj, key);
    jobj_find_array(obj, key);
    jobj_find_bool(obj, key);
    jobj_find_nil(obj, key);
    jobj_find_num(obj, key);
    jobj_find_obj(obj, key);
    jobj_find_str(obj, key);
    jerr_fprint(file, &err);

    jarray_get_array(array, idx);
    jarray_get_bool(array, idx);
    jarray_get_json(array);
    jarray_get_num(array, idx);
    jarray_get_obj(array, idx);
    jarray_get_str(array, idx);

    jobj_get_json(obj);
    jobj_find(obj, key);
    jobj_add_str(obj, key, str);

    jobj_findl_idx(obj, key, strlen(key));

    jval_is_array(val);
    jval_is_str(val);
    jval_is_num(val);
    jval_is_nil(val);
    jval_is_obj(val);
    jval_is_true(val);
    jval_is_false(val);
    jval_is_bool(val);

    json_free(jsn);
}
