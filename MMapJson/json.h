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
#define JTRUE  ((jbool_t)1)
#define JFALSE ((jbool_t)0)

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
static const int JPRINT_PRETTY = 0x1;
static const int JPRINT_ESC_UNI = 0x2;
typedef void (*print_func)( void* ctx, const char* );

//------------------------------------------------------------------------------
struct jval_t
{
    uint32_t type : 4;
    uint32_t idx : 28;
};
typedef struct jval_t jval_t;

void jval_print(struct json_t* jsn, jval_t val, int flags, print_func p, void* udata);
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

#define JNULL_OBJ ((jobj_t){.json=NULL, .idx=0})

void jobj_reserve( jobj_t obj, size_t cap );
void jobj_add_num( jobj_t obj, const char* key, jnum_t num );
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen );
void jobj_add_bool( jobj_t obj, const char* key, jbool_t b );
void jobj_add_nil( jobj_t obj, const char* key );
jobj_t jobj_add_obj( jobj_t obj, const char* key );

size_t jobj_findl( jobj_t obj, const char* key, size_t klen );
size_t jobj_len( jobj_t obj );
const char* jobj_get(jobj_t obj, size_t idx, jval_t* val);
void jobj_print(jobj_t obj, int flags, print_func p, void* udata);

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

#define JNULL_ARRAY ((jarray_t){.json=NULL, .idx=0})

jarray_t jobj_add_array( jobj_t obj, const char* key );

void jarray_reserve( jarray_t a, size_t cap );
void jarray_add_num( jarray_t a, jnum_t num );
void jarray_add_strl( jarray_t a, const char* str, size_t slen );
void jarray_add_bool( jarray_t a, jbool_t b );
void jarray_add_nil(jarray_t a);
jarray_t jarray_add_array(jarray_t a);
jobj_t jarray_add_obj(jarray_t a);
size_t jarray_len(jarray_t a);
jval_t jarray_get(jarray_t a, size_t idx);
void jarray_print(jarray_t array, int flags, print_func p, void* udata);

/**
*/
#define jarray_add_str(A, STR) jarray_add_strl(A, STR, strlen(STR))

/**
*/
#define jarray_get_json(A) (A).json

/**
*/
#define jarray_get_str(A, IDX) json_get_str(jarray_get_json(A), jarray_get(A, IDX))

/**
*/
#define jarray_get_num(A, IDX) json_get_num(jarray_get_json(A), jarray_get(A, IDX))

//------------------------------------------------------------------------------

/**
    Internal structure used for the string hash table. Should not be accessed
    or modified directly. Could change in future revisions.
*/
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
/**
    json error structure used when parsing json docs.
*/
struct jerr_t
{
    /// the line the error occured on.
    size_t line;

    /// the column within the line the error occured on.
    size_t col;

    /// the document source
    char src[255];

    /// the error message
    char msg[255];
};
typedef struct jerr_t jerr_t;

/**
    formats and writes the error to the given FILE.
    @param FILE the file, must not be NULL.
    @param the error, must not be NULL.
*/
#define jerr_fprint(FILE, JERR) fprintf(FILE, "%s:%zu:%zu: %s", (JERR)->src, (JERR)->line+1, (JERR)->col, (JERR)->msg)

//------------------------------------------------------------------------------

/**
    structure representing a json document. Do not access or modify the internal
    fields directly. They are subject to change in future versions.
    
    @discussion
    Highly efficient json document both in terms of memory usage, allocations 
    and parsing speed. 
    
    Rather than relying on individually allocated unions for
    the json values (i.e. the array-of-structures pattern) we instead take a
    flattened structure-of-arrays approach. Each type of json value gets stored 
    in an array of the same type. This allows for efficient use of memory as 
    each type is only as big as it needs to be, and also allows us to ammortize 
    the allocation of each value as we can allocate them in chunks instead of 
    individually.
    
    Strings are internalized in an efficient hash table. Duplicate strings and
    keys are eliminated further reducing memory usage. This is important because
    most large documents tend to have the same keys repeated many times over. 
    The hashtable is efficient enough that most small documents will see 
    negligable performance overhead. An added benefit is key searches are very
    quick, especially in large objects with many keys.

*/
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

/**
    Allocates and initializes a new json doc. The doc can be safely used 
    immediately. If allocation fails, NULL is returned. The caller is 
    responsible for both destroying and free'ing the returned doc. 

    @see json_free
    
    @return a newly allocated and initialized json doc.
*/
#define json_new() json_init((json_t*)calloc(1, sizeof(json_t)))

/**
    Initializes a new json doc. The doc can be safely used after initialization. 
    The caller is responsible for both destroying the returned doc.
    
    @see json_destroy
    
    @param an uninitialized json doc. If the doc is already initialized, caller
           must not call this again unless the doc is destroyed.
    @return the input jsn or NULL if an error occurs.
*/
json_t* json_init( json_t* jsn );

/**
    Loads a json doc from the local filesystem at the given path. 
    
    @param jsn the json doc to load
    @param path the local path to the json file
    @param err pointer to store error info on failure
    @return the status code. Non-zero for an error.
*/
int json_load_path(json_t* jsn, const char* path, jerr_t* err);

/**
    Loads a json doc from the given FILE.
    
    @param jsn the json doc to load. Must not be null.
    @param file a json FILE.
    @param err pointer to store error info on failure.
    @return the status code. Non-zero for an error.
*/
int json_load_file(json_t* jsn, FILE* file, jerr_t* err);

/**
    Loads a json doc from a memory buffer of the given length.
    
    @param jsn the json doc to load.
    @param buf a memory buffer with a json doc.
    @param blen the length of the memory buffer.
    @param err pointer to store error info on failure.
    @return the status code. Non-zero for an error.
*/
int json_load_buf(json_t* jsn, void* buf, size_t blen, jerr_t* err);

/**
    Writes the json doc to the file. The json format can be controlled by 
    passing in optional flags.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param path the path of a local file.
    @return the status code. Non-zero for an error.
*/
int json_print_path(json_t* j, int flags, const char* path);

/**
    Writes the json doc to the file. The json format can be controlled by 
    passing in optional flags.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param file the output FILE.
    @return the status code. Non-zero for an error.
*/
int json_print_file(json_t* j, int flags, FILE* file);

/**
    Writes the json doc to the given user function. The udata is an opaque user
    pointer passed to the print_func.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param print_func function the json data is sent to.
    @param udata opac user data forwarded to the print function
    @return the status code. Non-zero for an error.
*/
int json_print(json_t* j, int flags, print_func p, void* udata);

/**
    Writes the json doc to an in memory string. The string is allocated using
    the standard routines. The returned string must be free'd by the caller.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to serialize. Must not be null.
    @param flags optional flags for controlling output format.
    @return the json string. Null for an error. Must be free'd by caller.
*/
char* json_to_str(json_t* jsn, int flags);

/**
    Destroys the given json_t and calls free() on the pointer.
    
    @see json_destroy
*/
void json_free(json_t* j);

/**
    Destroys the internal json state and free's any internally allocated memory. 
    Once destroyed the json_t cannot be safely used without re-initializing.
    
    @param jsn the json_t to destroy.
*/
void json_destroy(json_t* jsn);

/**
    Retrieves the root object of the given json. 
    
    @param jsn the json doc. Must not be null.
    @return the json doc's root object.
*/
jobj_t json_root(json_t* jsn);

/**
    Get's a string value from a json doc. If the value is not a string a NULL 
    pointer is returned. If the value does not belong to the given json doc, the
    result is undefined. 
    
    @details 
    The string returned is an interal buffer and must not be modified or free'd
    by the caller.

    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be a string value and must belong to 
               the json doc.
    @return The string value, or NULL if not found / invalid.
*/
const char* json_get_str( json_t* jsn, jval_t val );

/**
    Get's a number value from a json doc. If the value is not a number a 0 is 
    returned. If the value does not belong to the given json doc, the result is 
    undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be a number value and must belong to 
               the json doc.
    @return The number value, or 0 if not found / invalid.
*/
jnum_t json_get_num( json_t* jsn, jval_t val );

/**
    Get's a boolean value from a json doc. If the value is not a boolean a 
    JFALSE is returned. If the value does not belong to the given json doc, the 
    result is undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be a boolean value and must belong to 
               the json doc.
    @return The boolean value, or JFALSE if not found / invalid.
*/
jbool_t json_get_bool( json_t* jsn, jval_t val );

/**
    Get's an object value from a json doc. If the value is not an object an 
    JNULL_OBJ object is returned; attempting to use it will cause undefined
    behavior. If the value does not belong to the given json doc, the result is
    undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be an object value and must belong to
               the json doc.
    @return The obj value, or JNULL_OBJ if the value is not an object.
*/
jobj_t json_get_obj( json_t* jsn, jval_t val );

/**
    Get's an array value from a json doc. If the value is not an array an
    JNULL_ARRAY array is returned; attempting to use it will cause undefined
    behavior. If the value does not belong to the given json doc, the result is
    undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be an array value and must belong to
               the json doc.
    @return The obj array, or JNULL_ARRAY if the value is not an array.
*/
jarray_t json_get_array( json_t* jsn, jval_t val );

/**
    Searches the json doc for a string with the given key. If the key is not
    found or the value is not a string, a NULL pointer is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return a string value, or NULL if not found.
*/
#define jobj_find_str(OBJ, KEY) json_get_str(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/**
    Searches the json doc for a number with the given key. If the key is not
    found or the value is not a number, 0 is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return a number value, or 0 if not found.
*/
#define jobj_find_num(OBJ, KEY) json_get_num(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/**
    Searches the json doc for a boolean with the given key. If the key is not
    found or the value is not a boolean, JFALSE is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return a boolean value, or JFALSE if not found.
*/
#define jobj_find_bool(OBJ, KEY) json_get_bool(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/**
    Searches the json doc for a nil value with the given key. Returnes JTRUE
    if the value is a nil, or JFALSE otherwise. If the key is not found, JFALSE
    is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return JTRUE if the value is found and is nil, JFALSE otherwise.
*/
#define jobj_find_nil(OBJ, KEY) jval_is_nil(jobj_find(OBJ, KEY))

/**
    Searches the json doc for an object value with the given key. If the key
    is not found or the value is not an object, a JNULL_OBJ is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return an object value, or JNULL_OBJ if not found.
*/
#define jobj_find_obj(OBJ, KEY) json_get_obj(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/**
    Searches the json doc for an array value with the given key. If the key
    is not found or the value is not an array, a JNULL_ARRAY is returned.
    
    @param obj the object to search.
    @param key the key to search for.
    @return an array value, or JNULL_ARRAY if not found.
*/
#define jobj_find_array(OBJ, KEY) json_get_array(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

#ifdef __cplusplus
} // namespace
}
#endif

#endif /* defined(__MMapJson__json2__) */
