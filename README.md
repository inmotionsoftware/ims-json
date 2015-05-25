Why yet another (and another, and another) json library? I wanted a high 
performance, low memory, portable C / C++ json library. The goal is to 
have the highest performance json parser possible while still maintaining 
portability.

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
