# Yet another json library? 

I have been exploring data-driven design approaches to game engine design for 
some time now, I had not really seen a json library using that same mentality. 
I decided to do a little experiment born somewhat out of a need of a high 
performance json parser and partly out of pure curisoity. What I discovered 
(as no big surprise) is that applying some data-driven concepts to a json 
parsing library has some huge benefits in terms of both parsing performance and 
memory overheads. From there the ims-json library was born.

What I have developed is I believe one of the fastest (if not the fastest) 
portable json parser out there. I have spend some time comparing it against many 
other popular libraries out there and found that ims-json is something around 5x
faster than some leading C libraries, and 20x faster than most other high level
language parsers for Java / C# while using 1/10th the memory.

## Some high level features of the project:

- Portable C based library.
- Easy and intuitive to use.
- Out of the box modern STL inspired C++11 interface that would seem natural to a C++ developer.
- Fast
- Memory efficient, low overall memory usage and low allocation counts.


## Design
The basic gist of the design is that we take a different approach from the 
typical using C unions to mash all json types together into a single structure 
(array-of-structures approach).

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
each type of json value as a separate array where other elements of the same 
type are stored together. 

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

This has the benefit that we reduce memory because each element takes up only as
much space as needed, no more waste just so that we can mash values together. We 
also benefit in that allocations can amortized as each array can allocate chunks
of values instead individual values as is typical.

## String handling

Some other improvements worth mentioning are that all string and key values are
internalized. This means that we do not store duplicate string values. If a 
string or key shows up more than once within a document (which is very typical 
of json) we store it once and then share a reference to it for all subsequent 
duplicate strings. There is some overhead with this as we used a hashtable to
store all strings, but this is typical of most json libraries and 
counterintuitively actually improves performance as it reduces allocations.

## C Examples
     
###Parsing a json doc from a file path

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

###Parsing a json doc from a c-string
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

###Parsing a json doc from a buffer
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

###Constructing a json file dynamically
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

###Write a json file to a file
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

###Create a json string
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

## C++ Examples

### Create a json doc
```cpp
ims::json jsn;
auto root = jsn.root();
{
    root["true"] = true;
    root["false"] = false;
    root["nil"] = nullptr;
    root["num"] = 3.14;
    root["string"] = "string";
    root["child"].add_obj("key", 5, "key2", true);
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

### Find a key
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

### Write json doc to output stream
```cpp
ims::json jsn;
//...
std::cout << jsn;
```

### Convert json to string
```cpp
ims::json jsn;
//...
const std::string& str = jsn.str();
```

### Iterating json objects
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
