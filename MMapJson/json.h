//
//  json.h
//  MMapJson
//
//  Created by Brian Howard on 4/17/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#ifndef __MMapJson__json__
#define __MMapJson__json__

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cassert>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <fstream>

//------------------------------------------------------------------------------
#define json_assert(A, STR, ...) { if (!(A)) {printf(STR "\n", ## __VA_ARGS__ ); abort();} }

// types
//------------------------------------------------------------------------------
typedef int64_t integer;
typedef double number;
typedef std::string jstr;

//------------------------------------------------------------------------------
template<typename F>
class scoped_guard
{
public:
    inline scoped_guard( scoped_guard&& mv )
        : lambda( std::move(mv.lambda) )
    {}

    inline scoped_guard( const F& f)
        : lambda(f)
    {}

    scoped_guard( const scoped_guard& ) = delete;
    scoped_guard& operator=( const scoped_guard& ) = delete;

    inline ~scoped_guard() { lambda(); }
protected:
    const F& lambda;
};

class __scoped_guard
{
public:
    __scoped_guard() {}

    template<typename F>
    inline scoped_guard<F> operator << ( const F& lambda)
    {
        return scoped_guard<F>(lambda);
    }
};

#ifndef SCOPED_GUARD
    #define _CONCAT(A, B) A ## B
    #define _UNIQUE_NAME(PREFIX, POSTFIX) _CONCAT(PREFIX, POSTFIX)
    #define UNIQUE_NAME(PREFIX) _UNIQUE_NAME(PREFIX, __LINE__)
    #define SCOPED_GUARD auto UNIQUE_NAME(scoped_guard_) = __scoped_guard() << [&]()
#endif

//------------------------------------------------------------------------------
class jobject
{
public:
    typedef std::pair<uint16_t, uint32_t> keyval;
    typedef std::vector< keyval > keymap;

    jobject( class json& j )
        : m_json(j)
    {}
    ~jobject() {}

    jobject( const jobject& cp )
        : m_json(cp.m_json)
        , m_keys(cp.m_keys)
    {}

    jobject& operator=( const jobject& ) = delete;

    bool empty() const { return m_keys.empty(); }

    jobject& create_obj( const std::string& key );
    class jarray& create_array( const std::string& key );

    void add( const std::string& key, number num );
    void add( const std::string& key, bool b );
    void add( const std::string& key, const std::string& str );
    void add( const std::string& key, std::nullptr_t n );

    std::ostream& write( std::ostream& os, int depth = 0) const;

    class iterator
    {
    public:
        typedef std::pair<const std::string&, class value> vpair;

        iterator( const jobject& p, keymap::const_iterator it )
            : m_parent(p)
            , m_iter(it)
        {}

        iterator operator++ (int)
        {
            iterator it = *this;
            ++m_iter;
            return it;
        }
        iterator& operator++ () { ++m_iter; return *this; }

        bool operator!= ( const iterator& it ) const { return m_iter != it.m_iter; }
        bool operator== ( const iterator& it ) const { return m_iter == it.m_iter; }
        vpair operator*() const;

    protected:
        const jobject& m_parent;
        keymap::const_iterator m_iter;
    };

    iterator begin() const { return iterator(*this, m_keys.begin()); }
    iterator end() const { return iterator(*this, m_keys.end()); }

protected:
    json& m_json;
    keymap m_keys;
};

//------------------------------------------------------------------------------
class jarray
{
public:

    jarray( json& j )
        : m_json(j)
    {}
    ~jarray() {}

    jarray( const jarray& cp)
        : m_json(cp.m_json)
        , m_items(cp.m_items)
    {}

    bool empty() const { return m_items.empty(); }

    void push_back ( integer i ) { this->push_back((double)i); }
    void push_back ( double d );
    void push_back ( const std::string& s );
    void push_back ( bool b );
    void push_back ( std::nullptr_t ) { m_items.push_back(0); }

    jobject& create_obj();
    jarray& create_array();

    std::ostream& write( std::ostream& os, int depth = 0) const;

protected:
    json& m_json;
    std::vector<uint32_t> m_items;
};

//------------------------------------------------------------------------------
class json
{
    friend class jobject;
    friend class jarray;
    friend class value;
public:
    enum type
    {
        NIL     = 0x0,
        TRUE    = 0x1,
        FALSE   = 0x2,
        STRING  = 0x4,
        NUMBER  = 0x8,
        INTEGER = 0xA,
        ARRAY   = 0xB,
        OBJECT  = 0xC,
        BOOLEAN = TRUE|FALSE
    };

    static json load_buf( const void* ptr, size_t len )
    {
        const char* cptr = (const char*)ptr;
        json jsn;
        jsn.parse_obj(0, cptr, cptr+len);
        return jsn;
    }

    static json load_file( const std::string& path )
    {
        int fd = open(path.c_str(), O_RDONLY);
        SCOPED_GUARD { close(fd); };

        json_assert(fd, "could not open file");

        struct stat st;
        json_assert(fstat(fd, &st) == 0, "could not get file size");

        void* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE|MAP_NORESERVE, fd, 0);
        SCOPED_GUARD { munmap(ptr, st.st_size); };

        json_assert(ptr, "could not load file contents");
        return json::load_buf(ptr, st.st_size);
    }

    static json load_file2( const std::string& path )
    {
        json jsn;

        std::fstream is;
        is.open(path, std::ios::in);
        json_assert(is, "could not read file: '%s'", path.c_str());

        std::istream_iterator<char> end;
        std::istream_iterator<char> beg(is);
        jsn.parse_obj(0, beg, end);
        return jsn;
    }

    json()
        : m_nums()
        , m_strs()
        , m_objs()
        , m_arrays()
    {
        m_objs.emplace_back(*this);
    }

    json ( json&& mv )
        : m_nums( std::move(mv.m_nums) )
        , m_strs( std::move(mv.m_strs) )
        , m_objs( std::move(mv.m_objs) )
        , m_arrays( std::move(mv.m_arrays) )
    {}

    json& operator= ( json&& mv )
    {
        m_nums = std::move(mv.m_nums);
        m_strs = std::move(mv.m_strs);
        m_objs = std::move(mv.m_objs);
        m_arrays = std::move(mv.m_arrays);
        return *this;
    }

    json( const json& ) = delete;
    json& operator= ( const json& ) = delete;

    ~json() {}

    jobject& root() { return m_objs.front(); }
    const jobject& root() const { return m_objs.front(); }
    std::ostream& write( std::ostream& os, size_t val, int depth);

protected:
    template < typename T >
    void parse_array( size_t, T& beg, T end );

    template < typename T >
    void parse_obj( size_t idx, T& beg, T end );

    size_t push_back( const std::string& str )
    {
        size_t idx = m_strs.size();
        m_strs.push_back(str);
        return idx;
    }

    size_t push_back( number num )
    {
        size_t idx = (uint32_t)m_nums.size();
        m_nums.push_back(num);
        return idx;
    }

    std::vector<number> m_nums;
    std::vector<std::string> m_strs;
    std::vector<jobject> m_objs;
    std::vector<jarray> m_arrays;
};


//------------------------------------------------------------------------------
class value
{
public:
    value( json& j, uint32_t val )
        : m_json(j)
        , m_val(val)
    {}

//    value( const value& ) = delete;
//    value& operator= ( const value& ) = delete;

    operator int() const { return (int)this->operator number(); }
    operator integer() const { return (integer)this->operator number(); }
    operator float() const { return (float)this->operator number(); }
    operator number() const
    {
        json_assert( is_num(), "value is not a number" );
        return m_json.m_nums[(m_val&0xFFFFFF)];
    }

    operator const std::string&() const
    {
        json_assert( is_str(), "value is not a string" );
        return m_json.m_strs[(m_val&0xFFFFFF)];
    }

    operator bool() const
    {
        json_assert( is_bool(), "value is not a boolean" );
        return ((m_val>>24) & 0xFF) == json::TRUE;
    }

    operator const jobject&() const
    {
        json_assert( is_obj(), "value is not an object" );
        return m_json.m_objs[(m_val&0xFFFFFF)];
    }

    operator const jarray&() const
    {
        json_assert( is_array(), "value is not an array" );
        return m_json.m_arrays[(m_val&0xFFFFFF)];
    }

    bool is_null() const { return type() == json::NIL; }
    bool is_num() const { return type() == json::NUMBER; }
    bool is_str() const { return type() == json::STRING; }
    bool is_obj() const { return type() == json::OBJECT; }
    bool is_array() const { return type() == json::ARRAY; }
    bool is_bool() const { auto t = type(); return t == json::TRUE || t == json::FALSE; }
    int type() const { return (m_val >> 24)&0xFF; }

protected:
    json& m_json;
    uint32_t m_val;
};

//------------------------------------------------------------------------------
inline jobject::iterator::vpair jobject::iterator::operator*() const
{
    auto key = m_iter->first;
    auto idx = m_iter->second;
    return std::make_pair(m_parent.m_json.m_strs[key], value(m_parent.m_json, idx));
}

//------------------------------------------------------------------------------
inline std::ostream& operator<< ( std::ostream& os, const value& val )
{
    uint32_t type = val.type();
    switch(type)
    {
        case json::NIL:
            os << "null";
            break;

        case json::NUMBER:
            os << (number)val;
            break;

        case json::INTEGER:
            os << (integer)val;
            break;

        case json::STRING:
            os << '"' << (const std::string&)val << '"';
            break;

        case json::TRUE:
            os << "true";
            break;

        case json::FALSE:
            os << "false";
            break;

        case json::OBJECT:
            static_cast<jobject>(val).write(os);
            break;

        case json::ARRAY:
            static_cast<jarray>(val).write(os);
            break;

        default:
            break;
    }
    return os;
}

//------------------------------------------------------------------------------
inline std::ostream& write_tabs ( std::ostream& os, size_t cnt )
{
    for ( auto i = 0; i < cnt; i++ )
    {
        os << "    ";
    }
    return os;
}

//------------------------------------------------------------------------------
inline std::ostream& json::write( std::ostream& os, size_t val, int depth)
{
    uint32_t type = (val >> 24) & 0xFF;
    uint32_t idx = val & 0xFFFFFF;
    switch(type)
    {
        case json::NIL:
            os << "null";
            break;

        case json::NUMBER:
        case json::INTEGER:
            os << m_nums[idx];
            break;

        case json::STRING:
            os << '"' << m_strs[idx] << '"';
            break;

        case json::TRUE:
            os << "true";
            break;

        case json::FALSE:
            os << "false";
            break;

        case json::OBJECT:
            m_objs[idx].write(os, depth+1);
            break;

        case json::ARRAY:
            m_arrays[idx].write(os, depth+1);
            break;

        default:
            break;
    }

    return os;
}

//------------------------------------------------------------------------------
inline std::ostream& jarray::write( std::ostream& os, int depth ) const
{
    auto it = m_items.begin();
    if ( it == m_items.end() )
        return os;

    os << '[' << std::endl;

    write_tabs(os, depth+1);
    m_json.write(os, *it, depth);

    for ( ++it ; it != m_items.end(); ++it )
    {
        os << ',' << std::endl;
        write_tabs(os, depth+1);
        m_json.write(os, *it, depth);
    }

    os << std::endl;
    write_tabs(os, depth);
    os << ']';
    return os;
}

//------------------------------------------------------------------------------
inline std::ostream& jobject::write( std::ostream& os, int depth ) const
{
    auto it = this->begin();
    auto end = this->end();
    if (it == end)
        return os;

    os << '{' << std::endl;
    write_tabs(os, depth+1);
    os << '"' << (*it).first << '"' << ": " << (*it).second;

    for ( ++it; it != end; ++it )
    {
        os << ',' << std::endl;
        write_tabs(os, depth+1);
        os << '"' << (*it).first << '"' << ": " << (*it).second;
    }
    os << std::endl;
    write_tabs(os, depth);
    os << '}';
    return os;
}

//------------------------------------------------------------------------------
inline std::ostream& operator<< ( std::ostream& os, const json& j )
{
    return j.root().write(os, 0);
}

//------------------------------------------------------------------------------
inline void jarray::push_back ( double d )
{
    size_t idx = m_json.push_back(d);
    m_items.push_back( (json::NUMBER<<24)|(idx&0xFFFFFF) );
}

//------------------------------------------------------------------------------
inline void jarray::push_back ( const std::string& s )
{
    size_t idx = m_json.push_back(s);
    m_items.push_back( (json::STRING<<24)|(idx&0xFFFFFF) );
}

//------------------------------------------------------------------------------
inline void jarray::push_back ( bool b )
{
    m_items.push_back( (b?json::TRUE:json::FALSE)<<24 );
}

//------------------------------------------------------------------------------
inline jobject& jobject::create_obj( const std::string& _key )
{
    size_t key = m_json.push_back(_key);
    size_t idx = m_json.m_objs.size();
    m_keys.emplace_back( keyval(key, (json::OBJECT<<24) | (idx&0xFFFFFF)) );

    json& j = m_json;
    j.m_objs.emplace_back(m_json);
    return j.m_objs.back();
}

//------------------------------------------------------------------------------
inline jarray& jobject::create_array( const std::string& _key )
{
    size_t key = m_json.push_back(_key);
    size_t idx = m_json.m_arrays.size();
    m_keys.emplace_back( keyval(key, (json::ARRAY<<24) | (idx&0xFFFFFF)) );

    json& j = m_json;
    j.m_arrays.emplace_back(m_json);
    return j.m_arrays.back();
}

//------------------------------------------------------------------------------
inline void jobject::add( const std::string& _key, number num )
{
    size_t key = m_json.push_back(_key);
    size_t idx = m_json.push_back(num);
    m_keys.emplace_back( keyval(key, (json::NUMBER<<24) | (idx&0xFFFFFF)) );
}

//------------------------------------------------------------------------------
inline void jobject::add( const std::string& _key, bool b )
{
    size_t key = m_json.push_back(_key);
    m_keys.emplace_back( keyval(key, (b?json::TRUE:json::FALSE)<<24) );
}

//------------------------------------------------------------------------------
inline void jobject::add( const std::string& _key, const std::string& str )
{
    size_t key = m_json.push_back(_key);
    size_t idx = m_json.push_back(str);
    m_keys.emplace_back( keyval(key, (json::STRING<<24) | (idx&0xFFFFFF)) );
}

//------------------------------------------------------------------------------
inline void jobject::add( const std::string& _key, std::nullptr_t n )
{
    size_t key = m_json.push_back(_key);
    m_keys.emplace_back( keyval(key, (json::NIL<<24)) );
}

//------------------------------------------------------------------------------
inline jobject& jarray::create_obj()
{
    size_t idx = m_json.m_objs.size();
    m_items.push_back((uint32_t)idx);

    json& j = m_json;
    j.m_objs.emplace_back(j);
    return j.m_objs.back();
}

//------------------------------------------------------------------------------
inline jarray& jarray::create_array()
{
    size_t idx = m_json.m_arrays.size();
    m_items.push_back((uint32_t)idx);

    json& j = m_json;
    j.m_arrays.emplace_back(j);
    return j.m_arrays.back();
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
inline number parse_sign( T& beg, T end )
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
static number __ignore = 0;
template < typename T >
inline number parse_digits( T& beg, T end, number& places = __ignore)
{
    json_assert(beg != end, "unexpected end of file");

    number num = 0;
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
                places = std::pow(10, std::distance(beg, start));
                return num;
            }
        }
    }

    json_assert(false, "unexpected end of file");
    return num;
}


//------------------------------------------------------------------------------
template < typename T >
inline number parse_number( T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");

    // +/-
    number sign = parse_sign(beg, end);

    // whole number
    number num = parse_digits(beg, end);

    // fraction
    switch (*beg)
    {
        case '.':
        {
            number places = 1;
            number fract = parse_digits(++beg, end, places);
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
            number esign = parse_sign(++beg, end);
            number digits = parse_digits(beg, end);
            num *= std::pow(10, esign*digits);
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

//------------------------------------------------------------------------------
static inline void utf8_encode(int32_t codepoint, jstr& str )
{
    json_assert(codepoint > 0, "invalid unicode");
    if(codepoint < 0x80)
    {
        str += (char)codepoint;
    }
    else if(codepoint < 0x800)
    {
        str += 0xC0 + ((codepoint & 0x7C0) >> 6);
        str += 0x80 + ((codepoint & 0x03F));
    }
    else if(codepoint < 0x10000)
    {
        str += 0xE0 + ((codepoint & 0xF000) >> 12);
        str += 0x80 + ((codepoint & 0x0FC0) >> 6);
        str += 0x80 + ((codepoint & 0x003F));
    }
    else if(codepoint <= 0x10FFFF)
    {
        str += 0xF0 + ((codepoint & 0x1C0000) >> 18);
        str += 0x80 + ((codepoint & 0x03F000) >> 12);
        str += 0x80 + ((codepoint & 0x000FC0) >> 6);
        str += 0x80 + ((codepoint & 0x00003F));
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
inline void parse_unicode( jstr& str, T& beg, T end )
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
inline jstr parse_string( T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");
    json_assert(*beg == '"', "not a valid string");

    jstr str;

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
                        str += '/';
                        break;
                    case 'b':
                        str += '\b';
                        break;
                    case 'f':
                        str += '\f';
                        break;
                    case 'n':
                        str += '\n';
                        break;
                    case 'r':
                        str += '\r';
                        break;
                    case 't':
                        str += '\t';
                        break;
                    case 'u':
                        parse_unicode(str, beg, end);
                        break;
                    case '"':
                        str += '\"';
                        break;
                    case '\\':
                        str += '\\';
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
                        return str;

                    default:
                        str += ch;
                        break;
                }
                break;
            }
        }
        prev = ch;
    }

    json_assert(false, "string terminated unexpectedly");
    return jstr();
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
inline void json::parse_array( size_t idx, T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");
    while (beg++ != end)
    {
        eat_whitespace(beg, end);
        switch ( *beg )
        {
            case '{': // object
            {
                size_t nidx = m_objs.size();
                m_arrays[idx].create_obj();
                parse_obj(nidx, beg, end);
                break;
            }

            case '[': // array
            {
                size_t nidx = m_arrays.size();
                m_arrays[idx].create_array();
                parse_array(nidx, beg, end);
                break;
            }

            case '"': // string
                m_arrays[idx].push_back(parse_string(beg, end));
                break;

            case 't': // true
                parse_true(beg, end);
                m_arrays[idx].push_back(true);
                break;

            case 'f': // false
                parse_false(beg, end);
                m_arrays[idx].push_back(false);
                break;

            case 'n': // null
                parse_null(beg, end);
                m_arrays[idx].push_back(nullptr);
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
                m_arrays[idx].push_back(parse_number(beg, end));
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
inline void json::parse_obj( size_t idx, T& beg, T end )
{
    json_assert(beg != end, "unexpected end of file");
    while (beg++ != end)
    {
        eat_whitespace(beg, end);
        jstr key = parse_string(beg, end);

        eat_whitespace(beg, end);
        json_assert(*beg == ':', "invalid character '%c', following key, expected: ':'", *beg); ++beg;
        eat_whitespace(beg, end);

        switch ( *beg )
        {
            case '{': // object
            {
                size_t nidx = m_objs.size();
                m_objs[idx].create_obj(key);
                parse_obj(nidx, beg, end);
                break;
            }

            case '[': // array
            {
                size_t nidx = m_arrays.size();
                m_objs[idx].create_array(key);
                parse_array(nidx, beg, end);
                break;
            }

            case '"': // string
                m_objs[idx].add(key, parse_string(beg, end));
                break;

            case 't': // true
                parse_true(beg, end);
                m_objs[idx].add(key, true);
                break;

            case 'f': // false
                parse_false(beg, end);
                m_objs[idx].add(key, false);
                break;

            case 'n': // null
                parse_null(beg, end);
                m_objs[idx].add(key, nullptr);
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
                m_objs[idx].add(key, parse_number(beg, end));
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

#endif /* defined(__MMapJson__json__) */
