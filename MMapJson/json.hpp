//
//  json.hpp
//  MMapJson
//
//  Created by Brian Howard on 5/7/15.
//  Copyright (c) 2015 InMotion Software, LLC. All rights reserved.
//

#ifndef MMapJson_json_hpp
#define MMapJson_json_hpp

#include <string>
extern "C"
{
    #include "json2.h"
}

namespace ims
{
    class val
    {
    public:
    protected:
    };

    class obj
    {
        friend class json;
        friend class array;
    public:

        class iterator
        {
        public:
            iterator()
                : m_idx(-1)
            {}

            iterator& operator++ (int)
            {
                m_idx++;
                return *this;
            }

            iterator operator++ ()
            {
                iterator it;
                m_idx++;
                return it;
            }

        protected:
            int m_idx;
        };


        class setter
        {
        public:
            setter( const char* key, obj& obj )
                : m_key(key)
                , m_obj(obj)
            {}

            void operator= ( const std::string& str ) { jobj_add_strl(m_obj, m_key, str.c_str(), str.length()); }
            void operator= ( const char* str ) { jobj_add_str(m_obj, m_key, str); }
            void operator= ( jnum_t n ) { jobj_add_num(m_obj, m_key, n); }
            void operator= ( bool b ) { jobj_add_bool(m_obj, m_key, b); }
            void operator= ( std::nullptr_t ) { jobj_add_nil(m_obj, m_key); }

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
                m_obj.add(key, t);
            }

            template < typename T, typename... ARGS >
            void _add( obj o, const char* key, const T& t, const ARGS&... args )
            {
                m_obj.add(key, t);
                _add(o, args...);
            }

            const char* m_key;
            obj& m_obj;
        };

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

        setter operator[] ( const char* key ) { return setter(key, *this); }

        obj add_obj( const char* key ) { return jobj_add_obj(m_obj, key); }
    protected:

        obj( jobj_t o )
            : m_obj(o)
        {}

        operator const jobj_t () const { return m_obj; }
        operator jobj_t () { return m_obj; }

        jobj_t m_obj;
    };

    class array
    {
        friend class json;
        friend class obj;
    public:

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

        obj push_obj() { return jarray_add_obj(m_array); }
        array push_array() { return jarray_add_array(m_array); }

        template < typename T >
        void add( const T& t )
        {
            push_back(t);
        }

        template < typename T, typename... ARGS >
        void add( const T& t, const ARGS&... args )
        {
            add(t, args...);
        }

    protected:

        array( jarray_t a )
            : m_array(a)
        {}

        operator const jarray_t () const { return m_array; }
        operator jarray_t () { return m_array; }

        jarray_t m_array;
    };

    array obj::setter::add_array() { return jobj_add_array(m_obj, m_key); }

    template < typename... ARGS >
    class array obj::setter::add_array( const ARGS&... args )
    {
        array a = add_array();
        _add(a, args...);
        return a;
    }

    template < typename T >
    void obj::setter::_add( array a, const T& t )
    {
        a.push_back(t);
    }

    template < typename T, typename... ARGS >
    void obj::setter::_add( array a, const T& t, const ARGS&... args )
    {
        a.push_back(t);
        _add(a, args...);
    }

    class json
    {
    public:
        json()
            : m_jsn(nullptr)
        {
            m_jsn = json_new();
        }

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

        json& operator= ( json&& mv )
        {
            json_free(m_jsn);
            m_jsn = mv.m_jsn;
            mv.m_jsn = json_new();
            return *this;
        }

        obj root() const { return json_root(m_jsn); }

    protected:
        json_t* m_jsn;
    };

}

#endif
