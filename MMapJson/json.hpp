//
//  json.hpp
//  MMapJson
//
//  Created by Brian Howard on 5/7/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#ifndef MMapJson_json_hpp
#define MMapJson_json_hpp

#if __cplusplus

#include "json.h"
#include <string>
#include <iostream>
#include <vector>
#include <map>

namespace ims
{
    //--------------------------------------------------------------------------
    class obj
    {
        friend class json;
        friend class array;
        friend class val;
        friend class const_val;
    public:

        class iterator
        {
        public:
            explicit iterator( const obj& o, size_t idx = std::numeric_limits<size_t>::max())
                : m_idx(idx)
                , m_obj(o)
            {}

            bool operator!= ( const iterator& it ) const { return !this->operator==(it); }
            bool operator== ( const iterator& it ) const { return it.m_idx == m_idx && m_obj == it.m_obj; }
            iterator operator++ (int) { return iterator(m_obj, m_idx++); }
            iterator& operator++ () { m_idx++; return *this; }
            class std::pair<std::string, class const_val> operator*() const;

        protected:
            const obj& m_obj;
            size_t m_idx;
        };

        bool operator== ( const obj& o ) const { return m_obj.json == o.m_obj.json && m_obj.idx == o.m_obj.idx; }
        bool operator!= ( const obj& o ) const { return !this->operator==(o); }
        iterator begin() const { return iterator(*this, 0); }
        iterator end() const { return iterator(*this, this->size()); }

        class setter
        {
        public:
            explicit setter( const char* key, obj& obj )
                : m_key(key)
                , m_obj(obj)
            {}

            void operator= ( const std::string& str ) { jobj_add_strl(m_obj, m_key, str.c_str(), str.length()); }
            void operator= ( const char* str ) { jobj_add_str(m_obj, m_key, str); }
            void operator= ( jnum_t n ) { jobj_add_num(m_obj, m_key, n); }
            void operator= ( int i ) { jobj_add_num(m_obj, m_key, i); }
            void operator= ( size_t i ) { jobj_add_num(m_obj, m_key, i); }
            void operator= ( bool b ) { jobj_add_bool(m_obj, m_key, b); }
            void operator= ( std::nullptr_t ) { jobj_add_nil(m_obj, m_key); }
            void operator= ( const class val& v );

            class array add_array();

            template < typename... ARGS >
            class array add_array( const ARGS&... args );

            obj add_obj() { return jobj_add_obj(m_obj, m_key); }

            template < typename T, typename... ARGS >
            obj add_obj( const char* key, const T& t, const ARGS&... args )
            {
                obj o = add_obj();
                _add(o, key, t, args...);
                return o;
            }

        protected:
            template < typename T >
            void _add( class array, const T& t );

            template < typename T, typename... ARGS >
            void _add( class array, const T& t, const ARGS&... args );

            template < typename T >
            void _add( obj o, const char* key, const T& t )
            {
                o.add(key, t);
            }

            template < typename T, typename... ARGS >
            void _add( obj o, const char* key, const T& t, const ARGS&... args )
            {
                o.add(key, t);
                _add(o, args...);
            }

            const char* m_key;
            obj& m_obj;
        };

        bool empty() const { return size() == 0; }
        size_t size() const { return jobj_len(m_obj); }

        obj& add ( const char* key, const std::string& str )
        {
            jobj_add_strl(m_obj, key, str.c_str(), str.length());
            return *this;
        }

        obj& add ( const char* key, const char* str )
        {
            jobj_add_str(m_obj, key, str);
            return *this;
        }

        obj& add ( const char* key, int n )
        {
            jobj_add_num(m_obj, key, n);
            return *this;
        }

        obj& add ( const char* key, jnum_t n )
        {
            jobj_add_num(m_obj, key, n);
            return *this;
        }

        obj& add ( const char* key, bool b )
        {
            jobj_add_bool(m_obj, key, b);
            return *this;
        }

        obj& add ( const char* key, std::nullptr_t )
        {
            jobj_add_nil(m_obj, key);
            return *this;
        }

        iterator find( const std::string& str )
        {
            size_t idx = jobj_findl_idx(m_obj, str.c_str(), str.length());
            if (idx == SIZE_T_MAX)
            {
                return end();
            }
            return iterator(*this, idx);
        }

        setter operator[] ( const std::string& key ) { return setter(key.c_str(), *this); }
        setter operator[] ( const char* key ) { return setter(key, *this); }
        obj add_obj( const char* key ) { return jobj_add_obj(m_obj, key); }
        class array add_array( const char* key );

        friend std::ostream& operator<< ( std::ostream& os, const obj& o );

    protected:

        obj( jobj_t o )
            : m_obj(o)
        {}

        operator const jobj_t () const { return m_obj; }
        operator jobj_t () { return m_obj; }

        jobj_t m_obj;
    };

    //--------------------------------------------------------------------------
    class array
    {
        friend class json;
        friend class obj;
        friend class val;
        friend class const_val;
    public:

        class iterator
        {
        public:

            iterator(const array& a, size_t idx = std::numeric_limits<size_t>::max())
                : m_array(a)
                , m_idx(idx)
            {}

            bool operator!= ( const iterator& it ) const { return !this->operator==(it); }
            bool operator== ( const iterator& it ) const { return it.m_idx == m_idx && m_array == it.m_array; }
            iterator operator++ (int) { return iterator(m_array, m_idx++); }
            iterator& operator++ () { m_idx++; return *this; }
            class const_val operator*() const;

        protected:
            const array& m_array;
            size_t m_idx;
        };

        obj push_obj() { return jarray_add_obj(m_array); }
        array push_array() { return jarray_add_array(m_array); }

        template < typename T >
        void add( const T& t ) { push_back(t); }

        template < typename T, typename... ARGS >
        void add( const T& t, const ARGS&... args ) { push_back(t); add(args...); }

        iterator begin() const { return iterator(*this, 0); }
        iterator end() const { return iterator(*this, size()); }
        bool operator== ( const array& a ) const { return m_array.json == a.m_array.json && m_array.idx == a.m_array.idx; }
        size_t size() const { return jarray_len(m_array); }

        array& push_back( jnum_t n )
        {
            jarray_add_num(m_array, n);
            return *this;
        }

        array& push_back( std::nullptr_t )
        {
            jarray_add_nil(m_array);
            return *this;
        }

        array& push_back( const char* s )
        {
            jarray_add_str(m_array, s);
            return *this;
        }

        array& push_back( const val& val );

        friend std::ostream& operator<< ( std::ostream& os, const array& a );

    protected:

        array( jarray_t a )
            : m_array(a)
        {}

        operator const jarray_t () const { return m_array; }
        operator jarray_t () { return m_array; }

        jarray_t m_array;
    };

    //--------------------------------------------------------------------------
    array obj::add_array( const char* key ) { return jobj_add_array(m_obj, key); }

    //--------------------------------------------------------------------------
    array obj::setter::add_array() { return jobj_add_array(m_obj, m_key); }

    //--------------------------------------------------------------------------
    template < typename... ARGS >
    class array obj::setter::add_array( const ARGS&... args )
    {
        array a = add_array();
        _add(a, args...);
        return a;
    }

    //--------------------------------------------------------------------------
    template < typename T >
    void obj::setter::_add( array a, const T& t )
    {
        a.push_back(t);
    }

    //--------------------------------------------------------------------------
    template < typename T, typename... ARGS >
    void obj::setter::_add( array a, const T& t, const ARGS&... args )
    {
        a.push_back(t);
        _add(a, args...);
    }

    //--------------------------------------------------------------------------
    class json
    {
        friend class val;
        friend class const_val;
    public:
        static json from_file( const std::string& path )
        {
            // Error handling uses C++ style exceptions
            json_t* jsn = json_new();
            jerr_t err;
            if (json_load_path(jsn, path.c_str(), &err) != 0)
            {
                throw std::runtime_error(err.msg);
            }
            return json(jsn);
        }

        json()
            : json(nullptr)
        {}

        ~json()
        {
            json_free(m_jsn);
        }

        json( const json& ) = delete;
        json& operator= ( const json& ) = delete;

        json( json&& mv )
            : m_jsn(mv.m_jsn)
        {
            mv.m_jsn = json_new();
        }

        bool operator!() const { return empty(); }
        operator bool () const { return !empty(); }
        bool empty() const { return m_jsn == nullptr || root().empty(); }

        json& operator= ( json&& mv )
        {
            json_free(m_jsn);
            m_jsn = mv.m_jsn;
            mv.m_jsn = json_new();
            return *this;
        }

        obj root() const { return json_root(m_jsn); }

        std::string str( int flags = JPRINT_PRETTY ) const
        {
            std::string str;
            json_print(m_jsn, flags, [](void* ctx, const char* cstr)
            {
                std::string& str = *(std::string*)ctx;
                str += cstr;
            }, &str);
            return std::move(str);
        }

        friend std::ostream& operator<< ( std::ostream& os, const json& j );

    protected:
        json( json_t* j )
            : m_jsn(j)
        {
            if (!m_jsn) m_jsn = json_new();
        }

        json_t* m_jsn;
    };

    //--------------------------------------------------------------------------
    static inline void ostream_write( void* ctx, const char* str )
    {
        *((std::ostream*)ctx) << str;
    }

    //--------------------------------------------------------------------------
    class val
    {
        friend class obj::setter;
        friend class array;
    public:
        typedef std::vector<val> array;
        typedef std::map<std::string, val> obj;

        val ()
            : m_type(JTYPE_NIL)
        {}

        val ( bool b )
            : m_type(b ? JTYPE_TRUE : JTYPE_FALSE)
        {}

        val ( int n )
            : m_type(JTYPE_NUM)
            , m_num(n)
        {}

        val ( jnum_t n )
            : m_type(JTYPE_NUM)
            , m_num(n)
        {}

        val ( std::nullptr_t )
            : m_type(JTYPE_NIL)
        {}

        val ( const char* cstr )
            : m_type(JTYPE_STR)
            , m_str(cstr)
        {}

        val ( const std::string& str )
            : m_type(JTYPE_STR)
            , m_str(str)
        {}

        val ( const val::array& array )
            : m_type(JTYPE_ARRAY)
            , m_array(array)
        {}

        val ( const val::obj& obj )
            : m_type(JTYPE_OBJ)
            , m_obj(obj)
        {}

        val ( val::array&& array )
            : m_type(JTYPE_ARRAY)
            , m_array(std::move(array))
        {}

        val ( val::obj&& obj )
            : m_type(JTYPE_OBJ)
            , m_obj(std::move(obj))
        {}

        val ( std::string&& str )
            : m_type(JTYPE_STR)
            , m_str(std::move(str))
        {}

        ~val()
        {
            switch (m_type)
            {
                case JTYPE_OBJ:
                    m_obj.~obj();
                    break;
                case JTYPE_STR:
                    m_str.~basic_string();
                    break;
                case JTYPE_ARRAY:
                    m_array.~array();
                    break;

                default:
                    break;
            }
            m_type = JTYPE_NIL;
        }

        val( val&& mv )
            : m_type(mv.m_type)
        {
            switch (mv.m_type)
            {
                case JTYPE_OBJ:
                    new (this) val(std::move(mv.m_obj));
                    break;

                case JTYPE_STR:
                    new (this) val(std::move(mv.m_str));
                    break;

                case JTYPE_ARRAY:
                    new (this) val(std::move(mv.m_array));
                    break;

                case JTYPE_NUM:
                    new (this) val(mv.m_num);
                    break;

                case JTYPE_NIL:
                    new (this) val();

                case JTYPE_FALSE:
                    new (this) val(false);
                    break;

                case JTYPE_TRUE:
                    new (this) val(true);
                    break;

                default:
                    break;
            }
            mv.m_type = JTYPE_NIL;
        }

        val( const val& v )
            : m_type(v.m_type)
        {
            switch (v.m_type)
            {
                case JTYPE_OBJ:
                    new (this) val(v.m_obj);
                    break;

                case JTYPE_STR:
                    new (this) val(v.m_str);
                    break;

                case JTYPE_ARRAY:
                    new (this) val(v.m_array);
                    break;

                case JTYPE_NUM:
                    new (this) val(v.m_num);
                    break;

                case JTYPE_NIL:
                    new (this) val();
                    break;

                case JTYPE_FALSE:
                    new (this) val(false);
                    break;

                case JTYPE_TRUE:
                    new (this) val(true);
                    break;

                default:
                    break;
            }
        }

        int type() const { return m_type; }

    protected:
        int m_type;
        union
        {
            std::string m_str;
            jnum_t m_num;
            val::array m_array;
            val::obj m_obj;
        };
    };

    //--------------------------------------------------------------------------
    array& array::push_back( const val& v )
    {
        switch (v.type())
        {
            case JTYPE_OBJ:
            {
                auto obj = this->push_obj();
                for ( const auto& pair : v.m_obj )
                {
                    obj[pair.first] = pair.second;
                }
                break;
            }

            case JTYPE_STR:
                push_back(v.m_str);
                break;

            case JTYPE_ARRAY:
            {
                auto array = this->push_array();
                for ( const auto& val : v.m_array )
                {
                    array.push_back(val);
                }
                break;
            }

            case JTYPE_NUM:
                push_back(v.m_num);
                break;

            case JTYPE_NIL:
                push_back(nullptr);
                break;

            case JTYPE_FALSE:
                push_back(false);
                break;

            case JTYPE_TRUE:
                push_back(true);
                break;

            default:
                break;
        }
        return *this;
    }

    //--------------------------------------------------------------------------
    void obj::setter::operator= ( const class val& v )
    {
        switch (v.type())
        {
            case JTYPE_OBJ:
            {
                auto obj = this->add_obj();
                for ( const auto& pair : v.m_obj )
                {
                    obj[pair.first] = pair.second;
                }
                break;
            }

            case JTYPE_STR:
                this->operator=(v.m_str);
                break;

            case JTYPE_ARRAY:
            {
                auto array = this->add_array();
                for ( const auto& val : v.m_array )
                {
                    array.push_back(val);
                }
                break;
            }

            case JTYPE_NUM:
                this->operator=(v.m_num);
                break;

            case JTYPE_NIL:
                this->operator=(nullptr);
                break;

            case JTYPE_FALSE:
                this->operator=(false);
                break;

            case JTYPE_TRUE:
                this->operator=(true);
                break;

            default:
                break;
        }
    }

    //--------------------------------------------------------------------------
    class const_val
    {
    public:
        const_val( json_t* jsn, jval_t val )
            : m_jsn(jsn)
            , m_val(val)
        {}

        bool is_bool() const { return jval_is_bool(m_val); }
        bool is_false() const { return jval_is_false(m_val); }
        bool is_true() const { return jval_is_true(m_val); }
        bool is_nil() const { return jval_is_nil(m_val); }
        bool is_str() const { return jval_is_str(m_val); }
        bool is_num() const { return jval_is_num(m_val); }
        bool is_obj() const { return jval_is_obj(m_val); }
        bool is_array() const { return jval_is_array(m_val); }
        bool operator! ( ) const { return is_nil(); }

        operator int() const { return (int)json_get_num(m_jsn, m_val); }
        operator jnum_t() const { return json_get_num(m_jsn, m_val); }
        operator const char*() const { return json_get_str(m_jsn, m_val); }
        operator bool() const { return json_get_bool(m_jsn, m_val); }
        operator obj() const { return json_get_obj(m_jsn, m_val); }
        operator array() const { return json_get_array(m_jsn, m_val); }
        operator std::string () const
        {
            const char* cstr =  json_get_str(m_jsn, m_val);
            return (cstr) ? cstr : std::string();
        }

        int type() const { return jval_type(m_val); }

        friend std::ostream& operator<< ( std::ostream& os, const const_val& val );

    protected:
        json_t* m_jsn;
        jval_t m_val;
    };

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const obj& o )
    {
        jobj_print(o, JPRINT_PRETTY, ostream_write, &os);
        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const array& a )
    {
        jarray_print(a, JPRINT_PRETTY, ostream_write, &os);
        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const const_val& val )
    {
        jval_print(val.m_jsn, val.m_val, JPRINT_PRETTY, ostream_write, &os);
        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const json& jsn )
    {
        if (jsn)
        {
            json_print(jsn.m_jsn, JPRINT_PRETTY, ostream_write, &os);
        }
        return os;
    }

    //--------------------------------------------------------------------------
    class const_val array::iterator::operator*() const
    {
        jval_t val = jarray_get(m_array, m_idx);
        json_t* jsn = jobj_get_json(m_array.m_array);
        return const_val(jsn, val);
    }

    //--------------------------------------------------------------------------
    class std::pair<std::string, class const_val> obj::iterator::operator*() const
    {
        jval_t val;
        const char* key = jobj_get(m_obj, m_idx, &val);
        json_t* jsn = jobj_get_json(m_obj.m_obj);
        return std::make_pair(key, const_val(jsn, val) );
    }
}

#endif // __cplusplus
#endif // MMapJson_json_hpp
