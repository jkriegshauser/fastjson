#ifndef FASTJSON_HPP_INCLUDED
#define FASTJSON_HPP_INCLUDED

// Copyright (C) 2013 Joshua M. Kriegshauser
// Version 1.0
// Revision $DateTime: 2013/09/21 18:24:00 $
//! \file fastjson.hpp This file contains the fastjson parser

// Disable warnings for MS VC++
#ifdef _MSC_VER
    #pragma warning (push)
    #pragma warning (disable : 4127) // Conditional expression is constant
    #pragma warning (disable : 4702) // Unreachable code
#endif

// If no standard library, user must provide required functions and typedefs
#if !defined(FASTJSON_NO_STDLIB)
    #include <cassert>
    #include <new>
    #include <cmath>
    #include <sstream>
#endif

#if defined(FASTJSON_NO_EXCEPTIONS)

#define FASTJSON_PARSE_ERROR(what, where) do { parse_error_handler(what, where); abort(); } while (0)

namespace fastjson
{
    //! When exceptions are disabled with FASTJSON_NO_EXCEPTIONS,
    //! this function is called to notify user about the error.
    //! It must be defined by the user.
    //! <br><br>
    //! This function cannot return. If it does, the results are undefined.
    //! <br><br>
    //! Example of a very simple definition:
    //! <pre>
    //! void %fastjson::%parse_error_handler(const char* what, void* where)
    //! {
    //!     std::cout << "Parse error: " << what << "\n";
    //!     std::abort();
    //! }
    //! </pre>
    //! \param what Human-readable error description
    //! \param what Pointer to character data where error was detected
    void parse_error_handler(const char* what, void* where);
}

#else

#include <exception> // For std::exception

#define FASTJSON_PARSE_ERROR(what, where) throw parse_error(what, where)

namespace fastjson
{
    //! Parse error exception
    //! This exception is thrown by the parser when an error occurs.
    //! Use the what() function to get the human-readable error description.
    //! Use the where() function to get a pointer to position within source text where the error
    //! was detected.
    //! <br><br>
    //! If throwing exceptions is undesirable, exceptions can be disabled
    //! by using #define FASTJSON_NO_EXCEPTIONS before fastjson.hpp is included.
    //! This will use the parse_error_handler function instead of throwing an exception.
    //! The parse_error_handler function must be defined by the user.
    //! <br><br>
    //! This class derives from <code>std::exception</code>.
    class parse_error : public std::exception
    {
    public:
        //! Constructor
        parse_error(const char* what, void* where)
            : what_(what)
            , where_(where)
        {}

        //! Gets the human-readable description of the error
        virtual const char* what() const throw()
        {
            return what_;
        }

        //! Gets the pointer to the character data where the error happened.
        //! Ch should be the same as the character type of json_document that produced
        //! the error.
        //! \return Pointer to location within the parsed string where the error occurred.
        template<class Ch> Ch* where() const
        {
            return reinterpret_cast<Ch*>(where_);
        }

    private:
        const char* what_;
        void* where_;
    };
}

#endif

///////////////////////////////////////////////////////////////////////////////
// Pool size definitions

#if !defined(FASTJSON_STATIC_POOL_SIZE)
    //! Size of the static memory block of memory_pool
    //! Define FASTJSON_STATIC_POOL_SIZE before including fastjson.hpp if you want to override the default value.
    //! The default value is 32 Kbytes.
    //! No dynamic allocations are performed until the static pool is exhausted.
    //! Value may be set to zero to disable static pool.
    #define FASTJSON_STATIC_POOL_SIZE (32 * 1024)
#endif

#if !defined(FASTJSON_DYNAMIC_POOL_SIZE)
    //! Size of the dynamic memory block of memory_pool
    //! Define FASTJSON_DYNAMIC_POOL_SIZE before including fastjson.hpp if you want to override the default value.
    //! The default value is 32 Kbytes.
    //! Dynamic memory blocks of this size are allocated when necessary, and only after the static memory pool is exhausted.
    #define FASTJSON_DYNAMIC_POOL_SIZE (32 * 1024)
#endif

#if !defined(FASTJSON_ALIGNMENT)
    //! Memory allocation alignment
    //! Define FASTJSON_ALIGNMENT before including fastjson.hpp if you want to override the default value.
    //! The default value is pointer-size alignment.
    //! Must be a power of two and at least 1, otherwise memory_pool will not work.
    #define FASTJSON_ALIGNMENT (sizeof(void*))
#endif

///////////////////////////////////////////////////////////////////////////////
// Parser implementation

namespace fastjson
{

    // Forward declarations and typedefs
    typedef unsigned char byte;
    template<class Ch, class T_POOL> class json_document;
    template<class Ch> class json_value;
    template<class Ch> class json_object;

    //! Enumeration of value types produced by the parser
    enum value_type
    {
        value_null,
        value_bool,
        value_number,
        value_string,
        value_array,
        value_object,
    };

    namespace internal
    {
        template<int Dummy>
        struct lookup_tables
        {
            static const bool lookup_whitespace[256];
            static const bool lookup_digit[256];
            static const double lookup_double[10];
            static const char lookup_hexchar[16];
        };

        template<class Ch>
        Ch* nullstr() throw()
        {
            static Ch nul = Ch('\0');
            return &nul;
        }

        template<class Ch>
        json_value<Ch>* nullval() throw();

        template<class Ch>
        std::size_t length(const Ch* p) throw()
        {
            const Ch* end = p;
            if (end)
            {
                while (*end != Ch('\0'))
                    ++end;
                return (std::size_t)(end - p);
            }
            return 0;
        }

        template<class Ch>
        int compare(const Ch* first, const Ch* second, const Ch* secondend) throw()
        {
            assert(first); assert(second); assert(secondend);
            while (*first && second != secondend)
            {
                if (*first < *second) return -1;
                else if (*second < *first) return 1;
                ++first, ++second;
            }
            if (*first && second == secondend) return 1;
            else if (*first == Ch('\0') && second != secondend) return -1;
            return 0;
        }

        template<class Ch>
        byte hex_value(Ch* text)
        {
            Ch ch = *text;
            switch (ch)
            {
            case Ch('0'): case Ch('1'): case Ch('2'): case Ch('3'): case Ch('4'):
            case Ch('5'): case Ch('6'): case Ch('7'): case Ch('8'): case Ch('9'):
                return ((byte)ch) - '0';

            case Ch('a'): case Ch('A'): return byte(10);
            case Ch('b'): case Ch('B'): return byte(11);
            case Ch('c'): case Ch('C'): return byte(12);
            case Ch('d'): case Ch('D'): return byte(13);
            case Ch('e'): case Ch('E'): return byte(14);
            case Ch('f'): case Ch('F'): return byte(15);

            default:
                FASTJSON_PARSE_ERROR("Expected hex character (0-9, a-f, A-F)", text);
                return byte(0);
            }
        }

        template<class Ch>
        Ch hex_char(byte value)
        {
            return Ch(lookup_tables<0>::lookup_hexchar[value]);
        }

        template<class Ch>
        double value_to_number(const Ch* start, const Ch* end) throw()
        {
            if (start == end) return 0.0;

            double num = 0.0, fact = 1.0;
            if (*start == Ch('-'))
            {
                fact = -1.0;
                ++start;
            }

            bool period = false;
            while (start < end)
            {
                switch (*start)
                {
                case Ch('0'): case Ch('1'): case Ch('2'): case Ch('3'): case Ch('4'):
                case Ch('5'): case Ch('6'): case Ch('7'): case Ch('8'): case Ch('9'):
                    num *= 10.0;
                    num += lookup_tables<0>::lookup_double[*start - '0'];
                    if (period) fact /= 10.0;
                    ++start;
                    break;

                case Ch('.'):
                    if (!period) { period = true; ++start; }
                    else start = end; // Done.
                    break;

                case Ch('e'):
                case Ch('E'):
                    num *= fact;
                    if (++start < end)
                    {
                        bool neg = false;
                        if (*start == Ch('+') || *start == Ch('-'))
                        {
                            neg = (*start == Ch('-'));
                            ++start;
                        }

                        double exp = 0.0;
                        while (start < end)
                        {
                            switch (*start)
                            {
                            case Ch('0'): case Ch('1'): case Ch('2'): case Ch('3'): case Ch('4'):
                            case Ch('5'): case Ch('6'): case Ch('7'): case Ch('8'): case Ch('9'):
                                exp *= 10.0;
                                exp += lookup_tables<0>::lookup_double[*start - '0'];
                                ++start;
                                break;

                            default:
                                start = end; // Done
                            }
                        }
                        num *= pow(10.0, neg ? -exp : exp);
                    }
                    start = end; // Done
                    return num;

                default:
                    break;
                }
            }
            
            return num * fact;
        }

        union udouble
        {
            double d;
            unsigned long long ull;
            struct sdouble
            {
                const static unsigned Bias = 1023;
                const static unsigned NonFiniteExp = (1u << 11)-1;
                unsigned long long mantissa: 52;
                unsigned long long exponent: 11;
                unsigned long long sign: 1;
            } s;
        };

        template<class Ch>
        bool number_to_string(const double& val, Ch* buf, Ch*& bufend)
        {
            assert(buf < bufend);
            udouble u; u.d = val;
            const bool neg = (u.s.sign != 0);
            u.s.sign = 0;
            if (u.d < 1.0e-12) // Very small
            {
                // Zero
                *buf++ = Ch('0');
                *(bufend = buf) = Ch('\0');
                return true;
            }
            else if (u.s.exponent == udouble::sdouble::NonFiniteExp)
            {
                if (u.s.mantissa == 0)
                {
                    // Infinite. Convert to string.
                    if (neg) { *buf++ = Ch('-');  assert(buf < bufend); }
                    *buf++ = Ch('I'); assert(buf < bufend);
                    *buf++ = Ch('n'); assert(buf < bufend);
                    *buf++ = Ch('f'); assert(buf < bufend);
                    *(bufend = buf) = Ch('\0');
                }
                else
                {
                    // Not a number. Convert to string.
                    *buf++ = Ch('N'); assert(buf < bufend);
                    *buf++ = Ch('a'); assert(buf < bufend);
                    *buf++ = Ch('N'); assert(buf < bufend);
                    *(bufend = buf) = Ch('\0');
                }
                return false; // as a string
            }

            // Double to float is beyond the scope of fastjson. However, json is very clear about the rules.
            const bool scientific = u.d < 1.0e-9 || u.d > 1.0e12;
            char temp[128]; int len;
#if defined (_MSC_VER)
            len = ::sprintf_s<sizeof(temp)>(temp, scientific ? "%.12g" : "%.12f", neg ? -u.d : u.d);
#else
            len = ::snprintf(temp, sizeof(temp), scientific ? "%.12g" : "%.12f", neg ? -u.d : u.d);
#endif
            if (!scientific)
            {
                // Strip trailing zeros
                for (char* p = temp + len - 1; p > temp; --p)
                {
                    if (*p != '0') break;
                    *p = '\0';
                    --len;
                }
                if (temp[len-1] == '.') temp[--len] = '\0';
            }
            assert(len < (bufend - buf));
            for (char* p = temp; *p; ++p)
            {
                *buf++ = Ch(*p);
            }
            *(bufend = buf) = Ch('\0');
            return true;
        }

        template<class Ch>
        bool value_to_boolean(const Ch* start, const Ch* end) throw()
        {
            if (start == end) return false;
            if (start[0] == Ch('t') && start[1] == Ch('r') && start[2] == Ch('u') && start[3] == Ch('e')) return true;
            if (start[0] == Ch('f') && start[1] == Ch('a') && start[2] == Ch('l') && start[3] == Ch('s') && start[4] == Ch('e')) return false;
            return value_to_number(start, end) != 0.0;
        }

        template<class Ch>
        wint_t read_utf16(Ch*& ptr)
        {
            if (*ptr++ != Ch('\\')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 1);
            if (*ptr++ != Ch('u')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 2);
            wint_t c(0);
            // hex_value will throw if not a hex character
            c |= hex_value(ptr); c <<= 4; ++ptr;
            c |= hex_value(ptr); c <<= 4; ++ptr;
            c |= hex_value(ptr); c <<= 4; ++ptr;
            c |= hex_value(ptr); ++ptr;
            return c;
        }

        // utf16-to-utf? converter
        // Default implementation; generates compile error
        template<class Ch, std::size_t S = sizeof(Ch)>
        struct from_utf16 { };

        template<class Ch>
        struct from_utf16<Ch, 1> // utf-16 to utf-8
        {
            void operator () (wint_t c[2], Ch*& out)
            {
                if (c[0] <= 0x7f)
                {
                    *out++ = Ch(c[0]);
                }
                else if (c[0] <= 0x7ff)
                {
                    *out++ = Ch(0xc0 + ((c[0] >> 6) & 0x3f));
                    *out++ = Ch(0x80 + ((c[0]     ) & 0x3f));
                }
                else if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                {
                    // UTF-16 surrogate pair
                    std::size_t ch = std::size_t(c[0] & 0x3ff) << 10;
                    assert(c[1] >= 0xdc00 && c[1] <= 0xdfff);
                    ch |= (c[1] & 0x3ff);
                    ch += 0x10000;

                    *out++ = Ch(0xf0 + ((ch >> 18) & 0x07));
                    *out++ = Ch(0x80 + ((ch >> 12) & 0x3f));
                    *out++ = Ch(0x80 + ((ch >>  6) & 0x3f));
                    *out++ = Ch(0x80 + ((ch      ) & 0x3f));
                }
                else if (c[0] <= 0xffff)
                {
                    *out++ = Ch(0xe0 + ((c[0] >> 12) & 0x0f));
                    *out++ = Ch(0x80 + ((c[0] >>  6) & 0x3f));
                    *out++ = Ch(0x80 + ((c[0]      ) & 0x3f));
                }
            }
        };

        template<class Ch>
        struct from_utf16<Ch, 2> // utf-16 to utf-16
        {
            void operator () (wint_t c[2], Ch*& out)
            {
                *out++ = c[0];
                if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                {
                    *out++ = c[1];
                }
            }
        };

        template<class Ch>
        struct from_utf16<Ch, 4> // utf-16 to utf-32
        {
            void operator () (wint_t c[2], Ch*& out)
            {
                if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                {
                    // UTF-16 surrogate pair
                    std::size_t ch = std::size_t(c[0] & 0x3ff) << 10;
                    assert(c[1] >= 0xdc00 && c[1] <= 0xdfff);
                    ch |= (c[1] & 0x3ff);
                    ch += 0x10000;

                    assert(ch <= 0x10ffff);

                    *out++ = Ch(ch);
                }
                else
                {
                    *out++ = Ch(c[0]);
                }
            }
        };
    
    } // namespace internal



    ///////////////////////////////////////////////////////////////////////////
    // Parsing flags

    //! Parse flags which represent the default behavior of the parser.
    //! This is always zero, so that flags may be or'd together.
    //! See json_document::parse() function.
    const int parse_default = 0;

    const int parse_no_string_terminators = 1 << 0;

    const int parse_no_string_translation = 1 << 1;

    const int parse_non_destructive = parse_no_string_terminators | parse_no_string_translation;

    ///////////////////////////////////////////////////////////////////////////
    // Allocator interface

    //! \class default_allocator
    //! Uses new/delete to allocate pool memory
    class default_allocator
    {
    public:
        default_allocator() {}
        ~default_allocator() {}

        unsigned char* raw_heap_alloc(std::size_t size)
        {
            return new byte[size];
        }

        void raw_heap_free(byte* p)
        {
            delete[] p;
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Memory pool

    template<std::size_t static_size>
    class static_pool
    {
        byte static_pool_[static_size];
    protected:
        byte* static_pool_start() throw() { return static_pool_; }
        byte* static_pool_end() throw() { return static_pool_ + static_size; }
    };

    template <>
    class static_pool<0>
    {
    protected:
        byte* static_pool_start() throw() { return 0; }
        byte* static_pool_end() throw() { return 0; }
    };

    //! \class memory_pool
    template<class T_ALLOC = default_allocator, std::size_t static_size = FASTJSON_STATIC_POOL_SIZE, std::size_t dynamic_size = FASTJSON_DYNAMIC_POOL_SIZE>
    class memory_pool : public T_ALLOC, public static_pool<static_size>
    {
    public:
        //! Constructor
        memory_pool() throw() { init(); }
        ~memory_pool() { clear(); }

        void clear()
        {
            while (pool_ != static_pool_start())
            {
                byte* next = reinterpret_cast<header*>(align_forward(pool_))->next_pool_;
                raw_heap_free(pool_);
                pool_ = next;
            }
        }

        byte* alloc(std::size_t size)
        {
            if (size > (std::size_t)(end_ - next_))
            {
                // Need to alloc another pool
                pool_alloc(size);
            }
            assert(size <= (std::size_t)(end_ - next_));
            byte* allocd = next_;
            next_ = align_forward(allocd + size);
            assert(next_ <= end_);
            return allocd;
        }

    protected:
        void init() throw()
        {
            pool_ = static_pool_start();
            next_ = align_forward(pool_);
            end_ = static_pool_end();
            assert(next_ <= end_);
        }

        byte* align_forward(byte* data) throw()
        {
            return (byte*)((size_t)(data + (FASTJSON_ALIGNMENT-1)) & ~(FASTJSON_ALIGNMENT-1));
        }

        void pool_alloc(std::size_t minsize)
        {
            std::size_t size = dynamic_size;
            if (size < minsize) size = minsize;

            size += (FASTJSON_ALIGNMENT * 2);
            size += sizeof(header);

            byte* pool = raw_heap_alloc(size);
            if (pool == 0)
            {
                FASTJSON_PARSE_ERROR("Memory allocation failed", 0);
            }
            header* h = new (align_forward(pool)) header(pool_);
            pool_ = pool;
            next_ = align_forward((byte*)(h + 1));
            end_  = pool + size;
            assert(next_ < end_);
        }

    private:
        struct header
        {
            byte* next_pool_;
            header(byte* next_pool) : next_pool_(next_pool) {}
        };

        byte* pool_;
        byte* next_;
        byte* end_;
    };

    ///////////////////////////////////////////////////////////////////////////
    // json_value

    template<class Ch = char>
    class json_value
    {
        template<class Ch, class T_POOL> friend class json_document;
        template<class Ch> friend class json_object;

    public:
        json_value(value_type type = value_null, Ch* name = 0, Ch* nameend = 0) throw()
            : type_(type)
            , name_(name)
            , nameend_(nameend)
            , value_(internal::nullstr<Ch>())
            , valueend_(internal::nullstr<Ch>())
            , owner_(0)
            , prev_(0)
            , next_(0)
        {}
        virtual ~json_value() throw() {}

        value_type type() const throw() { return type_; }
        const Ch* name() const throw() { return name_; }
        const Ch* nameend() const throw() { return nameend_; }

        bool is_null() const throw() { return type_ == value_null; }
        bool is_boolean() const throw() { return type_ == value_bool; }
        bool is_number() const throw() { return type_ == value_number; }
        bool is_string() const throw() { return type_ == value_string; }
        bool is_array() const throw() { return type_ == value_array; }
        bool is_object() const throw() { return type_ == value_object; }

        const Ch* as_string() const throw() { return value_; }
        const Ch* as_string_end() const throw() { return valueend_; }
        double as_number() const { return internal::value_to_number(value_, valueend_); }
        bool   as_boolean() const { return internal::value_to_bool(value_, valueend_); }

        json_object<Ch>* as_array() throw() { return is_array()|is_object() ? static_cast<json_object<Ch>*>(this) : 0; }
        const json_object<Ch>* as_array() const throw() { return is_array()|is_object() ? static_cast<const json_object<Ch>*>(this) : 0; }
        json_object<Ch>* as_object() throw() { return is_array()|is_object() ? static_cast<json_object<Ch>*>(this) : 0; }
        const json_object<Ch>* as_object() const throw() { return is_array()|is_object() ? static_cast<const json_object<Ch>*>(this) : 0; }

        const json_value<Ch>* next_sibling() const throw() { return next_; }

    protected:
        value_type type_;
        Ch* name_;
        Ch* nameend_;
        Ch* value_;
        Ch* valueend_;
        json_object<Ch>* owner_;
        json_value<Ch>* prev_;
        json_value<Ch>* next_;
    };

    template<class Ch = char>
    class json_object : public json_value<Ch>
    {
        template<class Ch, class T_POOL> friend class json_document;

    public:
        json_object(value_type type = value_null, Ch* name = 0, Ch* nameend = 0) throw()
            : json_value<Ch>(type, name, nameend)
            , child_(0)
            , lastchild_(0)
            , numchildren_(0)
        {}

        size_t num_children() const throw() { return numchildren_; }
        const json_value<Ch>* first_child() const throw() { return child_; }

        const json_value<Ch>& operator [] (const Ch* name) const throw()
        {
            assert(type_ == value_object);
            const json_value<Ch>* p = child_;
            while (p)
            {
                if (internal::compare(name, p->name_, p->nameend_) == 0)
                {
                    return *p;
                }
                p = p->next_;
            }
            return *internal::nullval<Ch>();
        }

        const json_value<Ch>& operator [] (int index) const throw()
        {
            const json_value<Ch>* p = child_;
            while (index-- > 0 && p)
            {
                p = p->next_;
            }
            return index == 0 ? *p : internal::nullval<Ch>();
        }

        bool array_add(json_value<Ch>* val) throw()
        {
            if (!is_array() || val == 0) return false;
            add_child(val);
            return true;
        }

        bool array_insert(json_value<Ch>* val, int index) throw()
        {
            assert(val->owner_ == 0);
            if (!is_array() || val == 0 || val->owner_ != 0) return false;
            json_value<Ch>** pp = &child_;
            while (index-- > 0 && *pp)
            {
                pp = &(*pp)->next_;
            }

            val->next_ = *pp;
            if (val->next_) val->next_->prev_ = val; else lastchild_ = val;
            val->prev_ = *pp ? (*pp)->prev_ : 0;
            *pp = val;
            ++numchildren_;
            return true;
        }

        json_value<Ch>* array_remove(int index) throw()
        {
            if (!is_array()) return 0;
            json_value<Ch>* p = child_;
            while (index-- > 0 && p)
            {
                p = p->next_;
            }

            if (p)
            {
                if (p->prev_) p->prev_->next_ = p->next_; else child_ = p->next_;
                if (p->next_) p->next_->prev_ = p->prev_; else lastchild_ = p->prev_;
                p->owner_ = 0;
                --numchildren_;
                return p;
            }
            return 0;
        }

        bool object_set(const Ch* name, json_value<Ch>* val) throw()
        {
            if (!is_object()) return false;
            if (name == 0 || *name == Ch('\0')) return false;
            assert(val->owner_ == 0); // Shouldn't have an owner
            val->name_ = name;
            val->nameend_ = name + internal::length(name);
            // If there's an existing value by this name, then replace it.
            json_value<Ch>** pp = &child_;
            while (*pp)
            {
                if (internal::compare(name, (*pp)->name_, (*pp)->nameend_) == 0)
                {
                    // Replace the node *pp with val
                    val->prev_ = (*pp)->prev_;
                    val->next_ = (*pp)->next_;
                    if (val->next_) val->next_->prev_ = val;
                    *pp = val;
                    break;
                }
                pp = &((*pp)->next_);
            }
            if (*pp == 0)
            {
                add_child(child);
            }
            return true;
        }

        json_value<Ch>* object_remove(const Ch* name) throw()
        {
            if (!is_object()) return 0;
            if (name == 0 || *name == Ch('\0')) return false;
            json_value<Ch>* p = child_;
            while (p)
            {
                if (internal::compare(name, p->name_, p->nameend_) == 0)
                {
                    if (p->prev_) p->prev_->next_ = p->next_; else child_ = p->next_;
                    if (p->next_) p->next_->prev_ = p->prev_; else lastchild_ = p->prev_;
                    p->owner_ = 0;
                    --numchildren_;
                    return p;
                }
                p = p->next_;
            }
            return 0;
        }

        void remove_all() throw()
        {
            json_value<Ch>* p = child_;
            while (p)
            {
                p->owner_ = 0;
                p->prev_ = p->next_ = 0;
            }
            numchildren_ = 0;
            child_ = lastchild_ = 0;
        }

    private:
        void add_child(json_value<Ch>* child) throw()
        {
            assert(child->owner_ == 0); // Shouldn't have an owner
            child->owner_ = this;
            child->prev_ = child->next_ = 0;
            if (child_ == 0)
            {
                child_ = lastchild_ = child;
            }
            else
            {
                child->prev_ = lastchild_;
                lastchild_->next_ = child;
                lastchild_ = child;
            }
            ++numchildren_;
        }

        json_value<Ch>* child_;
        json_value<Ch>* lastchild_;
        size_t numchildren_;
    };

    ///////////////////////////////////////////////////////////////////////////
    // json_document

    template<class Ch = char, class T_POOL = memory_pool<> >
    class json_document : protected T_POOL
    {
    public:
        //! Constructor
        json_document()
            : root_(allocate_object(value_object, internal::nullstr<Ch>(), internal::nullstr<Ch>()))
        {}

        template<int Flags> void parse(Ch* text)
        {
            assert(text);

            skip<whitespace_pred, Flags>(text);

            // Must encounter an object or an array
            if (*text == Ch('{'))
            {
                root_ = parse_object<Flags>(allocate_object(), ++text);
            }
            else if (*text == Ch('['))
            {
                root_ = parse_array<Flags>(allocate_object(), ++text);
            }
            else
            {
                FASTJSON_PARSE_ERROR("Expected '{' or '['", text);
            }
        }

        const json_object<Ch>& root() const throw() { return *root_; }

        json_value<Ch>* allocate_value()
        {
            return new (T_POOL::alloc(sizeof(json_value<Ch>))) json_value<Ch>;
        }

        json_value<Ch>* allocate_value(value_type type, Ch* name, Ch* nameend)
        {
            return new (T_POOL::alloc(sizeof(json_value<Ch>))) json_value<Ch>(type, name, nameend);
        }

        json_object<Ch>* allocate_object()
        {
            return new (T_POOL::alloc(sizeof(json_object<Ch>))) json_object<Ch>;
        }

        json_object<Ch>* allocate_object(value_type type, Ch* name, Ch* nameend)
        {
            assert(type == value_object || type == value_array);
            return new (T_POOL::alloc(sizeof(json_object<Ch>))) json_object<Ch>(type, name, nameend);
        }

        Ch* allocate_string(const Ch* str)
        {
            if (str)
            {
                std::size_t len = internal::length(str);
                Ch* newstr = (Ch*)T_POOL::alloc(sizeof(Ch) * (len + 1));
                Ch* p = newstr;
                while (*str)
                {
                    *p = *str;
                    ++p, ++str;
                }
                return newstr;
            }
            return nullstr<Ch>();
        }

        json_value<Ch>* allocate_null_value(const Ch* name)
        {
            json_value<Ch>* pval = allocate_value(value_null, name, name + internal::length(name));
            pval->value_ = &nulltext[0];
            pval->valueend_ = &nulltext[4];
            return pval;
        }

        json_value<Ch>* allocate_bool_value(const Ch* name, bool val)
        {
            json_value<Ch>* pval = allocate_value(value_bool, name, name + internal::length(name));
            if (val)
            {
                pval->value_ = &truetext[0];
                pval->valueend_ = &truetext[4];
            }
            else
            {
                pval->value_ = &falsetext[0];
                pval->valueend_ = &falsetext[5];
            }
            return pval;
        }

        json_value<Ch>* allocate_string_value(const Ch* name, const Ch* value)
        {
            json_value<Ch>* pval = allocate_value(value_string, name, name + internal::length(name));
            pval->value_ = value;
            pval->valueend_ = value + internal::length(value);
            return pval;
        }

        json_value<Ch>* allocate_number_value(const Ch* name, double val)
        {
            json_value<Ch>* pval = allocate_value(value_number, name, name + internal::length(name));

            Ch buffer[128];
            if (!internal::number_to_string(val, buffer, buffer + 128))
            {
                // Actually a string due to infinite or NaN
                pval->type_ = value_string;
            }

            Ch* sval = allocate_string(buffer);
            pval->value_ = sval;
            pval->valueend_ = sval + internal::length(sval);
            return pval;
        }

    private:
        struct whitespace_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return internal::lookup_tables<0>::lookup_whitespace[(byte)ch];
            }
        };

        struct digit_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return internal::lookup_tables<0>::lookup_digit[(byte)ch];
            }
        };

        template<class StopPred, int Flags>
        static void skip(Ch*& text)
        {
            StopPred pred;
            Ch* temp = text;
            while (pred(*temp))
            {
                ++temp;
            }
            text = temp;
        }

        template<int Flags> json_object<Ch>* parse_object(json_object<Ch>* val, Ch*& text)
        {
            json_value<Ch>* lastchild = 0;
            val->type_ = value_object;

            skip<whitespace_pred, Flags>(text);

            // Check for empty object
            if (*text == Ch('}'))
            {
                ++text;
                return val;
            }

            for (;;)
            {
                if (*text != Ch('\"')) FASTJSON_PARSE_ERROR("Expected name (string)", text);

                // Read the name
                Ch* namebegin;
                Ch* nameend;
                parse_string<Flags>(++text, namebegin, nameend);

                // ws : ws (name separator)
                skip<whitespace_pred, Flags>(text);
                if (*text != Ch(':')) FASTJSON_PARSE_ERROR("Expected name separator (:)", text);
                ++text;
                skip<whitespace_pred, Flags>(text);

                json_value<Ch>* child = parse_value<Flags>(text);
                child->name_ = namebegin;
                child->nameend_ = nameend;

                val->add_child(child);

                lastchild = child;

                // Eat whitespace and an optional value-separator
                skip<whitespace_pred, Flags>(text);

                if (*text == Ch(','))
                {
                    ++text;
                    skip<whitespace_pred, Flags>(text);

                    // Close off the last value
                    if ((Flags & parse_no_string_terminators) == 0)
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                }
                else if (*text == Ch('}'))
                {
                    // End of object
                    ++text;

                    // Close off the last value
                    if ((Flags & parse_no_string_terminators) == 0)
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                    break;
                }
                else
                {
                    FASTJSON_PARSE_ERROR("Expected value-separator ',' or end-of-object '}'", text);
                }
            }
            return val;
        }

        template<int Flags> json_object<Ch>* parse_array(json_object<Ch>* val, Ch*& text)
        {
            json_value<Ch>* lastchild = 0;
            val->type_ = value_array;

            skip<whitespace_pred, Flags>(text);

            // Check for empty array
            if (*text == Ch(']'))
            {
                ++text;
                return val;
            }

            for (;;)
            {
                json_value<Ch>* child = parse_value<Flags>(text);

                val->add_child(child);

                lastchild = child;

                // Eat whitespace
                skip<whitespace_pred, Flags>(text);

                if (*text == Ch(','))
                {
                    ++text;
                    skip<whitespace_pred, Flags>(text);

                    // Close off the last value
                    if ((Flags & parse_no_string_terminators) == 0)
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                }
                else if (*text == Ch(']'))
                {
                    // End of array
                    ++text;

                    // Close off the last value
                    if ((Flags & parse_no_string_terminators) == 0)
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                    break;
                }
                else
                {
                    FASTJSON_PARSE_ERROR("Expected value-separator ',' or end-of-array ']'", text);
                }
            }
            return val;
        }

        template<int Flags> void parse_string(Ch*& text, Ch*& begin, Ch*& end)
        {
            Ch* out = text;
            begin = text;
            for (;;)
            {
                switch (*text)
                {
                default:
                    if ((Flags & parse_no_string_translation) == 0)
                    {
                        *out++ = *text;
                    }
                    ++text;
                    break;

                case Ch('\"'):  // End of string
                    end = text;
                    if ((Flags & parse_no_string_terminators) == 0)
                    {
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out = Ch('\0');
                            end = out;
                        }
                        else
                        {
                            *text = Ch('\0');
                        }
                    }
                    ++text;
                    return;

                case Ch('\\'):  // Escaped character
                    switch (*++text)
                    {
                    default:
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = *text;
                        }
                        ++text;
                        break;

                    case Ch('b'): // Backspace
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = Ch('\x08');
                        }
                        ++text;
                        break;

                    case Ch('f'): // Form feed
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = Ch('\x0c');
                        }
                        ++text;
                        break;

                    case Ch('n'): // Line feed
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = Ch('\x0a');
                        }
                        ++text;
                        break;

                    case Ch('r'): // carriage return
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = Ch('\x0d');
                        }
                        ++text;
                        break;

                    case Ch('t'): // tab
                        if ((Flags & parse_no_string_translation) == 0)
                        {
                            *out++ = Ch('\x09');
                        }
                        ++text;
                        break;

                    case Ch('u'): // unicode UTF-16 character
                        --text; // revert back to the backslash
                        {
                            wint_t c[2];
                            c[0] = internal::read_utf16(text);
                            if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                            {
                                // UTF-16 surrogate pair; read the next required character
                                c[1] = internal::read_utf16(text);
                                assert(c[1] >= 0xdc00 && c[1] <= 0xdfff);
                            }
                            if ((Flags & parse_no_string_translation) == 0)
                            {
                                internal::from_utf16<Ch>()(c, out);
                            }
                        }
                        break;
                    }
                    break;

                case Ch('\0'):
                    FASTJSON_PARSE_ERROR("Expected end-of-string '\"'", text);
                    break;
                }
            }
        }

        template<int Flags> void parse_number(Ch*& text, json_value<Ch>* val)
        {
            val->type_ = value_number;
            val->value_ = text;

            // A minus is allowed as the first character
            if (*text == Ch('-')) ++text;

            // A single zero or a series of digits
            if (*text == Ch('0')) ++text;
            else
            {
                Ch* start = text;
                skip<digit_pred, Flags>(text);
                if (text == start) FASTJSON_PARSE_ERROR("Expected digit", text);
            }

            // Optional decimal point
            if (*text == Ch('.'))
            {
                ++text;
                Ch* start = text;
                skip<digit_pred, Flags>(text);
                if (text == start) FASTJSON_PARSE_ERROR("Expected fractional digits", text);
            }

            // Optional exponent
            if (*text == Ch('e') || *text == Ch('E'))
            {
                ++text;
                
                // Optional +/-
                if (*text == Ch('+') || *text == Ch('-')) ++text;

                // Digits
                Ch* start = text;
                skip<digit_pred, Flags>(text);
                if (text == start) FASTJSON_PARSE_ERROR("Expected exponent digits", text);
            }

            val->valueend_ = text;
        }

        template<int Flags> json_value<Ch>* parse_value(Ch*& text)
        {
            json_value<Ch>* val = 0;
            switch (*text)
            {
            case Ch('-'):
            case Ch('0'): case Ch('1'): case Ch('2'): case Ch('3'): case Ch('4'):
            case Ch('5'): case Ch('6'): case Ch('7'): case Ch('8'): case Ch('9'):
                parse_number<Flags>(text, val = allocate_value());
                return val;

            case Ch('f'): // False
                if (text[1] == Ch('a') && text[2] == Ch('l') && text[3] == Ch('s') && text[4] == Ch('e'))
                {
                    val = allocate_value();
                    val->type_ = value_bool;
                    val->value_ = &text[0];
                    val->valueend_ = &text[5];
                    text += 5;
                    return val;
                }
                break;

            case Ch('t'): // True
                if (text[1] == Ch('r') && text[2] == Ch('u') && text[3] == Ch('e'))
                {
                    val = allocate_value();
                    val->type_ = value_bool;
                    val->value_ = &text[0];
                    val->valueend_ = &text[4];
                    text += 4;
                    return val;
                }
                break;

            case Ch('n'): // Null
                if (text[1] == Ch('u') && text[2] == Ch('l') && text[3] == Ch('l'))
                {
                    val = allocate_value();
                    val->type_ = value_null;
                    val->value_ = &text[0];
                    val->valueend_ = &text[4];
                    text += 4;
                    return val;
                }
                break;

            case Ch('{'): // Object begin
                ++text;
                return parse_object<Flags>(allocate_object(), text);

            case Ch('['): // Array begin
                ++text;
                return parse_array<Flags>(allocate_object(), text);

            case Ch('"'): // String begin
                ++text;
                val = allocate_value();
                val->type_ = value_string;
                parse_string<Flags>(text, val->value_, val->valueend_);
                return val;
            }

            FASTJSON_PARSE_ERROR("Expected value", text);
        }

        static const Ch truetext[];
        static const Ch falsetext[];
        static const Ch nulltext[];

        json_object<Ch>* root_;
    };

    template<class Ch, class T_POOL> const Ch json_document<Ch, T_POOL>::truetext =
         = { Ch('t'), Ch('r'), Ch('u'), Ch('e'), Ch('\0') };

    template<class Ch, class T_POOL> const Ch json_document<Ch, T_POOL>::falsetext =
         = { Ch('f'), Ch('a'), Ch('l'), Ch('s'), Ch('e'), Ch('\0') };

    template<class Ch, class T_POOL> const Ch json_document<Ch, T_POOL>::nulltext =
         = { Ch('n'), Ch('u'), Ch('l'), Ch('l'), Ch('\0') };

    ///////////////////////////////////////////////////////////////////////////
    // Implementation

    template<class Ch> json_value<Ch>* internal::nullval()
    {
        static json_value<Ch> val;
        return &val;
    }

    namespace internal
    {
        template<int Dummy>
        const bool lookup_tables<Dummy>::lookup_whitespace[256] =
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  0,  0,  1,  0,  0,  // 0
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
             1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 3
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 7
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
        };

        template<int Dummy>
        const bool lookup_tables<Dummy>::lookup_digit[256] =
        {
          // 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 0
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 1
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 2
             1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  // 3
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 4
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 5
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 6
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 7
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 8
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // 9
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // A
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // B
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // C
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // D
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  // E
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0   // F
        };

        template<int Dummy>
        const double lookup_tables<Dummy>::lookup_double[10] = 
        {
            0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0
        };

        template<int Dummy>
        const char lookup_tables<Dummy>::lookup_hexchar[16] =
        {
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
        };
    }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif

#endif