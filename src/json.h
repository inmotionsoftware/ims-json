/*!
    @file
    @header json.h
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
    
    @abstract 
    Highly efficient json library implemented in portable C with a convenient 
    C++11 interface.

    @details
    The design goals of this library are to be an efficient, high performance, 
    portable json parser, while still being easy to use and understand. 
    
    Json documents are still processed and stored in a tree-like structure and
    therefore this library can be categorized as a DOM parsers rather than a 
    pure stream processor.
    
    In general we attempt to keep memory overhead as low as possible and in many
    cases we de-dup some data internally. Strings are internalized and stored 
    in a hashtable, which eliminates duplicate strings for both keys and string 
    values.

    We take a data-oriented approach to design over the typical object-oriented
    approach. This means we use a struct-of-arrays over the more traditional
    array-of-structs. This further reduces memory, and more importantly allows
    us to ammortize allocations over many values instead of having to allocate 
    each individual value.
    
    A flexible serializer is provided with built in functions for writing to a
    FILE or a memory buffer. For more advanced options, the caller can provide
    a user defined output function.
    
    Examples:
     
    Parsing a json doc from a file path.
    @code
    const char* path = "path/to/json";

    json_t jsn;
    json_init(&jsn);

    jerr_t err;
    if (json_load_path(&jsn, path, &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    Parsing a json doc from a c-string.
    @code
    jerr_t err;
    json_t jsn;
    json_init(&jsn);
    if (json_load_str(&jsn, "{\"key\": 1}", &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    Parsing a json doc from a buffer.
    @code
    char buf[] = "{\"key\": 1}";
    size_t buflen = sizeof(buf);

    jerr_t err;
    json_t jsn;
    json_init(&jsn);
    if (json_load_buf(&jsn, buf, buflen, &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    Constructing a json file dynamically.
    @code
    json_t jsn;
    json_init(&jsn);
    jobj_t root = json_root(&jsn);
    {
        jobj_add_bool(root, "true", true);
        jobj_add_bool(root, "false", false);
        jobj_add_nil(root, "nil");
        jobj_add_num(root, "num", 3.14);
        jobj_add_str(root, "string", "string");
        jobj_t obj = jobj_add_obj(root, "obj");
        {
            jobj_add_str(obj, "key", "child");
        }
        jarray_t array = jobj_add_array(root, "array");
        {
            jarray_add_bool(array, true);
            jarray_add_bool(array, false);
            jarray_add_nil(array);
            jarray_add_num(array, 5.5);
            jobj_t subobj = jarray_add_obj(array);
            {
                jobj_add_bool(subobj, "true", true);
            }
            jarray_t subarray = jarray_add_array(array);
            {
                jarray_add_num(subarray, 1);
                jarray_add_num(subarray, 2);
                jarray_add_num(subarray, 3);
            }
        }
    }
    json_destroy(&jsn);
    @endcode

    Write a json file to a file.
    @code
    json_t jsn;
    // load json...
    int flags = JPRINT_PRETTY|JPRINT_ESC_UNI;
    if (json_print_file(&jsn, flags, stdout) != 0)
    {
        // error!!!
    }
    json_destroy(&jsn);
    @endcode
    
    Create a json string.
    @code
    json_t jsn;
    // load json...
    int flags = JPRINT_PRETTY|JPRINT_ESC_UNI;
    char* str = json_to_str(&jsn, flags);
    if (!str)
    {
        // error...
    }

    // do stuff...

    free(str); // must free string when done
    json_destroy(&jsn);
    @endcode

*/
#ifndef __json_h__
#define __json_h__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
namespace ims {
#endif

//------------------------------------------------------------------------------
/*!
    Json number value.
*/
typedef double jnum_t;
typedef int64_t jint_t;

/*!
    Json boolean.
*/
typedef uint8_t jbool_t;

struct jstr_t;
struct _jobj_t;
struct _jarray_t;
struct json_t;

//------------------------------------------------------------------------------
/*!
    Boolean value representing TRUE.
*/
#define JTRUE  ((jbool_t)1)

/*!
    Boolean value representing FALSE.
*/
#define JFALSE ((jbool_t)0)

//------------------------------------------------------------------------------
extern const char JVER[32];

//------------------------------------------------------------------------------
/*!
    An enumeration of json value types.

    @constant JTYPE_NIL null json value.
    @constant JTYPE_STR a json string value.
    @constant JTYPE_NUM a json number type.
    @constant JTYPE_ARRAY a json array type.
    @constant JTYPE_OBJ a json object type.
    @constant JTYPE_TRUE a json boolean indicating true.
    @constant JTYPE_FALSE a json boolean indicating false.
*/
enum jtype_t
{
    JTYPE_NIL   = 0,
    JTYPE_STR   = 1,
    JTYPE_NUM   = 2,
    JTYPE_ARRAY = 3,
    JTYPE_OBJ   = 4,
    JTYPE_TRUE  = 5,
    JTYPE_FALSE = 6,
    JTYPE_INT   = 7
};
typedef enum jtype_t jtype_t;

/*!
    Mask for the json types. You must bitwise AND the type against this mask to
    check the value type in a jval_t.
    
    @see jval_t
*/
#define JTYPE_MASK 0x7

//------------------------------------------------------------------------------

/*!
    @constant JPRINT_PRETTY
    Output flag for "pretty" printing. Adds newlines and tabs to the output.
*/
static const int JPRINT_PRETTY = 0x1;

/*!
    @constant JPRINT_ESC_UNI
    Output flag for escaping unicode values in json strings and keys. Escape 
    sequence is \uXXXX.
*/
static const int JPRINT_ESC_UNI = 0x2;

/*!
    @constant JPRINT_NEWLINE_WIN
    Output flag for printing output with windows style '\\r\\n' newlines. The
    default behavior is to use unix style single '\\n' characters.
*/
static const int JPRINT_NEWLINE_WIN = 0x4;

/*!
    User function for writing json output. 
    
    @param ctx an opaque user pointer
    @param ptr a pointer to the data for writing
    @param n the number of bytes to write
*/
typedef size_t (*print_func)( void* ctx, const void* ptr, size_t n);

//------------------------------------------------------------------------------
/*!
    @group jval
    all json value functions and declarations.
*/

/*!
    @struct jval_t
    A struct representing a json value. Clients should not modify or access the
    fields directly as they can change between versions.
*/
struct jval_t
{
    uint32_t type : 4;
    uint32_t idx : 28;
};
typedef struct jval_t jval_t;

/*!
    NULL value.
*/
#define JNULL_VAL ((jval_t){.type=JTYPE_NIL, .idx=0})

/*!
    Writes the json value to the output function. 
    
    @see JPRINT_ESC_UNI
    @see JPRINT_PRETTY
    
    @param jsn the json doc.
    @param val the value.
    @param flags optional output flags.
    @param p the output function for writing the json.
    @param udata opaque user pointer passed to the output function.
*/
void jval_print(struct json_t* jsn, jval_t val, int flags, print_func p, void* udata);

/*!
    @function jval_type
    Gets the type of the value.
    
    @param VAL the value.
    @return the json type of the value.
*/
#define jval_type(VAL) (VAL.type & JTYPE_MASK)

/*!
    @function jval_is_str
    Checks whether or not the given value is a string type.
    
    @param VAL the value.
    @return whether or not the value is a string type.
*/
#define jval_is_str(VAL) (jval_type(VAL) == JTYPE_STR)

/*!
    @function jval_is_num
    Checks whether or not the given value is a number type.
    
    @param VAL the value.
    @return whether or not the value is a number type.
*/
#define jval_is_num(VAL) (jval_type(VAL) == JTYPE_NUM || jval_type(VAL) == JTYPE_INT)

/*!
    @function jval_is_int
    Checks whether or not the given value is an integer type.
    
    @param VAL the value.
    @return whether or not the value is a integer type.
*/
#define jval_is_int(VAL) (jval_type(VAL) == JTYPE_INT)

/*!
    @function jval_is_obj
    Checks whether or not the given value is an object type.
    
    @param VAL the value.
    @return whether or not the value is an object type.
*/
#define jval_is_obj(VAL) (jval_type(VAL) == JTYPE_OBJ)

/*!
    @function jval_is_true
    Checks whether or not the given value is true.
    
    @param VAL the value.
    @return whether or not the value is true.
*/
#define jval_is_true(VAL) (jval_type(VAL) == JTYPE_TRUE)

/*!
    @function jval_is_false
    Checks whether or not the given value is false.
    
    @param VAL the value.
    @return whether or not the value is false.
*/
#define jval_is_false(VAL) (jval_type(VAL) == JTYPE_FALSE)

/*!
    @function jval_is_bool
    Checks whether or not the given value is a boolean type.
    
    @param VAL the value.
    @return whether or not the value is a boolean type.
*/
#define jval_is_bool(VAL) (jval_is_true(VAL) || jval_is_false(VAL))

/*!
    @function jval_is_nil
    Checks whether or not the given value is a nil type.
    
    @param VAL the value.
    @return whether or not the value is a nil type.
*/
#define jval_is_nil(VAL) (jval_type(VAL) == JTYPE_NIL)

/*!
    @function jval_is_array
    Checks whether or not the given value is an array type.
    
    @param VAL the value.
    @return whether or not the value is an array type.
*/
#define jval_is_array(VAL) (jval_type(VAL) == JTYPE_ARRAY)

//------------------------------------------------------------------------------
/*!
    @group jobj
*/

/*!
    A json object struct. Do not modify or access the fields directly. They are 
    subject to change version to version.
    
    @field json pointer to the parent json document.
    @field idx index of the json object data.
*/
struct jobj_t
{
    struct json_t* json;
    size_t idx;
};
typedef struct jobj_t jobj_t;

/*!
    NULL object
*/
#define JNULL_OBJ ((jobj_t){.json=NULL, .idx=0})

/*!
    @function jobj_is_null
    Tests whether or not the given object is NULL.
*/
#define jobj_is_null(OBJ) ((OBJ).json == NULL)

/*!
    Reserves additional capacity for the object. The new capacity is at least
    the current capacity + N
    
    @param obj the object.
    @param n the additional capacity reserved.
*/
void jobj_reserve( jobj_t obj, size_t n );

/*!
    Appends a number to the object with the given key.
    
    @param obj the object to append to.
    @param key the key of the number.
    @param num the number to append.
*/
void jobj_add_num( jobj_t obj, const char* key, jnum_t num );


/*!
    Appends an integer to the object with the given key.
    
    @param obj the object to append to.
    @param key the key of the number.
    @param num the integer to append.
*/
void jobj_add_int( jobj_t obj, const char* key, jint_t num );

/*!
    Appends a string to the object with the given key.
    
    @param obj the object.
    @param key the key of the string.
    @param str the string to be appended.
    @param slen the length of the string being appended.
*/
void jobj_add_strl( jobj_t obj, const char* key, const char* str, size_t slen );

/*!
    Appends a boolean to the object with the given key.
    
    @param obj the object to append to.
    @param key the key of the boolean.
    @param b the boolean value.
*/
void jobj_add_bool( jobj_t obj, const char* key, jbool_t b );

/*!
    Appends a nil value to the object with the given key.
    
    @param obj the object to append to.
    @param key the key of the nil value.
*/
void jobj_add_nil( jobj_t obj, const char* key );

/*!
    Appends and returns a new object to the specified object with the given key.
    
    @param obj the object to append to.
    @param key the key of the new object.
    @return the newly created object.
*/
jobj_t jobj_add_obj( jobj_t obj, const char* key );

/*!
    @function jobj_findl
    Finds the first matching key.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @param KLEN the length of the key string.
    @return the matching value or JNULL_VAL if not found.
*/
#define jobj_findl(OBJ, KEY, KLEN) jobj_get_val(OBJ, jobj_findl_idx(OBJ, KEY, KLEN))

/*!
    Searches the object for a matching key starting at the given index.
    
    @param obj the object to search.
    @param idx the index to start searching from.
    @param key the key to search.
    @param klen the length of the key.
    
    @return the index of the matching key-value, or SIZE_MAX if not found.
*/
size_t jobj_findl_next_idx( jobj_t obj, size_t idx, const char* key, size_t klen );

/*!
    @function jobj_findl_idx
    Finds the first matching value for the given key.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @param KLEN the length of the key string.
    @return the index of the matching key-value or SIZE_MAX if not found.
*/
#define jobj_findl_idx(OBJ, KEY, KLEN) jobj_findl_next_idx(OBJ, 0, KEY, KLEN)

/*!
    Gets the length of the object.
    
    @param obj the object.
    @return the number of key-values in the object.
*/
size_t jobj_len( jobj_t obj );

/*!
    Gets the key and value from object at the given index.
    
    @param obj the object.
    @param idx the index to retrieve. If the index is out of range, the results 
           are unspecified.
    @param val a pointer to hold the value returned. Must not be NULL.
    @return the key of the value.
*/
const char* jobj_get(jobj_t obj, size_t idx, jval_t* val);

/*!
    Gets the value from the object at the given index.
    
    @param obj the object.
    @param idx the index to retrieve.
    @return the value at the given index. If the index is out of range, the 
            results are undefined.
*/
jval_t jobj_get_val(jobj_t obj, size_t idx);

/*!
    Writes the json object to the output function.
    
    @see JPRINT_ESC_UNI
    @see JPRINT_PRETTY
    
    @param obj the object to write.
    @param flags optional output flags.
    @param p the output function for writing the json.
    @param udata opaque user pointer passed to the output function.
*/
void jobj_print(jobj_t obj, int flags, print_func p, void* udata);

/*!
    @function jobj_get_json
    Gets the json doc the given object is attached to.
    
    @param OBJ the object
    @return the json this object is attached to.
*/
#define jobj_get_json(OBJ) (OBJ).json

/*!
    @function jobj_find
    Finds the first value with the matching key.
    
    @param OBJ the search target.
    @param KEY the search key.
    @return
*/
#define jobj_find(OBJ, KEY) jobj_findl(OBJ, KEY, strlen(KEY))

/*!
    @function jobj_add_str
    Appends a string to the object with the given key.
    
    @param OBJ the object to append to.
    @param KEY the key string.
    @param STR the string to be appended.
*/
#define jobj_add_str(OBJ, KEY, STR) jobj_add_strl(OBJ, KEY, STR, strlen(STR))

/*!
    @function jobj_find_str
    Searches the json doc for a string with the given key. If the key is not
    found or the value is not a string, a NULL pointer is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return a string value, or NULL if not found.
*/
#define jobj_find_str(OBJ, KEY) json_get_str(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/*!
    @function jobj_find_num
    Searches the json doc for a number with the given key. If the key is not
    found or the value is not a number, 0 is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return a number value, or 0 if not found.
*/
#define jobj_find_num(OBJ, KEY) json_get_num(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/*!
    @function jobj_find_bool
    Searches the json doc for a boolean with the given key. If the key is not
    found or the value is not a boolean, JFALSE is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return a boolean value, or JFALSE if not found.
*/
#define jobj_find_bool(OBJ, KEY) json_get_bool(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/*!
    @function jobj_find_nil
    Searches the json doc for a nil value with the given key. Returnes JTRUE
    if the value is a nil, or JFALSE otherwise. If the key is not found, JFALSE
    is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return JTRUE if the value is found and is nil, JFALSE otherwise.
*/
#define jobj_find_nil(OBJ, KEY) jval_is_nil(jobj_find(OBJ, KEY))

/*!
    @function jobj_find_obj
    Searches the json doc for an object value with the given key. If the key
    is not found or the value is not an object, a JNULL_OBJ is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return an object value, or JNULL_OBJ if not found.
*/
#define jobj_find_obj(OBJ, KEY) json_get_obj(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

/*!
    @function jobj_find_array
    Searches the json doc for an array value with the given key. If the key
    is not found or the value is not an array, a JNULL_ARRAY is returned.
    
    @param OBJ the object to search.
    @param KEY the key to search for.
    @return an array value, or JNULL_ARRAY if not found.
*/
#define jobj_find_array(OBJ, KEY) json_get_array(jobj_get_json(OBJ), jobj_find(OBJ, KEY))

//------------------------------------------------------------------------------

/*!
    @group jarray
*/

/*!
    Json array struct. Do not modify or access the fields directly as they may
    change version to version.
    
    @field json pointer to the parent json document.
    @field idx index of the json array data.
*/
struct jarray_t
{
    struct json_t* json;
    size_t idx;
};
typedef struct jarray_t jarray_t;

/*!
    NULL array
*/
#define JNULL_ARRAY ((jarray_t){.json=NULL, .idx=0})

/*!
    @function jarray_is_null
    Tests whether or not the given array is NULL
*/
#define jarray_is_null(ARRAY) ((ARRAY).json == NULL)

/*!
    Reserve additional memory for the array. The amount reserved is at least the
    current capacity + N.
    
    @param a the array.
    @param n the additional amount to reserve.
*/
void jarray_reserve( jarray_t a, size_t n );

/*!
    Appends a number to the end of array.
    
    @param a the array. 
    @param num the number to append.
*/
void jarray_add_num( jarray_t a, jnum_t num );

/*!
    Appends an integer to the end of array.
    
    @param a the array. 
    @param num the integer to append.
*/
void jarray_add_int( jarray_t a, jint_t num );

/*!
    Appends a string to the end of the array.
    
    @param a the array.
    @param str the string to append.
    @param slen the length of the string.
*/
void jarray_add_strl( jarray_t a, const char* str, size_t slen );

/*!
    Appends a boolean to the end of the array.
    
    @param a the array.
    @param b the boolean to append.
*/
void jarray_add_bool( jarray_t a, jbool_t b );

/*!
    Appends a nil to the end of the array.
    
    @param a the array.
*/
void jarray_add_nil(jarray_t a);

/*!
    Appends and returns a new array to the array.
    
    @param a the array.
    @return the newly created array appended to the given array.
*/
jarray_t jarray_add_array(jarray_t a);

/*!
    Appends and returns a new object to the end of the array.
    
    @param a the array.
    @return the newly created object appended to the given array.
*/
jobj_t jarray_add_obj(jarray_t a);

/*!
    Get the length of the array.
    
    @param a the array.
*/
size_t jarray_len(jarray_t a);

/*!
    Gets the value of an item in the given array at the index.
    
    If the index is outside of the bounds of the array the results are 
    unspecified.
    
    @param a the array.
    @param idx the index of the array.
    @return the value at the index.
*/
jval_t jarray_get(jarray_t a, size_t idx);

/*!
    Writes the array out to the given function.
    
    @param array the array.
    @param flags the optional flags to control the format.
    @param p the print function.
    @param udata opaque user data passed to the output function.
*/
void jarray_print(jarray_t array, int flags, print_func p, void* udata);

/*!
    @function jarray_add_str
    Appends a string value to the end of the array.
    
    @param A the array, must not be NULL.
    @param STR the string, must not be NULL.
*/
#define jarray_add_str(A, STR) jarray_add_strl(A, STR, strlen(STR))

/*!
    @function jarray_get_json
    Gets the json doc the array is attached to.

    @param A the array, must not be NULL.
    @return a pointer to the json doc this array is attached to.
*/
#define jarray_get_json(A) (A).json

/*!
    @function jarray_get_str
    Gets a string from the array at the given index. 
    
    @param A the array
    @param IDX the index of the string value.
    @return the string value for the index, or NULL.
*/
#define jarray_get_str(A, IDX) json_get_str(jarray_get_json(A), jarray_get(A, IDX))

/*!
    @function jarray_get_num
    Gets a number from the array at the given index.
    
    @param A the array
    @param IDX the index of the number value.
    @return the number value for the index, or 0.
*/
#define jarray_get_num(A, IDX) json_get_num(jarray_get_json(A), jarray_get(A, IDX))

/*!
    @function jarray_get_bool
    Gets a boolean from the array at the given index.
    
    @param A the array
    @param IDX the index of the boolean value.
    @return the boolean value for the index, or 0.
*/
#define jarray_get_bool(A, IDX) json_get_bool(jarray_get_json(A), jarray_get(A, IDX))

/*!
    @function jarray_get_obj
    Gets an object from the array at the given index.
    
    @param A the array
    @param IDX the index of the object value.
    @return the object value for the index, or JNULL_OBJ.
*/
#define jarray_get_obj(A, IDX) json_get_obj(jarray_get_json(A), jarray_get(A, IDX))

/*!
    @function jarray_get_array
    Gets an array from the array at the given index.
    
    @param A the array
    @param IDX the index of the array value.
    @return the array value for the index, or JNULL_ARRAY.
*/
#define jarray_get_array(A, IDX) json_get_array(jarray_get_json(A), jarray_get(A, IDX))

//------------------------------------------------------------------------------
/*!
    @group jmap
*/

/*!
    Internal structure used for the string hash table. Should not be accessed
    or modified directly. Could change in future revisions.
    
    @details 
    Strings are stored in a simple array.
    
    Strings are indexed in a hashtable. The hashtable contains the string's 
    index not the string itself.
    
    @field blen numer of bucket slots
    @field bcap bucket capacity
    @field buckets array of buckets
    @field slen number of strings in the strs array
    @field scap capacity of the strings array
    @field strs array of string values
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
/*!
    @group jerr
*/

/*!
    json error structure used when parsing json docs.
*/
struct jerr_t
{
    /// the line the error occured on.
    size_t line;

    /// the column within the line the error occured on.
    size_t col;

    size_t pcol;
    size_t pline;

    size_t off;

    /// the document source
    char src[255];

    /// the error message
    char msg[255];
};
typedef struct jerr_t jerr_t;

/*!
    @function jerr_fprint
    formats and writes the error to the given FILE.
    @param FILE the file, must not be NULL.
    @param JERR the error, must not be NULL.
*/
#define jerr_fprint(FILE, JERR) fprintf(FILE, "%s:%zu:%zu: %s", (JERR)->src, (JERR)->line+1, (JERR)->col, (JERR)->msg)

//------------------------------------------------------------------------------
/*!
    @group json
*/
/*!
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
    jval_t root;

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
        jint_t* ptr;
    } ints;

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

/*!
    @functiongroup json
*/

/*!
    @function json_new
    Allocates and initializes a new json doc. The doc can be safely used 
    immediately. If allocation fails, NULL is returned. The caller is 
    responsible for both destroying and free'ing the returned doc. 

    @see json_free
    
    @return a newly allocated and initialized json doc.
*/
#define json_new() json_init((json_t*)calloc(1, sizeof(json_t)))

/*!
    Initializes a new json doc. The doc can be safely used after initialization. 
    The caller is responsible for both destroying the returned doc.
    
    @see json_destroy
    
    @param jsn an uninitialized json doc. If the doc is already initialized, caller
           must not call this again unless the doc is destroyed.
    @return the input jsn or NULL if an error occurs.
*/
json_t* json_init( json_t* jsn );

/*!
    Clears out the contents of the json doc, removing all keys and values. The 
    end result will be an empty json document.
    
    @code
    json_t* jsn;
    //...
    json_clear(jsn);
    size_t len = jobj_len(json_root(jsn));
    assert( len == 0 ); // no keys
    @endcode
    
    @param jsn the json doc to clear.
*/
void json_clear( json_t* jsn );

/*!
    Loads a json doc from the local filesystem at the given path. 
    
    Example:
    @code
    const char* path = "path/to/json";

    json_t jsn;
    json_init(&jsn);

    jerr_t err;
    if (json_load_path(&jsn, path, &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    @param jsn the json doc to load
    @param path the local path to the json file
    @param err pointer to store error info on failure
    @return the status code. Non-zero for an error.
*/
int json_load_path(json_t* jsn, const char* path, jerr_t* err);

/*!
    Loads a json doc from the given FILE.
    
    @param jsn the json doc to load. Must not be null.
    @param file a json FILE.
    @param err pointer to store error info on failure.
    @return the status code. Non-zero for an error.
*/
int json_load_file(json_t* jsn, FILE* file, jerr_t* err);

/*!
    Loads a json doc from a memory buffer of the given length.
    
    Example:
    @code
    char buf[] = "{\"key\": 1}";
    size_t buflen = sizeof(buf);

    jerr_t err;
    json_t jsn;
    json_init(&jsn);
    if (json_load_buf(&jsn, buf, buflen, &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    @param jsn the json doc to load.
    @param buf a memory buffer with a json doc.
    @param blen the length of the memory buffer.
    @param err pointer to store error info on failure.
    @return the status code. Non-zero for an error.
*/
int json_load_buf(json_t* jsn, const void* buf, size_t blen, jerr_t* err);

/*!
    @function json_load_str
    Loads a json doc from a non-NULL c-string.
    
    Example:
    @code
    jerr_t err;
    json_t jsn;
    json_init(&jsn);
    if (json_load_str(&jsn, "{\"key\": 1}", &err) != 0)
    {
        // error!!!
        jerr_fprint(stderr, err);
    }
    json_destroy(&jsn);
    @endcode
    
    @param JSN the json doc to load.
    @param CSTR a c-string, must not be NULL.
    @param ERR pointer to store error info on failure.
    @return the status code. Non-zero for an error.
*/
#define json_load_str(JSN, CSTR, ERR) json_load_buf(JSN, CSTR, strlen(CSTR), ERR)

/*!
    Writes the json doc to the file. The json format can be controlled by 
    passing in optional flags.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param path the path of a local file.
    @return the number of bytes written, or zero if an error occured.
*/
size_t json_print_path(json_t* jsn, int flags, const char* path);

/*!
    Writes the json doc to the file. The json format can be controlled by 
    passing in optional flags.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    Example:
    @code
    json_t jsn;
    // load json...
    int flags = JPRINT_PRETTY|JPRINT_ESC_UNI;
    if (json_print_file(&jsn, flags, stdout) != 0)
    {
        // error!!!
    }
    json_destroy(&jsn);
    @endcode
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param file the output FILE.
    @return the number of bytes written, or zero if an error occured.
*/
size_t json_print_file(json_t* jsn, int flags, FILE* file);

/*!
    Writes the json doc to the given user function. The udata is an opaque user
    pointer passed to the print_func.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    @param jsn the json doc to write. Must not be null.
    @param flags optional flags for controlling output format.
    @param p function the json data is sent to.
    @param udata opac user data forwarded to the print function
    @return the number of bytes written, or zero if an error occured.
*/
size_t json_print(json_t* jsn, int flags, print_func p, void* udata);

/*!
    Writes the json doc to an in memory string. The string is allocated using
    the standard routines. The returned string must be free'd by the caller.
    
    @ref JPRINT_PRETTY output in a "pretty" format, adding newlines and tabs
    @ref JPRINT_ESC_UNI escape unicode chars with the format \uXXXX
    
    Example:
    @code
    json_t jsn;
    // load json...

    int flags = JPRINT_PRETTY|JPRINT_ESC_UNI;
    char* str = json_to_str(&jsn, flags);
    if (!str)
    {
        // error...
    }

    // do stuff...

    free(str); // must free string when done
    json_destroy(&jsn);
    @endcode
    
    @param jsn the json doc to serialize. Must not be null.
    @param flags optional flags for controlling output format.
    @return the json string. Null for an error. Must be free'd by caller.
*/
char* json_to_str(json_t* jsn, int flags);

/*!
    Destroys the given json_t and calls free() on the pointer.
    
    @see json_destroy
*/
void json_free(json_t* j);

/*!
    Destroys the internal json state and free's any internally allocated memory. 
    Once destroyed the json_t cannot be safely used without re-initializing.
    
    @param jsn the json_t to destroy.
*/
void json_destroy(json_t* jsn);

/*!
    Retrieves the root value of the given json, or a NIL value if it does not 
    have one.

    @see jval_is_nil
    
    @param JSN the json doc. Must not be null.
    @return the json doc's root value, or NIL if it does not have one.
*/
#define json_root(JSN) (JSN)->root

/*!
    Compares one value to another in the same json doc. Undefined if either 
    value is not part of the given json doc.
    
    @param jsn the json doc.
    @param v1 the first value.
    @param v2 the second value.
    @return <0 if v1 is less then v2, >0 if v1 is greater then v2, or 0 if they 
            are equal.
*/
int json_compare_val(json_t* jsn, jval_t v1, jval_t v2);

/*!
    Compares two json docs to one another.
    
    @param j1 the first json doc.
    @param j2 the second json doc.
    @return <0 if j1 is less then j2, >0 if j1 is greater then j2, or 0 if they
            are equal.
*/
int json_compare( json_t* j1, json_t* j2 );

/*!
    Retrieves the root object of the given json. 
    
    @details
    If the json is empty (i.e. no root) a new empty json object is added and 
    assigned as the root. However, if the root is already assigned and is 
    something other than an object the results are undefined.

    @param jsn the json doc. Must not be null.
    @return the json doc's root object.
*/
jobj_t json_root_obj( json_t* jsn );

/*!
    Retrieves the root array of the given json.
    
    @details
    If the json is empty (i.e. no root) a new empty json array is added and
    assigned as the root. However, if the root is already assigned and is 
    something other than an array the results are undefined.
    
    @param jsn the json doc. Must not be null.
    @return the json doc's root array.
*/
jarray_t json_root_array( json_t* jsn );

/*!
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

/*!
    Get's a number value from a json doc. If the value is not a number a 0 is 
    returned. If the value does not belong to the given json doc, the result is 
    undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be a number value and must belong to 
               the json doc.
    @return The number value, or 0 if not found / invalid.
*/
jnum_t json_get_num( json_t* jsn, jval_t val );

/*!
    Get's an integer value from a json doc. If the value is not an integer a 0 
    is returned. If the value does not belong to the given json doc, the result 
    is undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be an integer value and must belong to
               the json doc.
    @return The number value, or 0 if not found / invalid.
*/
jint_t json_get_int( json_t* jsn, jval_t val );

/*!
    Get's a boolean value from a json doc. If the value is not a boolean a 
    JFALSE is returned. If the value does not belong to the given json doc, the 
    result is undefined.
    
    @param jsn the json doc. Must not be null.
    @param val the value to retrieve. Must be a boolean value and must belong to 
               the json doc.
    @return The boolean value, or JFALSE if not found / invalid.
*/
jbool_t json_get_bool( json_t* jsn, jval_t val );

/*!
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

/*!
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

/*!
    @group jobj
*/

/*!
    Appends and returns a new array to the end of the object with the given key.
    
    @param obj the object.
    @param key the key.
    @return a newly created array.
*/
struct jarray_t jobj_add_array( jobj_t obj, const char* key );

/*!
    stucture to store the amount of used and reserved memory.
    
    @field used the amount of used memory in bytes
    @field reserved the amount of reserved memory in bytes.
*/
struct jmem_t
{
    size_t used;
    size_t reserved;
};
typedef struct jmem_t jmem_t;

/*!
    Stores stats to memory usage of a json doc.
    
    @field nums the memory used to store all numbers in the doc.
    @field objs the memory used to store all objects in the doc.
    @field arrays the memory used to store all arrays in the doc.
    @field strs the memory used to store all strings in the doc.
    @field total the total memory used in the doc.
*/
struct jmem_stats_t
{
    jmem_t nums;
    jmem_t ints;
    jmem_t objs;
    jmem_t arrays;
    jmem_t strs;
    jmem_t total;
};
typedef struct jmem_stats_t jmem_stats_t;

/*!
    Calculates the amount of memory used by the json doc. Reports both used and
    reserved memory.
    
    @param jsn the json doc.
    @return data structure reporting the amount of memory used and reserved.
*/
jmem_stats_t json_get_mem( json_t* jsn );

#ifdef __cplusplus
} // namespace
} // extern "C"
#endif

#endif /* defined(__json_h__) */
