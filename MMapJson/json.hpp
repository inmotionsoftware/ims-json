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
#include <iostream>

extern "C"
{
    #include "json2.h"
}

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

        setter operator[] ( const char* key ) { return setter(key, *this); }
        obj add_obj( const char* key ) { return jobj_add_obj(m_obj, key); }
        class array add_array( const char* key );
        std::ostream& write ( std::ostream& os, size_t depth = 0 ) const;

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

        std::ostream& write ( std::ostream& os, size_t depth = 0 ) const;
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
            return json(json_load_file(path.c_str()));
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

    protected:
        json( json_t* j )
            : m_jsn(j)
        {
            if (!m_jsn) m_jsn = json_new();
        }

        json_t* m_jsn;
    };

    //--------------------------------------------------------------------------
    class const_val
    {
    public:
        const_val( json_t* jsn, jval_t val )
            : m_jsn(jsn)
            , m_val(val)
        {}

        bool is_nil() const { return jval_is_nil(m_val); }
        bool is_str() const { return jval_is_str(m_val); }
        bool is_num() const { return jval_is_num(m_val); }
        bool is_obj() const { return jval_is_obj(m_val); }
        bool is_array() const { return jval_is_array(m_val); }
        bool operator! ( ) const { return is_nil(); }

        operator int() const { return json_get_num(m_jsn, m_val); }
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

        std::ostream& write(std::ostream& os, size_t depth = 0 ) const
        {
            switch (type())
            {
                case JTYPE_NIL:
                    os << "null";
                    break;
                case JTYPE_STR:
                    os << (const char*)*this;
                    break;
                case JTYPE_NUM:
                    os << (jnum_t)*this;
                    break;
                case JTYPE_ARRAY:
                    ((array)*this).write(os, depth);
                    break;
                case JTYPE_OBJ:
                    ((obj)*this).write(os, depth);
                    break;
                case JTYPE_TRUE:
                    os << "true";
                    break;
                case JTYPE_FALSE:
                    os << "false";
                    break;
                default:
                    break;
            }
            return os;
        }

    protected:
        json_t* m_jsn;
        jval_t m_val;
    };

    //--------------------------------------------------------------------------
    static inline std::ostream& write_tabs( std::ostream& os, size_t n )
    {
        while (n--)
        {
            os << "    ";
        }
        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& array::write ( std::ostream& os, size_t depth ) const
    {
        os << '{';
        for ( auto it = this->begin(), next = it, end = this->end(); it != end; ++it )
        {
            os << std::endl;
            write_tabs(os, depth+1);

            const auto& val = (*it);
            val.write(os, depth+1);
            if (++next != end) os << ',';
        }
        os << std::endl;
        write_tabs(os, depth);
        os << '}';

        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const obj& o )
    {
        return o.write(os);
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const array& a )
    {
        return a.write(os);
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const const_val& v )
    {
        return v.write(os);
    }

    //--------------------------------------------------------------------------
    std::ostream& operator<< ( std::ostream& os, const json& j )
    {
        if (j) j.root().write(os);
        return os;
    }

    //--------------------------------------------------------------------------
    std::ostream& obj::write ( std::ostream& os, size_t depth ) const
    {
        os << '{';
        for ( auto it = this->begin(), next = it, end = this->end(); it != end; ++it )
        {
            os << std::endl;
            write_tabs(os, depth+1);

            const auto& pair = (*it);
            os << '"' << pair.first << "\": ";
            pair.second.write(os, depth+1);

            if (++next != end) os << ',';
        }

        os << std::endl;
        write_tabs(os, depth);
        os << '}';
        
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

#endif
