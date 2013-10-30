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

#define NOEXCEPT throw()

// Exceptions may be disabled completely by using FASTJSON_NO_EXCEPTIONS,
// however this will cause the default_error_handler to abort().
#if !defined(FASTJSON_NO_EXCEPTIONS)

#include <exception> // For std::exception

namespace fastjson
{
    //! This exception is thrown by the parser when an error occurs when using fastjson::default_error_handler.
    //! Use the what() function to get the human-readable error description.
    //! Use the where() function to get a pointer to position within source text where the error
    //! was detected.
    //! <br><br>
    //! If throwing exceptions is undesirable, exceptions can be disabled
    //! by using <tt>\#define FASTJSON_NO_EXCEPTIONS</tt> before fastjson.hpp is included.
    //! This will change the behavior of fastjson::default_error_handler to \c abort().
    //! <br><br>
    //! This class derives from <tt>std::exception</tt>.
    class parse_error : public std::exception
    {
    public:
        //! Constructor
        parse_error(const char* what, void* where)
            : what_(what)
            , where_(where)
        {}

        //! Gets the human-readable description of the error
        //! \return Pointer to a human-readable description of the error. This error
        //! is always a static literal string.
        virtual const char* what() const NOEXCEPT
        {
            return what_;
        }

        //! Gets the pointer to the character data where the error happened.
        //! Ch should be the same as the character type of json_document that produced
        //! the error.
        //! \return Pointer to location within the parsed string where the error occurred.
        template<class Ch> Ch* where() const NOEXCEPT
        {
            return reinterpret_cast<Ch*>(where_);
        }

    private:
        const char* what_;
        void* where_;
    };

    //! Defines an error handler functor to execute when a parse error occurs. The default
    //! behavior is to throw the parse_error exception. If this is undesirable, implement
    //! a custom error handler functor and pass it to the json_document::parse function.
    //! <br><br>
    //! A potential alternate to using exceptions would be to use \c setjmp() and \c longjmp().
    //! <br><br>
    //! If \c FASTJSON_NO_EXCEPTIONS is defined, the default behavior is to call \c abort().
    //! NOTE: The functor must never return, otherwise \c abort() is called.
    struct default_error_handler
    {
        //! \brief Handles the given error.
        //! \param what A human-readable description of the error.
        //! \param where The pointer offset into the buffer given to json_document::parse that generated the error.
        void operator () (const char* what, void* where)
        {
            throw parse_error(what, where);
        }
    };
}

#else

namespace fastjson
{
    struct default_error_handler
    {
        void operator () (const char*, void*)
        {
            abort();
        }
    };
}

#endif

#define FASTJSON_PARSE_ERROR(what, where) do { handler(what, where); abort(); } while (0)
#define FASTJSON_PARSE_ERROR_THIS(what, where) do { static_cast<error_handler&>(*this)(what, where); abort(); } while (0)

///////////////////////////////////////////////////////////////////////////////
// Pool size definitions

#if !defined(FASTJSON_STATIC_POOL_SIZE)
    //! Size of the static memory block of memory_pool
    //! Define FASTJSON_STATIC_POOL_SIZE before including fastjson.hpp if you want to override the default value.
    //! The default value is 32 Kbytes.
    //! No dynamic allocations are performed until the static pool is exhausted.
    //! Value may be set to zero to disable static pool and cause all allocations to be from the heap.
    #define FASTJSON_STATIC_POOL_SIZE (32 * 1024)
#endif

#if !defined(FASTJSON_DYNAMIC_POOL_SIZE)
    //! Size of the dynamic memory block of memory_pool
    //! Define FASTJSON_DYNAMIC_POOL_SIZE before including fastjson.hpp if you want to override the default value.
    //! The default value is 32 Kbytes.
    //! Dynamic memory blocks of this size are allocated when necessary, and only after the static memory pool is exhausted.
    //! If set to zero, all allocations will require a separate heap allocation.
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
    template<class Ch, class error_handler, class T_POOL> class json_document;
    template<class Ch> class json_value;
    template<class Ch> class json_object;
    
    //! A single byte representation used by fastjson.
    typedef unsigned char byte;

    //! \brief Enumeration of value types produced by the parser.
    //! Given by the fastjson::json_value::type() function.
    //! See fastjson::json_value for more information.
    enum value_type
    {
        value_null,     //!< The json null type.
        value_bool,     //!< A boolean value that may be true/false.
        value_number,   //!< A double-precision floating point value.
        value_string,   //!< A Unicode-encoded string. The encoding depends on the size of \c Ch.
        value_array,    //!< An array type. The class of the json_value is actually json_object.
        value_object,   //!< An object type. The class of the json_value is actually json_object.
    };

    namespace internal
    {
        //! Lookup tables used to speed lookups. These are defined as a template so that they
        //! may be included in this header file without the linker complaining.
        template<int Dummy>
        struct lookup_tables
        {
            static const bool lookup_whitespace[256];
            static const bool lookup_digit[256];
            static const double lookup_double[10];
            static const char lookup_hexchar[16];
        };

        //! Returns a null (empty) string.
        template<class Ch>
        Ch* nullstr() NOEXCEPT
        {
            static Ch nul = Ch('\0');
            return &nul;
        }

        //! Returns a shared json_value that has type value_null.
        template<class Ch>
        json_value<Ch>* nullval() NOEXCEPT;

        //! Measures the length of a NUL-terminated string of \c Ch characters.
        //! A null pointer returns a zero length.
        template<class Ch>
        std::size_t length(const Ch* p) NOEXCEPT
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

        //! Compares two strings. Assumes that \c first is NUL-terminated and
        //! \c second is counted (\c secondend points to the first character following the string).
        //! \return 0 if strings match fully (length must match)
        //! \return -1 if first would be sorted lexicographically before second
        //! \return 1 if first would be sorted lexicographically after second
        template<class Ch>
        int compare(const Ch* first, const Ch* second, const Ch* secondend) NOEXCEPT
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
        
        //! \brief Translates a \c Ch of a hex character to its numeric value.
        //! \param text the character to convert
        //! \param handler an error handler that is called if \c text is not a valid hex character
        //! \return the numeric representation of the given character
        template<class Ch, class error_handler>
        byte hex_value(Ch* text, error_handler handler)
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

        //! \brief Translates a numeric value to a hex character
        //! \param value The value to translate. Must be less than 16.
        //! \return The hex character that represents the value.
        template<class Ch>
        Ch hex_char(byte value)
        {
            assert(value < 16);
            return Ch(lookup_tables<0>::lookup_hexchar[value]);
        }

        //! \brief Converts a string value to a double.
        //! Numeric string values are converted to a double floating-point number. A string value of 'true' returns 1.0. Other strings return 0.0.
        //! \return The double value of the given string. As much of the string as possible is processed.
        //! \return Processing stops when the string is no longer formatted as a double value.
        template<class Ch>
        double value_to_number(const Ch* start, const Ch* end) NOEXCEPT
        {
            if (start == end) return 0.0;

            // Convert 'true' to 1.0
            if ((end - start) == 4 && start[0] == Ch('t') && start[1] == Ch('r') && start[2] == Ch('u') && start[3] == Ch('e'))
            {
                return 1.0;
            }

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

        //! A union for a double that breaks it apart into mantissa, exponent and sign bits.
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

        //! \brief Converts a double to a json-compatible number string.
        //! Absolute values smaller than 0.0000000000001 are rounded to zero.
        //! Absolute values smaller than 0.0000000001 or larger than 1,000,000,000,000 are represented using scientific notation.
        //! Up to 12 digits of fractional precision are used.
        //! \return true if the value could be represented as a json number.
        //! \return false if the value is non-finite (infinite or not-a-number) and must be represented as a json string
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

        //! \brief Converts a text string to a boolean value.
        //! This works with all json values. A json string of "true" will return true.
        //! A string that starts with a non-zero number will also return true.
        //! A null value will return false.
        //! No errors or exceptions are thrown. Invalid values will return false.
        template<class Ch>
        bool value_to_boolean(const Ch* start, const Ch* end) NOEXCEPT
        {
            if (start == end) return false;
            if (start[0] == Ch('t') && start[1] == Ch('r') && start[2] == Ch('u') && start[3] == Ch('e')) return true;
            if (start[0] == Ch('f') && start[1] == Ch('a') && start[2] == Ch('l') && start[3] == Ch('s') && start[4] == Ch('e')) return false;
            return value_to_number(start, end) != 0.0;
        }

        //! \brief Reads a json-style UTF-16 character in the format \\u1234
        //! \param ptr the current parsing location. The pointer is advanced by the parsing of the UTF-16 value.
        //! \param handler if parsing fails, the function is invoked.
        //! \return The UTF-16 character represented by the string.
        template<class Ch, class error_handler>
        wint_t read_utf16(Ch*& ptr, error_handler handler)
        {
            if (*ptr++ != Ch('\\')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 1);
            if (*ptr++ != Ch('u')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 2);
            wint_t c(0);
            // hex_value will throw if not a hex character
            c |= hex_value(ptr, handler); c <<= 4; ++ptr;
            c |= hex_value(ptr, handler); c <<= 4; ++ptr;
            c |= hex_value(ptr, handler); c <<= 4; ++ptr;
            c |= hex_value(ptr, handler); ++ptr;
            return c;
        }

        // The from_utf16 structures are template specialization that convert
        // from UTF-16 (used by the \u1234 construct in json) to the target encoding of \c Ch.

        // Default implementation; generates compile error
        template<class Ch, std::size_t S = sizeof(Ch)>
        struct from_utf16 { };

        template<class Ch>
        struct from_utf16<Ch, 1> // UTF-16 to UTF-8
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
        struct from_utf16<Ch, 2> // UTF-16 to UTF-16
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
        struct from_utf16<Ch, 4> // UTF-16 to UTF-32
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

    const int parse_no_string_terminators = 1 << 0; //!< Don't terminate string values with NUL characters. Use json_value::nameend() to get the end of the string.

    const int parse_no_string_translation = 1 << 1; //!< Don't translate characters such as \\b to backspace.

    const int parse_non_destructive = parse_no_string_terminators | parse_no_string_translation; //!< Ensures that the buffer passed to json_document::parse is not modified.

    ///////////////////////////////////////////////////////////////////////////
    // Allocator interface

    //! \class default_allocator
    //! Uses new/delete to allocate pool memory. Used by memory_pool to handle dynamic allocations.
    class default_allocator
    {
    public:
        //! Constructor
        default_allocator() {}
        //! Destructor
        ~default_allocator() {}

        //! \brief Allocates memory from the heap.
        //! \param size the number of bytes to allocate from the heap
        //! \return memory allocated from the heap or NULL if allocation failed
        //! \exception std::bad_alloc may be thrown if memory cannot be allocated
        byte* raw_heap_alloc(std::size_t size)
        {
            return new byte[size];
        }

        //! \brief Frees memory previously allocated with raw_heap_alloc
        //! \param p the memory previously allocated with raw_heap_alloc
        void raw_heap_free(byte* p)
        {
            delete[] p;
        }
    };

    ///////////////////////////////////////////////////////////////////////////
    // Memory pool

    // Implements the statically-sized pool. This pool is consumed first.
    template<std::size_t static_size>
    class static_pool
    {
        byte static_pool_[static_size];
    protected:
        byte* static_pool_start() NOEXCEPT { return static_pool_; }
        byte* static_pool_end() NOEXCEPT { return static_pool_ + static_size; }
    };

    // Specialization of static_pool that is zero bytes
    template <>
    class static_pool<0>
    {
    protected:
        byte* static_pool_start() NOEXCEPT { return 0; }
        byte* static_pool_end() NOEXCEPT { return 0; }
    };

    //! \class memory_pool
    //! \brief Allocates and tracks memory used by a json_document.
    //!
    //! The memory_pool uses a two-stage allocation system. First a static pool of size \c static_size is used. This is actually reserved as part of the size of
    //! the object and therefore does not require a heap allocation. Once the static pool is exhausted, dynamic pools of size \c dynamic_size
    //! (or larger as necessary) are allocated whenever additional memory is needed. The \c T_ALLOC template parameter is used for the dynamic pool
    //! allocation.
    //! \tparam T_ALLOC the allocator to use for dynamic allocations. See default_allocator for more info.
    //! \tparam static_size Defaults to FASTJSON_STATIC_POOL_SIZE (32 KBytes). Specifies the size of memory to be included in the memory_pool for initial allocations. May be zero to force all allocations to be dynamic (reduces \c sizeof(memory_pool) ).
    //! \tparam dynamic_size Defaults to FASTJSON_DYNAMIC_POOL_SIZE (32 KBytes). Specifies the size of each additional needed dynamic pool from the heap. If set to zero, all requested allocations are passed through to the heap (no pooling occurs).
    template<class T_ALLOC = default_allocator, std::size_t static_size = FASTJSON_STATIC_POOL_SIZE, std::size_t dynamic_size = FASTJSON_DYNAMIC_POOL_SIZE>
    class memory_pool : public T_ALLOC, public static_pool<static_size>
    {
    public:
        //! Constructor
        memory_pool() NOEXCEPT { init(); }

        //! Destructor. Frees all of the memory used by this memory_pool.
        ~memory_pool() { clear(); }

        //! \brief Clears all allocations from this memory pool back to the heap.
        void clear()
        {
            while (pool_ != static_pool_start())
            {
                byte* next = reinterpret_cast<header*>(align_forward(pool_))->next_pool_;
                raw_heap_free(pool_);
                pool_ = next;
            }
        }

        //! \brief Allocates the requested number of bytes from the memory pool. May cause a heap allocation for additional dynamic pool space.
        //! \param size The number of bytes to allocate.
        //! \return The allocated memory, or zero if allocation fails.
        //! \return The returned memory is aligned on FASTJSON_ALIGNMENT.
        byte* alloc(std::size_t size)
        {
            if (size > (std::size_t)(end_ - next_))
            {
                // Need to alloc another pool
                if (!pool_alloc(size))
                {
                    return 0;
                }
            }
            assert(size <= (std::size_t)(end_ - next_));
            byte* allocd = next_;
            next_ = align_forward(allocd + size);
            assert(next_ <= end_);
            return allocd;
        }

    protected:
        //! \brief Initializes the memory pool.
        void init() NOEXCEPT
        {
            pool_ = static_pool_start();
            next_ = align_forward(pool_);
            end_ = static_pool_end();
            assert(next_ <= end_);
        }

        //! \brief Aligns \c data on FASTJSON_ALIGNMENT.
        byte* align_forward(byte* data) NOEXCEPT
        {
            return (byte*)((size_t)(data + (FASTJSON_ALIGNMENT-1)) & ~(FASTJSON_ALIGNMENT-1));
        }

        //! \brief Allocates memory from T_ALLOC (e.g. default_allocator) to serve as an additional dynamic pool.
        //! \param minsize The minimum size to request. The actual size of the dynamic pool may be larger than this.
        //! \return true if allocation succeeded; false otherwise
        bool pool_alloc(std::size_t minsize)
        {
            std::size_t size = dynamic_size;
            if (size < minsize) size = minsize;

            size += (FASTJSON_ALIGNMENT * 2);
            size += sizeof(header);

            byte* pool = raw_heap_alloc(size);
            if (pool == 0)
            {
                return false;
            }
            header* h = new (align_forward(pool)) header(pool_);
            pool_ = pool;
            next_ = align_forward((byte*)(h + 1));
            end_  = pool + size;
            assert(next_ < end_);
            return true;
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

    //! \brief An instance of a json value.
    //! \tparam Ch the character type to use. Will match character type used by json_document.
    template<class Ch = char>
    class json_value
    {
        template<class Ch, class error_handler, class T_POOL> friend class json_document;
        template<class Ch> friend class json_object;

        //! Constructor. Values may only be created by the json_document.
        json_value(value_type type = value_null, Ch* name = 0, Ch* nameend = 0) NOEXCEPT
            : type_(type)
            , name_(name)
            , nameend_(nameend)
            , value_(internal::nullstr<Ch>())
            , valueend_(internal::nullstr<Ch>())
            , owner_(0)
            , prev_(0)
            , next_(0)
        {}
        //! Destructor
        virtual ~json_value() NOEXCEPT {}

    public:
        //! \brief Returns the value_type of this value.
        //! \return The value_type of this value.
        value_type type() const NOEXCEPT { return type_; }

        //! \brief Returns the name of this value, or an empty string if the value is not named.
        const Ch* name() const NOEXCEPT { return name_; }
        //! \brief Returns the end of the string returned by name(). Useful if parse_no_string_terminators is used.
        const Ch* nameend() const NOEXCEPT { return nameend_; }

        //! \brief Returns true if this is a representation of a json null vaule.
        bool is_null() const NOEXCEPT { return type_ == value_null; }
        //! \brief Retruns true if this is a representation of a json boolean value.
        bool is_boolean() const NOEXCEPT { return type_ == value_bool; }
        //! \brief Returns true if this is a representation of a json numeric value.
        bool is_number() const NOEXCEPT { return type_ == value_number; }
        //! \brief Returns true if this is a representation of a json string value.
        bool is_string() const NOEXCEPT { return type_ == value_string; }
        //! \brief Returns true if this is a representation of a json array value.
        bool is_array() const NOEXCEPT { return type_ == value_array; }
        //! \brief Returns true if this is a representation of a json object value.
        bool is_object() const NOEXCEPT { return type_ == value_object; }

        //! \brief Attempts to convert the value to a string.
        //! For a null value, "null" is returned.
        //! For a boolean value, "true" or "false" is returned.
        //! For a numeric value, the string representation of the numeric value is returned.
        //! Objects and arrays will return an empty string.
        //! Use fastjson::print in fastjson_print.hpp to render objects and arrays to string.
        const Ch* as_string() const NOEXCEPT { return value_; }
        //! \brief Returns the end of the string returned by as_string(). Useful if parse_no_string_terminators is used.
        const Ch* as_string_end() const NOEXCEPT { return valueend_; }

        //! \brief Attempts to convert the value to a number.
        //! A null value or a false boolean value will return 0.0.
        //! A true boolean value will return 1.0.
        //! A string value will attempt to convert as much as possible to a number.
        //! Zero is returned if an error occurs.
        double as_number() const { return internal::value_to_number(value_, valueend_); }
        
        //! \brief Attempts to convert the value to a boolean.
        //! A null value, false boolean value or a zero value will return 0.0.
        //! A non-zero numeric value or a true boolean value will return true.
        //! String values return true for "true" and non-zero numeric representations. All other string values return false.
        bool   as_boolean() const { return internal::value_to_bool(value_, valueend_); }

        //! \brief Queries the array interface if the value is an array or an object.
        json_object<Ch>* as_array() NOEXCEPT { return is_array()|is_object() ? static_cast<json_object<Ch>*>(this) : 0; }
        //! \brief Queries the array interface if the value is an array or an object.
        const json_object<Ch>* as_array() const NOEXCEPT { return is_array()|is_object() ? static_cast<const json_object<Ch>*>(this) : 0; }
        //! \brief Queries the object interface if the value is an array or an object.
        json_object<Ch>* as_object() NOEXCEPT { return is_array()|is_object() ? static_cast<json_object<Ch>*>(this) : 0; }
        //! \brief Queries the object interface if the value is an array or an object.
        const json_object<Ch>* as_object() const NOEXCEPT { return is_array()|is_object() ? static_cast<const json_object<Ch>*>(this) : 0; }

        //! \brief When iterating, returns the next sibling to this value.
        const json_value<Ch>* next_sibling() const NOEXCEPT { return next_; }

    protected:
        value_type type_;           //!< The type of this json_value object
        Ch* name_;                  //!< Pointer to the name of this json_value. name_ and nameend_ will be valid but point to an empty string if this json_value has no name.
        Ch* nameend_;               //!< Pointer to the end of the name of this json_value.
        Ch* value_;                 //!< Pointer to the string representation of this json_value.
        Ch* valueend_;              //!< Pointer to the end of the string representation of this json_value.
        json_object<Ch>* owner_;    //!< The json_object that owns this json_value. Will be NULL for the root.
        json_value<Ch>* prev_;      //!< Points to the previous json_value in an array or object. NULL if there is no previous json_value.
        json_value<Ch>* next_;      //!< Points to the next json_value in an array or object. NULL if there is no previous json_value.
    };

    //! \brief An instance of a json object or array.
    //! \tparam Ch the character type to use. Will match character type used by json_document.
    template<class Ch = char>
    class json_object : public json_value<Ch>
    {
        template<class Ch, class error_handler, class T_POOL> friend class json_document;

    public:
        //! \brief Constructor
        json_object(value_type type, Ch* name, Ch* nameend) NOEXCEPT
            : json_value<Ch>(type, name, nameend)
            , child_(0)
            , lastchild_(0)
            , numchildren_(0)
        {
            assert(type == value_array || type == value_object);
        }

        //! \brief Returns the number of child json_value objects.
        size_t num_children() const NOEXCEPT { return numchildren_; }

        //! \brief Returns true if this object or array has no children (i.e. it's empty).
        bool is_empty() const NOEXCEPT { return numchildren_ == 0; }

        //! \brief Returns the first json_value object. See json_value::next_sibling() for iteration.
        //! If there are no child objects, NULL is returned.
        const json_value<Ch>* first_child() const NOEXCEPT { return child_; }

        //! \brief For a json object, accesses a member by name.
        //! The name is case-sensitive. Has time order O(n).
        //! If the value does not exist, a json_value of type value_null is returned.
        const json_value<Ch>& operator [] (const Ch* name) const NOEXCEPT
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

        //! \brief For a json array (or object), accesses a member by index.
        //! Has time order O(n).
        //! \param index The index to access. Negative numbers start at the last position (-1 is the last). Zero starts at the first position.
        //! \return the json_value at the specified index.
        //! \return For an invalid index, a json_value of type value_null is returned.
        const json_value<Ch>& operator [] (int index) const NOEXCEPT
        {
            const json_value<Ch>* p;
            if (index < 0)
            {
                p = lastchild_;
                while (++index < 0 && p) p = p->prev_;
            }
            else
            {
                while (index-- > 0 && p) p = p->next_;
            }
            return p ? *p : internal::nullval<Ch>();
        }

        //! \brief Adds the given value to the end of the array.
        //! \param val A json_value that has been allocated by any of the allocation functions in json_document.
        //! \return true if the json_value was added. Returns false if not an array or \c val is a NULL pointer or already exists in an array or object.
        bool array_add(json_value<Ch>* val) NOEXCEPT
        {
            if (!is_array() || val == 0 || val->owner_ != 0) return false;
            add_child(val);
            return true;
        }

        //! \brief Inserts the given value at the given index of the array.
        //! Has time order O(n).
        //! \param val A json_value that has been allocated by any of the allocation functions in json_document.
        //! \param index The index at which to insert \c val. Negative numbers start at the last position (-1). Zero inserts before the first position. Indexes are clamped, so INT_MIN would insert before first and INT_MAX would insert after last.
        //! \return true if the json_value was inserted. Returns false if not an array or \c val is a NULL pointer or already exists in an array or object.
        bool array_insert(json_value<Ch>* val, int index) NOEXCEPT
        {
            assert(val->owner_ == 0);
            if (!is_array() || val == 0 || val->owner_ != 0) return false;
            json_value<Ch>** pp;
            if (index < 0)
            {
                pp = &lastchild_;
                while (++index < 0 && *pp)
                {
                    pp = &(*pp)->prev_;
                }
                val->prev_ = *pp;
                if (val->prev_) val->prev_->next_ = val; else child_ = val;
                val->next_ = *pp ? (*pp)->next_ : 0;
            }
            else
            {
                pp = &child_;
                while (index-- > 0 && *pp)
                {
                    pp = &(*pp)->next_;
                }
                val->next_ = *pp;
                if (val->next_) val->next_->prev_ = val; else lastchild_ = val;
                val->prev_ = *pp ? (*pp)->prev_ : 0;
            }

            *pp = val;
            ++numchildren_;
            return true;
        }

        //! \brief Removes a json_value from the array.
        //! Has time order O(n).
        //! \param index The index to remove. Negative numbers index from the last value (-1 is the last item). Zero starts at the first value. Indexes are clamped so INT_MIN would remove the first value and INT_MAX would remove the last.
        //! \return the removed value. It may be re-added by using array_add or array_insert. If the array is empty, NULL is returned. The returned value will be automatically destroyed when the json_document that constructed it is destroyed.
        json_value<Ch>* array_remove(int index) NOEXCEPT
        {
            if (!is_array()) return 0;
            json_value<Ch>* p;
            if (index < 0)
            {
                p = lastchild_;
                while (++index < 0 && p && p->prev_)
                {
                    p = p->prev_;
                }
            }
            else
            {
                p = child_;
                while (index-- > 0 && p && p->next_)
                {
                    p = p->next_;
                }
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

        //! \brief Sets a value by name on a json object.
        //! Has time order O(n).
        //! \param name The case-sensitive name to assign to \c val. If the name is already in use, the previous value is replaced.
        //! \param val The value to set within the object that has been allocated by any of the allocation functions in json_document.
        //! \param old An optional pointer that receives the replaced value (or NULL if a value was not replaced).
        bool object_set(const Ch* name, json_value<Ch>* val, json_value<Ch>** old = 0) NOEXCEPT
        {
            if (old) *old = 0;
            if (!is_object()) return false;
            if (name == 0 || *name == Ch('\0')) return false;
            if (val == 0 || val->owner_ != 0) return false;
            val->name_ = name;
            val->nameend_ = name + internal::length(name);
            // If there's an existing value by this name, then replace it.
            json_value<Ch>** pp = &child_;
            while (*pp)
            {
                if (internal::compare(name, (*pp)->name_, (*pp)->nameend_) == 0)
                {
                    // Replace the node *pp with val
                    if (old) *old = *pp;
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

        //! \brief Removes a value from a json object.
        //! Has time order O(n).
        //! \param name The case-sensitive name of the value to remove.
        //! \return The removed json_value object if found. NULL if nothing was found. The returned json_value will be automatically destroyed when the json_document that constructed it is destroyed.
        json_value<Ch>* object_remove(const Ch* name) NOEXCEPT
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

        //! \brief Removes all children of a json object or array.
        void remove_all() NOEXCEPT
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
        //! \brief Helper function for appending a child to the end.
        void add_child(json_value<Ch>* child) NOEXCEPT
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

    //! \brief The main workhorse of the fastjson library.
    //! \tparam Ch The character type to use. Determines whether UTF-8, UTF-16 or UTF-32 encoding is used. The document is converted to the best size-matching encoding.
    //! \tparam error_handler The functor class that will be handling errors. A concrete instance can be passed to the parse() function.
    //! \tparam T_POOL Allows overriding the memory pool for this json_document object. See memory_pool for more information.
    template<class Ch = char, class error_handler = default_error_handler, class T_POOL = memory_pool<> >
    class json_document : protected T_POOL, protected error_handler
    {
        //! Disable copy constructor and assignment
        json_document(const json_document&);
        json_document& operator = (const json_document&);

    public:
        //! Constructor
        json_document()
            : root_(allocate_object())
        {}

        //! \brief Parses a json document.
        //! \tparam Flags The flags to use when parsing.
        //! \param text The json text to process. Must be NUL-terminated.
        //! \param handler An optional instance of the error handler. See default_error_handler for default behavior.
        template<int Flags> void parse(Ch* text, error_handler handler = error_handler())
        {
            assert(text);

            static_cast<error_handler&>(*this) = handler;

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
                FASTJSON_PARSE_ERROR_THIS("Expected '{' or '['", text);
            }

            skip<whitespace_pred, Flags>(text);

            if (*text != Ch('\0'))
            {
                FASTJSON_PARSE_ERROR_THIS("Expected end of document", text);
            }
        }

        //! \brief Returns the const root json_object for this document.
        //! \return The const root json_object for this document. For a newly-constructed json_document, this is an empty json_object of type value_object.
        const json_object<Ch>& root() const NOEXCEPT { return *root_; }

        //! \brief Returns the mutable root json_object for this document.
        //! \return The mutable root json_object for this document. For a newly-constructed json_document, this is an empty json_object of type value_object.
        json_object<Ch>& root() NOEXCEPT { return *root_; }

        //! \brief Allocates a string for use as a name or string value.
        //! See json_object::object_set() as an example of using \c allocate_string for a name.
        //! \param str The string to copy. May be NULL which returns an empty string.
        //! \return A pointer to a copy of str. The destruction of this copy will happen when the json_document is destroyed.
        //! \return Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        Ch* allocate_string(const Ch* str)
        {
            if (str)
            {
                std::size_t len = internal::length(str);
                Ch* newstr = (Ch*)alloc(sizeof(Ch) * (len + 1));
                Ch* p = newstr;
                while (*str)
                {
                    *p = *str;
                    ++p, ++str;
                }
                *p = Ch('\0');
                return newstr;
            }
            return nullstr<Ch>();
        }

        //! \brief Allocates a json null value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_null_value()
        {
            json_value<Ch>* pval = allocate_value(value_null);
            pval->value_ = &nulltext[0];
            pval->valueend_ = &nulltext[4];
            return pval;
        }

        //! \brief Allocates a json boolean value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_bool_value(bool val)
        {
            json_value<Ch>* pval = allocate_value(value_bool);
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

        //! \brief Allocates a json string value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! Note that \c value is not copied. The caller must ensure that the lifetime of \c value is as long as the json_document.
        //! This can be accomplished by calling allocate_string() to create a string copy with a guaranteed lifetime.
        //! \param value The string value to reference. The lifetime must be as long as the json_document exists.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_string_value(const Ch* value)
        {
            json_value<Ch>* pval = allocate_value(value_string);
            pval->value_ = value;
            pval->valueend_ = value + internal::length(value);
            return pval;
        }

        //! \brief Allocates a json number value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! Note that the given value is immediately converted to a string representation. If the given \c val is NaN or Infinite, the returned json_value is actually a string representation.
        //! Absolute values smaller than 0.0000000000001 are rounded to zero.
        //! Absolute values smaller than 0.0000000001 or larger than 1,000,000,000,000 are represented using scientific notation.
        //! Up to 12 digits of fractional precision are used.
        //! \param val The floating-point value to use.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_number_value(double val)
        {
            json_value<Ch>* pval = allocate_value(value_number);

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

        //! \brief Allocates a json array that can contain other json_value objects and can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_object representation of an array. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_object<Ch>* allocate_array()
        {
            return allocate_object(value_array);
        }

        //! \brief Allocates a json object that can contain other json_value objects and can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_object representation of an array. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_object<Ch>* allocate_object()
        {
            return allocate_object(value_object);
        }

    private:
        // Uses placement new to instantiate a json_value from the memory_pool.
        json_value<Ch>* allocate_value(value_type type = value_null)
        {
            return new (alloc(sizeof(json_value<Ch>))) json_value<Ch>(type, internal::nullstr<Ch>(), internal::nullstr<Ch>());
        }

        // Uses placement new to instantate a json_object from the memory_pool.
        json_object<Ch>* allocate_object(value_type type)
        {
            return new (alloc(sizeof(json_object<Ch>))) json_object<Ch>(type, internal::nullstr<Ch>(), internal::nullstr<Ch>());
        }

        // Predicate for determining if \c ch is whitespace. Uses a look-up-table.
        struct whitespace_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return internal::lookup_tables<0>::lookup_whitespace[(byte)ch];
            }
        };

        // Predicate for determining if \c ch is numeric. Uses a look-up-table.
        struct digit_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return internal::lookup_tables<0>::lookup_digit[(byte)ch];
            }
        };

        // Internal function to allocate \c size bytes from the given memory_pool (T_POOL).
        // Invokes the error_handler if memory allocation fails.
        byte* alloc(std::size_t size)
        {
            byte* p = T_POOL::alloc(size);
            if (p == 0)
            {
                FASTJSON_PARSE_ERROR_THIS("Memory allocation failed", 0);
            }
            return p;
        }

        // Advances \c text until the character at the front is not of type \c StopPred.
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

        // Internal helper function for parsing a json object. Advances \c text past the object.
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

            // Not necessary, but a more correct parse error.
            if (*text != Ch('\"')) { FASTJSON_PARSE_ERROR_THIS("Expected end-of-object '}' or name (string)", text); }

            for (;;)
            {
                if (*text != Ch('\"')) { FASTJSON_PARSE_ERROR_THIS("Expected name (string)", text); }

                // Read the name
                Ch* namebegin;
                Ch* nameend;
                parse_string<Flags>(++text, namebegin, nameend);

                // ws : ws (name separator)
                skip<whitespace_pred, Flags>(text);
                if (*text != Ch(':')) { FASTJSON_PARSE_ERROR_THIS("Expected name separator (:)", text); }
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
                    FASTJSON_PARSE_ERROR_THIS("Expected value-separator ',' or end-of-object '}'", text);
                }
            }
            return val;
        }

        // Internal helper function for parsing a json array. Advances \c text past the array.
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
                    FASTJSON_PARSE_ERROR_THIS("Expected value-separator ',' or end-of-array ']'", text);
                }
            }
            return val;
        }

        // Internal helper function for parsing a json string. Advances \c text past the string.
        // \c begin and \c end receive the string pointers.
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
                        FASTJSON_PARSE_ERROR_THIS("Invalid escaped character", text);
                        break;

                    case Ch('"'):
                    case Ch('\\'):
                    case Ch('/'):
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
                            c[0] = internal::read_utf16(text, static_cast<error_handler&>(*this));
                            if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                            {
                                // UTF-16 surrogate pair; read the next required character
                                c[1] = internal::read_utf16(text, static_cast<error_handler&>(*this));
                                if (c[1] < 0xdc00 || c[1] > 0xdfff)
                                {
                                    FASTJSON_PARSE_ERROR_THIS("Expected UTF-16 surrogate pair", text - 6);
                                }
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
                    FASTJSON_PARSE_ERROR_THIS("Expected end-of-string '\"'", text);
                    break;
                }
            }
        }

        // Internal helper function for parsing a json number. Advances \c text past the number.
        template<int Flags> void parse_number(Ch*& text, json_value<Ch>* val)
        {
            val->type_ = value_number;
            val->value_ = text;

            // A minus is allowed as the first character
            if (*text == Ch('-')) ++text;

            // A single zero or a series of digits
            if (*text == Ch('0'))
            {
                ++text;
            }
            else
            {
                Ch* start = text;
                skip<digit_pred, Flags>(text);
                if (text == start) FASTJSON_PARSE_ERROR_THIS("Expected digit", text);
            }

            // Optional decimal point
            if (*text == Ch('.'))
            {
                ++text;
                Ch* start = text;
                skip<digit_pred, Flags>(text);
                if (text == start) FASTJSON_PARSE_ERROR_THIS("Expected fractional digits", text);
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
                if (text == start) FASTJSON_PARSE_ERROR_THIS("Expected exponent digits", text);
            }

            val->valueend_ = text;
        }

        // Internal helper function for parsing a json_value.
        template<int Flags> json_value<Ch>* parse_value(Ch*& text)
        {
            json_value<Ch>* val = 0;
            switch (*text)
            {
            case Ch('-'):
            case Ch('.'): // Not necessary, but will provide a better 'Expected digit' parse error rather than 'Expected value'
            case Ch('0'): case Ch('1'): case Ch('2'): case Ch('3'): case Ch('4'):
            case Ch('5'): case Ch('6'): case Ch('7'): case Ch('8'): case Ch('9'):
                parse_number<Flags>(text, val = allocate_value());
                return val;

            case Ch('f'): // False
                if (text[1] == Ch('a') && text[2] == Ch('l') && text[3] == Ch('s') && text[4] == Ch('e'))
                {
                    val = allocate_value(value_bool);
                    val->value_ = &text[0];
                    val->valueend_ = &text[5];
                    text += 5;
                    return val;
                }
                break;

            case Ch('t'): // True
                if (text[1] == Ch('r') && text[2] == Ch('u') && text[3] == Ch('e'))
                {
                    val = allocate_value(value_bool);
                    val->value_ = &text[0];
                    val->valueend_ = &text[4];
                    text += 4;
                    return val;
                }
                break;

            case Ch('n'): // Null
                if (text[1] == Ch('u') && text[2] == Ch('l') && text[3] == Ch('l'))
                {
                    val = allocate_value(value_null);
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

            FASTJSON_PARSE_ERROR_THIS("Expected value", text);
        }

        // Pre-defined values to reduce unnecessary memory_pool allocations.
        static const Ch truetext[];
        static const Ch falsetext[];
        static const Ch nulltext[];

        json_object<Ch>* root_;
    };

    template<class Ch, class error_handler, class T_POOL> const Ch json_document<Ch, error_handler, T_POOL>::truetext =
         = { Ch('t'), Ch('r'), Ch('u'), Ch('e'), Ch('\0') };

    template<class Ch, class error_handler, class T_POOL> const Ch json_document<Ch, error_handler, T_POOL>::falsetext =
         = { Ch('f'), Ch('a'), Ch('l'), Ch('s'), Ch('e'), Ch('\0') };

    template<class Ch, class error_handler, class T_POOL> const Ch json_document<Ch, error_handler, T_POOL>::nulltext =
         = { Ch('n'), Ch('u'), Ch('l'), Ch('l'), Ch('\0') };

    ///////////////////////////////////////////////////////////////////////////
    // Implementation

    // Returns a common json null value.
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