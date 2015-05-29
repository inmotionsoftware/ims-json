# ims-json

Why another json library?

I have been exploring data-driven approaches to game engine design for some time
now with some great results. I had not really seen a json library using that 
same mentality. I decided to do a little experiment born somewhat out of a need 
of a high performance json parser and partly out of pure curisoity. What I 
discovered (as no big surprise) is that applying some data-driven concepts to a 
json parsing library has some huge benefits in terms of both parsing performance
and  memory overheads. From this the ims-json library was born.

What I have developed is, I believe, one of the fastest (if not the fastest),
portable json parsers out there. I spent some time comparing it against many 
other popular libraries out there and found that ims-json is something around 5x
faster than some leading C libraries, and 20x faster than most other high level 
language parsers (such as Gson for Java) while using 1/10th the memory. You can 
see some of the results for yourself below.

--------------------------------------------------------------------------------
## Some high level features of the project:

- Small and portable C code with no external dependencies.
- Easy and intuitive to use.
- First class, modern, STL inspired C++11 interface that would seem natural to a C++ developer.
- Fast. Very fast.
- Memory efficient, low overall memory usage and low allocation counts.
- Support for UTF-8 and Unicode escape sequences.
- Command line utility for validating and formatting json. Error output designed for IDE integration.

--------------------------------------------------------------------------------
## Design

The basic gist of the design is that we take a different approach from the 
typical implementation which might use C unions to mash all json types together
into a single structure (array-of-structures approach).

```c
// typical json implementation
struct jval_t
{
    int type;
    union
    {
        char* str;
        jnum_t num;
        jobj_t obj;
        jarray_t array;
    }
}
```

Instead we take a structure-of-arrays approach. What this means is that we treat
each type of json value as a single separate array where all elements of the 
same type are stored together.

```c
// ims-json approach
struct json_t
{
    jnum_t* nums;
    jobj_t* objs;
    jarray_t* arrays;
    char** strs;
}
```

This has many benefits. We reduce memory consumption because each element takes
only as much space as it needs. No wasted space is taken by having all elements
mashed into a single union. Another benefit is that allocations are amortized as
we grow each array by over allocating. This is much faster than allocating each
element individually.

--------------------------------------------------------------------------------
## String handling

Some other improvements worth mentioning are that all string and key values are
internalized. This means that we do not store duplicate string values. If a 
string or key shows up more than once within a document (which is very typical 
of json) we store it once and then share a reference to it for all subsequent 
duplicate strings. There is some overhead with this as we use a hashtable to
store the strings, but this is typical of most json libraries and 
counterintuitively actually improves performance as it reduces the overall 
allocation count.

--------------------------------------------------------------------------------
## Performance Comparisions
All tests where run as x86_64 binaries on a Late 2013 MacBook Pro, 2.4Ghz Core 
i5 with 8GB of RAM running OSX 10.10.3.

Two test json files were used. 

The first is spatial data layer for the City and County of San Francisco and is 
about 189MB in size. [Download here](https://github.com/zemirco/sf-city-lots-json/blob/master/citylots.json)

The second file is a compilation of Magic the Gathering data / stats. This file 
is 42.4 MB in size. [Download here](http://mtgjson.com/json/AllSets-x.json)

The comparision was run against [jannson (v2.7)](http://www.digip.org/jansson/)
a C library, [cson (831c0ee46e)](http://fossil.wanderinghorse.net/wikis/cson/?page=cson) 
another C library and [Gson (v2.3.1)](https://code.google.com/p/google-gson/) a 
popular Java based parser. Gson was included purely as a baseline between native
code vs virtual machine code to illustrate how efficient C code can be. There 
are likely faster Java-based parsers out there.

### Runtime performance
library | citylots.json| magic.json|
--------|-------------:|----------:|
ims-json| 1.94 secs    | 0.86 secs |
jansson | 10.04 secs   | 2.88 secs |
cson    | 22.40 secs   | 4.84 secs |
gson    | 34.12 secs   | 3.38 secs |


### Memory Usage
library | citylots.json | magic.json |
--------|--------------:|-----------:|
ims-json| 118.27 MB     | 53.57 MB   |
jansson | 993.12 MB     | 140.39 MB  |
cson    | 828.15 MB     | 158.69 MB  |
gson    | 1,970.36 MB   | 304.38 MB  |

--------------------------------------------------------------------------------
## Bash Examples

####Compact a json document by removing any whitespace
```bash
ims-jsonc --compact --out /path/to/out.json /path/to/file.json
```

####Validate only, suppress output
```bash
ims-jsonc --suppress /path/to/file.json
```

####Read from stdin and write to stdout formatting the json in human readable form
```bash
ims-jsonc --format --stdin < /path/to/file.json
```

--------------------------------------------------------------------------------
## C Examples

####Parsing a json doc from a file path

```c
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
```

####Parsing a json doc from a c-string
```c
jerr_t err;
json_t jsn;
json_init(&jsn);
if (json_load_str(&jsn, "{\"key\": 1}", &err) != 0)
{
    // error!!!
    jerr_fprint(stderr, err);
}
json_destroy(&jsn);
```

####Parsing a json doc from a buffer
```c
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
```

####Constructing a json file dynamically
```c
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
```

####Write a json doc to a file
```c
json_t jsn;
// load json...
int flags = JPRINT_PRETTY|JPRINT_ESC_UNI;
if (json_print_file(&jsn, flags, stdout) != 0)
{
    // error!!!
}
json_destroy(&jsn);
```

####Create a json string
```c
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
```

--------------------------------------------------------------------------------
## C++ Examples

#### Create a json doc
```cpp
ims::json jsn;
auto root = jsn.root();
{
    root["true"] = true;
    root["false"] = false;
    root["nil"] = nullptr;
    root["num"] = 3.14;
    root["string"] = "string";
    
    // C++11 vargs template for creating an object
    root["child"].add_obj
    (
        "key", 5, 
        "key2", true
    );
    
    // nicer syntax, but less efficient as it generates a copy
    root["array"] = val::array
    {
        true,
        false,
        nullptr,
        5.5,
        val::obj
        {
            {"true", true},
            {"num", 5}
        },
        val::array{1,2,3}
    };
}
```

#### Find a key
```cpp
ims::json jsn;
//...

auto root = jsn.root();
auto it = root.find("string");
if (it != root.end())
{
    std::cout << (*it).first << (*it).second << std::endl;
}
```

#### Write json doc to output stream
```cpp
ims::json jsn;
//...
std::cout << jsn;
```

#### Convert json to string
```cpp
ims::json jsn;
//...
const std::string& str = jsn.str();
```

#### Iterating json objects
```cpp

ims::json jsn;
//...
auto root = jsn.root();
for ( auto it = root.begin(), end = root.end(); it != end; ++it )
{
    const std::string& key = (*it).first;
    const auto& val = (*it).second;
    // do stuff...
}

std::for_each(root.begin(), root.end(), []( const obj::key_val& pair )
{
    const std::string& key = pair.first;
    const auto& val = pair.second;
    // do stuff...
});
```

#License

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