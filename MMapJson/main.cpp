//
//  main.cpp
//  MMapJson
//
//  Created by Brian Howard on 4/8/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#include <iostream>
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

//------------------------------------------------------------------------------
#define json_assert(A, STR, ...) { if (!(A)) {printf(STR "\n", ## __VA_ARGS__ ); abort();} }

//------------------------------------------------------------------------------
class value parse_value( const char*& beg, const char* end );
void copy_char( std::ostream& os, const char* str, size_t n );


//------------------------------------------------------------------------------
class jstr
{
public:

    jstr()
        : jstr(nullptr, (size_t)0)
    {}

    jstr( const char* cstr, size_t len, bool copy )
        : jstr(cstr, len)
    {
        m_free = true;
    }

    jstr( const char* cstr, bool copy )
        : jstr(cstr, cstr ? strlen(cstr) : 0)
    {
        m_free = true;
    }

    jstr( const char* cstr )
        : jstr(cstr, cstr ? strlen(cstr) : 0)
    {}

    jstr ( const char* ptr, size_t len )
        : m_ptr(ptr)
        , m_len(len)
        , m_free(false)
    {}

    jstr( const jstr& k )
    {
        if (k.m_free)
        {
            if (k.m_len > 0)
            {
                char* ch = (char*)malloc(k.m_len);
                for ( auto i = 0; i < k.m_len; i++ )
                {
                    ch[i] = k.m_ptr[i];
                }
                m_ptr = ch;
            }
            else
            {
                m_ptr = nullptr;
            }
        }
        else
        {
            m_ptr = k.m_ptr;
        }
        m_free = k.m_free;
        m_len = k.m_len;
    }

    jstr( jstr&& jstr )
    {
        m_free = jstr.m_free;
        m_ptr = jstr.m_ptr;
        m_len = jstr.m_len;

        jstr.m_free = false;
        jstr.m_ptr = nullptr;
        jstr.m_len = 0;
    }

    ~jstr()
    {
        if (m_free)
        {
            delete m_ptr;
        }
    }

    int compare ( const jstr& k )
    {
        auto len = std::min(m_len, k.m_len);
        for ( auto i = 0; i < len; i++ )
        {
            if (m_ptr[i] != k.m_ptr[i])
            {
                return m_ptr[i] - k.m_ptr[i];
            }
        }

        if (m_len != k.m_len)
        {
            return (int)(m_len - k.m_len);
        }

        return 0;
    }

    bool operator== ( const jstr& k ) const
    {
        if (m_len != k.m_len) return false;
        if (m_ptr == k.m_ptr) return true;

        for ( auto i = 0; i < m_len; i++ )
        {
            if (m_ptr[i] != k.m_ptr[i])
            {
                return false;
            }
        }
        return true;
    }

    bool operator < ( const jstr& k ) const
    {
        auto len = std::min(k.m_len, m_len);
        for ( auto i = 0; i < len; i++ )
        {
            if (m_ptr[i] < k.m_ptr[i])
            {
                return true;
            }
            else if (m_ptr[i] > k.m_ptr[i])
            {
                return false;
            }
        }
        return m_len < k.m_len;
    }

    bool operator> ( const jstr& k ) const
    {
        auto len = std::min(k.m_len, m_len);
        for ( auto i = 0; i < len; i++ )
        {
            if (m_ptr[i] > k.m_ptr[i])
            {
                return true;
            }
            else if (m_ptr[i] < k.m_ptr[i])
            {
                return false;
            }
        }
        return m_len > k.m_len;
    }

    bool operator!= ( const jstr& k ) const { return !this->operator==(k); }
    bool operator>= ( const jstr& k ) const { return !this->operator<(k); }
    bool operator<= ( const jstr& k ) const { return !this->operator>(k); }

    size_t size() const { return m_len; }
    bool empty() const { return m_len == 0; }

    char operator[] ( size_t i ) const { return m_ptr[i]; }

protected:
    bool m_free;
    size_t m_len;
    const char* m_ptr;
};


//------------------------------------------------------------------------------
class strbldr
{
public:
    strbldr( size_t cap = 0)
        : m_cap(0)
        , m_len(0)
        , m_ptr(nullptr)
    {
        reserve(cap);
    }

    strbldr( strbldr&& move )
        : m_ptr( move.m_ptr )
        , m_len( move.m_len )
        , m_cap( move.m_cap )
    {
        move.m_ptr = nullptr;
        move.m_len = 0;
        move.m_cap = 0;
    }

    strbldr( const strbldr& copy ) = delete;
    strbldr& operator=( const strbldr& copy ) = delete;

    ~strbldr()
    {
        free(m_ptr);
    }

    void reserve( size_t cap )
    {
        if (m_len+cap <= m_cap) return;
        m_cap = (size_t)std::max( (m_cap*2+1), (m_len+cap)*2+1);
        m_ptr = (char*)realloc(m_ptr, m_cap);
    }

    strbldr& operator+= ( char ch )
    {
        reserve(1);
        m_ptr[m_len++] = ch;
        return *this;
    }

    jstr move()
    {
        char* ptr = m_ptr;
        size_t len = m_len;
        m_ptr = nullptr;
        m_len = 0;
        m_cap = 0;
        return jstr(ptr, len, true);
    }

protected:
    size_t m_cap;
    size_t m_len;
    char* m_ptr;

};

//------------------------------------------------------------------------------
std::ostream& operator << ( std::ostream& os, const jstr& k )
{
    for ( auto i = 0; i < k.size(); i++ )
    {
        os << k[i];
    }
    return os;
}

// types
//------------------------------------------------------------------------------
typedef int64_t integer;
typedef double number;
typedef std::vector<class value> array;
typedef std::map<jstr, class value> object;

// value
//------------------------------------------------------------------------------
class value
{
public:
    enum type
    {
        NIL = 0,
        BOOLEAN,
        STRING,
        NUMBER,
        INTEGER,
        ARRAY,
        OBJECT
    };

    value()
        : m_type(NIL)
    {}

    constexpr value( bool b )
        : m_type(BOOLEAN)
        , m_bool(b)
    {}

    constexpr value( int i )
        : m_type(INTEGER)
        , m_int(i)
    {}

    constexpr value( integer i )
        : m_type(INTEGER)
        , m_int(i)
    {}

    constexpr value( float f )
        : m_type(NUMBER)
        , m_num(f)
    {}

    constexpr value( number f )
        : m_type(NUMBER)
        , m_num(f)
    {}

    value( const jstr& k )
        : m_type(STRING)
        , m_str(k)
    {}

    value( const array& a )
        : m_type(ARRAY)
        , m_array(a)
    {}

    value( const object& o )
        : m_type(OBJECT)
        , m_obj(o)
    {}

    value( jstr&& k )
        : m_type(STRING)
        , m_str(std::move(k))
    {}

    value( array&& a )
        : m_type(ARRAY)
        , m_array(std::move(a))
    {}

    value( object&& o )
        : m_type(OBJECT)
        , m_obj(std::move(o))
    {}

    value ( value&& move )
    {
        switch(move.m_type)
        {
            case STRING:
                new (this) value(std::move(move.m_str));
                break;

            case ARRAY:
                new (this) value(std::move(move.m_array));
                break;

            case OBJECT:
                new (this) value(std::move(move.m_obj));
                break;

            case BOOLEAN:
                new (this) value(move.m_bool);
                break;

            case NUMBER:
                new (this) value(move.m_num);
                break;

            case INTEGER:
                new (this) value(move.m_int);
                break;

            case NIL:
            default:
                m_type = NIL;
                break;
        }
        move.m_type = NIL;
    }

    value( const value& copy )
    {
        switch(copy.m_type)
        {
            case STRING:
                new (this) value(copy.m_str);
                break;

            case ARRAY:
                new (this) value(copy.m_array);
                break;

            case OBJECT:
                new (this) value(copy.m_obj);
                break;

            case BOOLEAN:
                new (this) value(copy.m_bool);
                break;

            case NUMBER:
                new (this) value(copy.m_num);
                break;
            
            case INTEGER:
                new (this) value(copy.m_int);
                break;

            case NIL:
            default:
                m_type = NIL;
                break;
        }
    }

    ~value()
    {
        switch(m_type)
        {
            case STRING:
                m_str.~jstr();
                break;

            case ARRAY:
                m_array.~array();
                break;

            case OBJECT:
                m_obj.~object();
                break;

            case NIL:
            case BOOLEAN:
            case NUMBER:
            case INTEGER:
            default:
                break;
        }

        m_type = NIL;
    }

    value& operator= ( value&& move )
    {
        this->~value();
        new (this) value( std::move(move) );
        return *this;
    }

    value& operator= ( const value& copy )
    {
        this->~value();
        new (this) value(copy);
        return *this;
    }

    int type() const { return m_type; }

    operator number () const
    {
        switch (m_type)
        {
            case INTEGER:
                return (number)m_int;

            case NUMBER:
                return m_num;

            default:
                check_type(NUMBER);
                return 0;
        }
    }

    operator integer () const
    {
        switch (m_type)
        {
            case INTEGER:
                return m_int;

            case NUMBER:
                return (integer)m_num;

            default:
                check_type(INTEGER);
                return 0;
        }
    }

    operator const jstr& () const
    {
        check_type(STRING);
        return m_str;
    }

    operator const array& () const
    {
        check_type(ARRAY);
        return m_array;
    }

    operator const object& () const
    {
        check_type(OBJECT);
        return m_obj;
    }

    operator bool () const
    {
        switch(m_type)
        {
            case STRING:
                return !m_str.empty();

            case ARRAY:
                return !m_array.empty();

            case OBJECT:
                return !m_obj.empty();


            case BOOLEAN:
                return m_bool;

            case NUMBER:
                return m_num != 0;

            case INTEGER:
                return m_int != 0;

            case NIL:
            default:
                return false;
        }
    }

    std::ostream& write( std::ostream& os, int depth = 0 ) const
    {
        static const char TAB[] = "   ";

        switch (this->type())
        {
            case value::STRING:
                os << '"' << m_str << '"' << std::endl;
                break;

            case value::INTEGER:
                os << m_int << std::endl;
                break;

            case value::NUMBER:
                os << m_num << std::endl;
                break;

            case value::OBJECT:
            {
                os << '{' << std::endl;
                for ( const auto& pair : m_obj )
                {
                    copy_char(os, TAB, depth+1);
                    os << '"' << pair.first << "\": " ;
                    pair.second.write(os, depth+1);
                }
                copy_char(os, TAB, depth);
                os << '}' << std::endl;
                break;
            }

            case value::ARRAY:
            {
                os << '[' << std::endl;;
                for ( const auto& val : m_array )
                {
                    copy_char(os, TAB, depth+1);
                    val.write(os);
                }
                copy_char(os, TAB, depth);
                os << ']' << std::endl;;
                break;
            }

            case value::BOOLEAN:
                os << (m_bool?"true":"false") << std::endl;;
                break;

            case value::NIL:
            default:
                os << "null" << std::endl;;
                break;
        }

        return os;
    }

protected:

    void check_type( int type ) const { json_assert (type == m_type, "invalid type"); }

    int m_type;
    union
    {
        integer m_int;
        number m_num;
        bool m_bool;
        jstr m_str;
        array m_array;
        object m_obj;
    };
};

//------------------------------------------------------------------------------
class json
{
public:

    static json load_file( const std::string& path )
    {
        json jsn;

        int fd = open(path.c_str(), O_RDONLY);
        if (!fd)
            return jsn;

        struct stat st;
        if (fstat(fd, &st) != 0)
        {
            close(fd);
            return jsn;
        }

        const char* ptr = (char*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (!ptr)
        {
            return jsn;
        }

        jsn.m_mmap = ptr;
        jsn.m_root = parse_value(ptr, ptr+st.st_size);
        return jsn;
    }

    json()
        : m_mmap(nullptr)
        , m_mlen(0)
        , m_root()
    {}

    json( json&& move )
        : m_root( std::move(move.m_root) )
        , m_mlen( move.m_mlen )
        , m_mmap( move.m_mmap )
    {
        move.m_mmap = nullptr;
        move.m_mlen = 0;
    }

    ~json()
    {
        if (m_mmap)
        {
            munmap((void*)m_mmap, m_mlen);
        }
    }

    value& root() { return m_root; }
    const value& root() const { return m_root; }

protected:
    size_t m_mlen;
    const char* m_mmap;
    value m_root;
};

//------------------------------------------------------------------------------
std::ostream& operator << ( std::ostream& os, const value& v )
{
    return v.write(os);
}

//------------------------------------------------------------------------------
std::ostream& operator << ( std::ostream& os, const json& jsn )
{
    return os << jsn.root();
}

//------------------------------------------------------------------------------
void copy_char( std::ostream& os, const char* str, size_t n )
{
    while (n--)
    {
        os << str;
    }
}

//------------------------------------------------------------------------------
static inline void eat_whitespace( const char*& beg, const char* end )
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
static inline number parse_sign( const char*& beg, const char* end )
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
static inline number parse_digits( const char*& beg, const char* end, number& places = __ignore)
{
    json_assert(beg != end, "unexpected end of file");

    number num = 0;
    const char* start = beg;
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
                return num;
        }
    }

    places = std::pow(10, beg - start);
    return num;
}


//------------------------------------------------------------------------------
static inline value parse_number( const char*& beg, const char* end )
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
static inline array parse_array( const char*& beg, const char* end )
{
    json_assert(beg != end, "unexpected end of file");
    array a;

    while (beg++ != end)
    {
        eat_whitespace(beg, end);
        a.push_back( parse_value(beg, end) );
        eat_whitespace(beg, end);

        // either another or the end
        switch (*beg)
        {
            case ']':
                beg++;
                return a;

            case ',':
                break;

            default:
                json_assert(false, "");
        }
    }

    json_assert(false, "unexpected end of stream while reading array");
    return a;
}

//------------------------------------------------------------------------------
static inline unsigned char char_to_hex(const char*& beg, const char* end)
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
static inline int utf8_encode(int32_t codepoint, strbldr& str )
{
    if(codepoint < 0)
    {
        return -1;
    }
    else if(codepoint < 0x80)
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
        return -1;
    }

    return 0;
}

//------------------------------------------------------------------------------
static inline unsigned int read_unicode_hex(const char*& beg, const char* end)
{
    return  char_to_hex(beg, end) << 12 |
            char_to_hex(beg, end) << 8 |
            char_to_hex(beg, end) << 4 |
            char_to_hex(beg, end);
}

//------------------------------------------------------------------------------
static inline void parse_unicode( strbldr& ss, const char*& beg, const char* end )
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
        utf8_encode(unicode, ss);
        return;
    }

    json_assert(0xDC00 > val || val > 0xDFFF, "invalid unicode");
    utf8_encode(val, ss);
}

//------------------------------------------------------------------------------
static inline jstr parse_string( const char*& beg, const char* end )
{
    json_assert(beg != end, "unexpected end of file");
    json_assert(*beg == '"', "not a valid string");

    strbldr str;

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
                        return str.move();

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
//
//    const char* ch = ++beg;
//    for (; beg != end && *beg != '"'; ++beg) {}
//    ++beg;
//    size_t slen = beg-ch;
//    return jstr(ch, slen);
}

//------------------------------------------------------------------------------
static inline object parse_obj( const char*& beg, const char* end )
{
    json_assert(beg != end, "unexpected end of file");
    object obj;

    while (beg++ != end)
    {
        eat_whitespace(beg, end);
        jstr key = parse_string(beg, end);

        eat_whitespace(beg, end);
        json_assert(*beg == ':', "invalid character '%c', following key, expected: ':'", *beg); ++beg;
        eat_whitespace(beg, end);

        obj.emplace( std::move(key), parse_value(beg, end));
        eat_whitespace(beg, end);

        // either another or the end
        switch (*beg)
        {
            case '}': // end of object
                ++beg;
                return obj;

            case ',': // another key
                break;

            default:
                json_assert(false, "unexpected character: '%c'", *beg);
                break;
        }
    }

    json_assert(false, "unexpected end of file");
    return obj;
}

//------------------------------------------------------------------------------
static inline value parse_true( const char*& beg, const char* end )
{
    json_assert( (end-beg) >= 4, "unexpected end of file");
    json_assert(*++beg == 'r' &&
                *++beg == 'u' &&
                *++beg == 'e',
        "expected literal 'true'");
    ++beg;
    return value(true);
}

//------------------------------------------------------------------------------
static inline value parse_false( const char*& beg, const char* end ){
    json_assert( (end-beg) >= 5, "unexpected end of file");
    json_assert(*++beg == 'a' &&
                *++beg == 'l' &&
                *++beg == 's' &&
                *++beg == 'e',
        "expected literal 'true'");
    ++beg;
    return value(false);
}

//------------------------------------------------------------------------------
static inline value parse_null( const char*& beg, const char* end )
{
    json_assert( (end-beg) >= 4, "unexpected end of file");
    json_assert(*++beg == 'u' &&
                *++beg == 'l' &&
                *++beg == 'l',
        "expected literal 'true'");
    ++beg;
    return value();
}

//------------------------------------------------------------------------------
value parse_value( const char*& beg, const char* end )
{
    if (beg == end)
        return value();

    eat_whitespace(beg, end);
    switch ( *beg )
    {
        case '{': // object
            return parse_obj(beg, end);

        case '[': // array
            return parse_array(beg, end);

        case '"': // string
            return parse_string(beg, end);

        case 't': // true
            return parse_true(beg, end);

        case 'f': // false
            return parse_false(beg, end);

        case 'n': // null
            return parse_null(beg, end);

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
            return parse_number(beg, end);

        default:
            json_assert(false, "invalid character: '%c'", *beg);
            break;
    }

    return value();
}

//------------------------------------------------------------------------------
int main(int argc, const char * argv[])
{
//    const char* path = "/Users/bghoward/Projects/Emblazed-NextGen/IMSFoundation/sample.json";

    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/citylots.json";
//    const char* path = "/Users/bghoward/Projects/MMapJson/MMapJson/test.json";
//    const char* path = "/Users/bghoward/Dropbox/Project Sluice/external data/gz_2010_us_050_00_500k.json";
//    const char* path = "/Users/bghoward/Downloads/Newtonsoft.Json-6.0.8/Src/Newtonsoft.Json.Tests/large.json";

    // mmap a file
    // parse into json structure

    clock_t start = clock();
    json doc = json::load_file(path);
    clock_t diff = clock() - start;
    printf("completed in: %f secs\n", diff / (double)CLOCKS_PER_SEC);

//    std::cout << doc << std::endl;

    return 0;
}
