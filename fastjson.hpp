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
    template<class Ch, class error_handler, class pool> class json_document;
    template<class Ch> class json_value;
    template<class Ch> class json_object;
    
    //! A single byte representation used by fastjson.
    typedef unsigned char byte;
    typedef unsigned char utf8_char;
    typedef unsigned short utf16_char;
    typedef unsigned int utf32_char;

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
            static const std::size_t utf8_lengths[64];
            static const std::ptrdiff_t encoding_sizes[5];
        };

        template<class Ch>
        struct string_tables
        {
            static const Ch nullstr[5];
            static const Ch truestr[5];
            static const Ch falsestr[6];
        };

        // Internal parse flags
        const int parse_do_swap = 1 << 30;

        //! Returns an empty string.
        template<class Ch>
        Ch* emptystr() NOEXCEPT
        {
            static Ch nul = Ch('\0');
            return &nul;
        }

        //! Returns the 'null' string
        template<class Ch>
        const Ch* nullstr() NOEXCEPT
        {
            return string_tables<Ch>::nullstr;
        }

        //! Returns the 'true' string
        template<class Ch>
        const Ch* truestr() NOEXCEPT
        {
            return string_tables<Ch>::truestr;
        }

        //! Returns the 'false' string
        template<class Ch>
        const Ch* falsestr() NOEXCEPT
        {
            return string_tables<Ch>::falsestr;
        }

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

        // The read_helper struct performs a swap if necessary. The default implementation assumes no swap. The specialization does the swap.
        template<bool swap>
        struct read_helper
        {
            template<class Ch> static Ch read(Ch val)
            {
                return val;
            }
        };

        template<>
        struct read_helper<true>
        {
            template<class Ch> static Ch read(Ch val)
            {
                if (sizeof(Ch) == 2)
                {
                    return (val >> 8) | (val << 8);
                }
                else if (sizeof(Ch) == 4)
                {
                    byte* p = (byte*)&val;
                    std::swap(p[0], p[3]);
                    std::swap(p[1], p[2]);
                    return val;
                }
                else
                {
                    assert(0); // Unsupported size for swapping
                }
            }
        };

        template<int Flags, class Ch>
        Ch read(Ch text)
        {
            return read_helper<!!(Flags & internal::parse_do_swap)>::read(text);
        }

        //! Predicate for determining if \c ch is whitespace. Uses a look-up-table.
        template<class Ch>
        struct whitespace_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return lookup_tables<0>::lookup_whitespace[(byte)ch];
            }
        };

        //! Predicate for determining if \c ch is numeric. Uses a look-up-table.
        template<class Ch>
        struct digit_pred
        {
            bool operator () (Ch ch)
            {
                if (sizeof(ch) > 1 && (ch < 0 || ch >= 256)) return false;
                return lookup_tables<0>::lookup_digit[(byte)ch];
            }
        };

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
            if (!*first && second == secondend) return 0;
            return !!*first ? 1 : -1;
        }

        //! Compares two strings. Assumes that both strings are counted--the end character
        //! points to the character immediately following the string.
        //! \return 0 if strings match fully and are the same length
        //! \return -1 if first would be sorted lexicographically before second
        //! \return 1 if first would be sorted lexicographically after second
        template<class Ch>
        int compare(const Ch* first, const Ch* firstend, const Ch* second, const Ch* secondend) NOEXCEPT
        {
            assert(first && firstend && second && secondend);
            while (first != firstend && second != secondend)
            {
                if (*first < *second) return -1;
                else if (*second < *first) return 1;
                ++first; ++second;
            }
            if (first == firstend && second == secondend) return 0;
            return first != firstend ? 1 : -1;
        }
        
        //! \brief Translates a \c Ch of a hex character to its numeric value.
        //! \param text the character to convert
        //! \param handler an error handler that is called if \c text is not a valid hex character
        //! \return the numeric representation of the given character
        template<int Flags, class Ch, class error_handler>
        byte hex_value(Ch* text, error_handler handler)
        {
            Ch ch = read<Flags>(*text);
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
        template<int Flags, class Ch, class error_handler>
        utf16_char read_utf16(Ch*& ptr, error_handler handler)
        {
            if (read<Flags>(*ptr++) != Ch('\\')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 1);
            if (read<Flags>(*ptr++) != Ch('u')) FASTJSON_PARSE_ERROR("Expected \\uXXXX", ptr - 2);
            // hex_value will throw if not a hex character
            utf16_char c =
                (hex_value<Flags>(ptr + 0, handler) << 12) |
                (hex_value<Flags>(ptr + 1, handler) << 8) |
                (hex_value<Flags>(ptr + 2, handler) << 4) |
                (hex_value<Flags>(ptr + 3, handler));
            ptr += 4;
            return c;
        }

        // These structures provide specializations that convert between any two types of encodings (UTF-8, UTF-16 BE/LE, UTF-32 BE/LE)
        template<int Flags, class Ch, std::size_t S = sizeof(Ch)> struct to_utf32
        {};

        template<int Flags, class Ch>
        struct to_utf32<Flags, Ch, 1> // UTF-8 to UTF-32
        {
            template<class error_handler> static utf32_char convert(Ch*& ch, const Ch* end, error_handler handler)
            {
                assert(ch < end);
                utf32_char out = 0;
                const utf8_char* p = (const utf8_char*)ch;
                switch (lookup_tables<0>::utf8_lengths[*p >> 2])
                {
                default:
                    FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", ch);
                    break;

                case 1:
                    out = utf32_char(*p);
                    ++ch;
                    break;

                case 2:
                    if ((end - ch) < 2)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", ch);
                    }
                    out = utf32_char(p[0] & 0x1f) << 6;
                    out |= utf32_char(p[1] & 0x3f);
                    ch += 2;
                    break;

                case 3:
                    if ((end - ch) < 3)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", ch);
                    }
                    out = utf32_char(p[0] & 0xf) << 12;
                    out |= utf32_char(p[1] & 0x3f) << 6;
                    out |= utf32_char(p[2] & 0x3f);
                    ch += 3;
                    break;

                case 4:
                    if ((end - ch) < 4)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", ch);
                    }
                    out = utf32_char(p[0] & 0x7) << 18;
                    out |= utf32_char(p[1] & 0x3f) << 12;
                    out |= utf32_char(p[2] & 0x3f) << 6;
                    out |= utf32_char(p[3] & 0x3f);
                    ch += 4;
                    break;
                }

                assert(out <= 0x10ffff);
                return out;
            }
        };

        template<int Flags, class Ch>
        struct to_utf32<Flags, Ch, 2> // UTF-16 to UTF-32
        {
            template<class error_handler> static utf32_char convert(Ch*& ch, const Ch* end, error_handler handler)
            {
                assert(ch < end);
                utf32_char out = 0;
                const utf16_char* p = (const utf16_char*)ch;
                utf16_char c = read<Flags>(p[0]);
                if (c < 0xd800 || c > 0xdfff)
                {
                    out = utf32_char(c);
                    ++ch;
                }
                else if (c < 0xdc00)
                {
                    // Surrogate pair
                    if ((end - ch) < 2)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-16 surrogate pair",ch);
                    }
                    utf16_char c2 = read<Flags>(p[1]);
                    if (c2 < 0xdc00 || c2 > 0xdfff)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-16 surrogate pair", ch);
                    }
                    out = utf32_char(c & 0x3ff) << 10;
                    out |= utf32_char(c & 0x3fff);
                    out += 0x10000;
                    ch += 2;
                }
                else
                {
                    FASTJSON_PARSE_ERROR("Invalid UTF-16 character", ch);
                }
                return out;
            }
        };

        template<int Flags, class Ch>
        struct to_utf32<Flags, Ch, 4> // UTF-32 to UTF-32
        {
            template<class error_handler> static utf32_char convert(Ch*& ch, const Ch* end, error_handler handler)
            {
                static_cast<void>(handler);
                assert(ch < end);
                utf32_char out = read<Flags>(*(const utf32_char*)ch);
                ++ch;
                return out;
            }
        };

        template<class Ch, std::size_t S = sizeof(Ch)> struct from_utf32
        {};

        template<class Ch>
        struct from_utf32<Ch, 1>
        {
            template<class error_handler> static void convert(utf32_char c, Ch*& out, const Ch* end, error_handler handler)
            {
                static_cast<void>(handler);
                assert(out < end);
                if (c <= 0x7f)
                {
                    *out++ = Ch(c);
                }
                else if (c <= 0x7ff)
                {
                    assert((end - out) >= 2);
                    *out++ = Ch(0xc0 + ((c >> 6) & 0x3f));
                    *out++ = Ch(0x80 + ( c       & 0x3f));
                }
                else if (c < 0x10000)
                {
                    assert((end - out) >= 3);
                    *out++ = Ch(0xe0 + ((c >> 12) & 0x0f));
                    *out++ = Ch(0x80 + ((c >> 6)  & 0x3f));
                    *out++ = Ch(0x80 + ( c        & 0x3f));
                }
                else
                {
                    assert((end - out) >= 4);
                    *out++ = Ch(0xf0 + ((c >> 18) & 0x07));
                    *out++ = Ch(0x80 + ((c >> 12) & 0x3f));
                    *out++ = Ch(0x80 + ((c >> 6)  & 0x3f));
                    *out++ = Ch(0x80 + ( c        & 0x3f));
                }
            }
        };

        template<class Ch>
        struct from_utf32<Ch, 2>
        {
            template<class error_handler> static void convert(utf32_char c, Ch*& out, const Ch* end, error_handler handler)
            {
                static_cast<void>(handler);
                assert(out < end);
                if (c < 0x10000)
                {
                    assert(c < 0xd800 || c > 0xdfff);
                    *out++ = Ch(c);
                }
                else
                {
                    assert((end - out) >= 2);
                    c -= 0x10000;
                    *out++ = Ch(0xd800 | (c >> 10));
                    *out++ = Ch(0xdc00 | (c & 0x3ff));
                }
            }
        };

        template<class Ch>
        struct from_utf32<Ch, 4>
        {
            template<class error_handler> static void convert(utf32_char c, Ch*& out, const Ch* end, error_handler handler)
            {
                static_cast<void>(handler);
                assert(out < end);
                *out++ = c;
            }
        };

        template<int Flags, class ChIn, class ChOut, std::size_t SIn = sizeof(ChIn), std::size_t SOut = sizeof(ChOut)>
        struct unicode_converter
        {
            template<class error_handler> static void convert(ChIn*& in, const ChIn* end, ChOut*& out, const ChOut* outend, error_handler handler)
            {
                const utf32_char c = to_utf32<Flags, ChIn>::convert(in, end, handler);
                from_utf32<ChOut>::convert(c, out, outend, handler);
            }
            template<class error_handler> static std::size_t measure(ChIn*& in, const ChIn* end, error_handler handler)
            {
                const utf32_char c = to_utf32<Flags, ChIn>::convert(in, end, handler);
                ChOut buf[6];
                ChOut* p = buf;
                // Do the conversion to actually measure
                from_utf32<ChOut>::convert(c, p, &buf[6], handler);
                return p - buf;
            }
        };

        template<int Flags, class ChIn, class ChOut>
        struct unicode_converter<Flags, ChIn, ChOut, 1, 1> // UTF-8 to UTF-8 converter
        {
            template<class error_handler> static void convert(ChIn*& in, const ChIn* end, ChOut*& out, const ChOut* outend, error_handler handler)
            {
                assert(in != end);
                const utf8_char* p = (const utf8_char*)in;
                utf8_char c = read<Flags>(p[0]);
                const std::size_t size = lookup_tables<0>::utf8_lengths[c >> 2];
                if (size == 0 || (in + size) > end)
                {
                    FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", in);
                }
                assert((std::size_t)(outend - out) >= size);

                switch (size)
                {
                default:
                    FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", in);
                    break;

                case 4: *out++ = ChOut(read<Flags>(*in++)); // Fall through
                case 3: *out++ = ChOut(read<Flags>(*in++)); // Fall through
                case 2: *out++ = ChOut(read<Flags>(*in++)); // Fall through
                case 1: *out++ = ChOut(read<Flags>(*in++)); // Fall through
                }
            }
            template<class error_handler> static std::size_t measure(ChIn*& in, const ChIn* end, error_handler handler)
            {
                assert(in != end);
                const utf8_char* p = (const utf8_char*)in;
                const std::size_t size = lookup_tables<0>::utf8_lengths[read<Flags>(*p) >> 2];
                if (size == 0 || (std::size_t)(end - in) < size)
                {
                    FASTJSON_PARSE_ERROR("Invalid UTF-8 sequence", in);
                }
                in += size;
                return size;
            }
        };

        template<int Flags, class ChIn, class ChOut>
        struct unicode_converter<Flags, ChIn, ChOut, 2, 2> // UTF-16 to UTF-16 converter
        {
            template<class error_handler> static void convert(ChIn*& in, const ChIn* end, ChOut*& out, const ChOut* outend, error_handler handler)
            {
                assert(in != end); assert(out != outend);
                const utf16_char* p = (const utf16_char*)in;
                utf16_char c = read<Flags>(*p);
                *out++ = (ChOut)c;

                ++in;
                if (c >= 0xd800 && c < 0xdc00)
                {
                    assert(in != end);
                    assert(out != outend);
                    c = read<Flags>(p[1]);
                    if (c < 0xdc00 || c > 0xdfff)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-16 surrogate pair", in);
                    }
                    *out++ = c;
                    ++in;
                }
            }
            template<class error_handler> static std::size_t measure(ChIn*& in, const ChIn* end, error_handler handler)
            {
                assert(in != end);
                const utf16_char* p = (const utf16_char*)in;
                ++in;
                utf16_char c = read<Flags>(p[0]);
                if (c >= 0xd800 && c < 0xdc00)
                {
                    assert(in != end);
                    c = read<Flags>(p[1]);
                    if (c < 0xdc00 || c > 0xdfff)
                    {
                        FASTJSON_PARSE_ERROR("Invalid UTF-16 surrogate pair", in);
                    }
                    ++in;
                    return 2;
                }
                return 1;
            }
        };

        template<int Flags, class ChIn, class ChOut>
        struct unicode_converter<Flags, ChIn, ChOut, 4, 4> // UTF-32 to UTF-32 converter
        {
            template<class error_handler> static void convert(ChIn*& in, const ChIn* end, ChOut*& out, const ChOut* outend, error_handler handler)
            {
                static_cast<void>(handler);
                assert(in != end);
                assert(out != outend);
                *out++ = ChOut(read<Flags>(*in++));
            }
            template<class error_handler> static std::size_t measure(ChIn*& in, const ChIn* end, error_handler handler)
            {
                static_cast<void>(handler);
                assert(in != end);
                ++in;
                return 1;
            }
        };

        template<int Flags, class ChIn, class ChOut, class error_handler>
        void convert(ChIn*& in, const ChIn* end, ChOut*& out, const ChOut* outend, error_handler handler)
        {
            unicode_converter<Flags, ChIn, ChOut>::convert(in, end, out, outend, handler);
        }

        template<int Flags, class ChOut, class ChIn, class error_handler>
        std::size_t measure(ChIn*& in, const ChIn* end, error_handler handler)
        {
            return unicode_converter<Flags, ChIn, ChOut>::measure(in, end, handler);
        }
    
    } // namespace internal



    ///////////////////////////////////////////////////////////////////////////
    // Parsing flags

    //! Parse flags which represent the default behavior of the parser.
    //! This is always zero, so that flags may be or'd together.
    //! See json_document::parse() function.
    const int parse_default = 0;

    const int parse_no_string_terminators = 1 << 0; //!< Don't terminate string values with NUL characters. Use json_value::nameend() to get the end of the string. Mutually exclusive with parse_force_string_terminators.

    const int parse_no_inline_translation = 1 << 1; //!< Don't translate strings in-line with the given data buffer. Instead, a copy is made and strings are fixed there.

    const int parse_force_string_terminators = 1 << 2; //<! Ensures that all strings are copied, so that translation is not inline but string terminators are present. This ensures a non-destructive parse but is less efficient than parse_non_destructive. Mutually exclusive with parse_no_string_terminators.

    const int parse_non_destructive = (parse_no_string_terminators|parse_no_inline_translation); //!< Ensures that the buffer passed to json_document::parse is not modified. Does not always terminate strings; use json_value::nameend() to get the end of the string.
    const int parse_non_destructive_nul = (parse_force_string_terminators); //!< Ensures that the buffer passed to json_document::parse is not modified. Strings are copied and NUL-terminated. Slightly less efficient than parse_non_destructive.

    // These flags are non-RFC-7159 compliant, but helpful
    const int parse_trailing_commas = 1 << 3; //<! Allows commas to exist at the end of objects and arrays
    const int parse_comments = 1 << 4; //<! Allows comments (//, /**/, #) to exist

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
        typedef static_pool<static_size> pool_type;
    public:
        //! Constructor
        memory_pool() NOEXCEPT { init(); }

        //! Destructor. Frees all of the memory used by this memory_pool.
        ~memory_pool() { clear(); }

        //! \brief Clears all allocations from this memory pool back to the heap.
        void clear()
        {
            while (pool_ != pool_type::static_pool_start())
            {
                byte* next = reinterpret_cast<header*>(align_forward(pool_))->next_pool_;
                T_ALLOC::raw_heap_free(pool_);
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
            pool_ = pool_type::static_pool_start();
            next_ = align_forward(pool_);
            end_ = pool_type::static_pool_end();
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

            byte* pool = T_ALLOC::raw_heap_alloc(size);
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
        template<class Ch_, class error_handler, class pool> friend class json_document;
        friend class json_object<Ch>;

        //! Constructor. Values may only be created by the json_document.
        json_value(value_type type = value_null, Ch* name = 0, Ch* nameend = 0) NOEXCEPT
            : type_(type)
            , name_(name)
            , nameend_(nameend)
            , value_(internal::emptystr<Ch>())
            , valueend_(internal::emptystr<Ch>())
            , owner_(0)
            , prev_(0)
            , next_(0)
        {
            if (type == value_null)
            {
                value_ = (Ch*)internal::string_tables<Ch>::nullstr;
                valueend_ = value_ + 4;
            }
        }
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
        bool   as_boolean() const { return internal::value_to_boolean(value_, valueend_); }

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

        //! Returns a shared json_value that has type value_null.
        static json_value<Ch>* null() NOEXCEPT
        {
            static json_value<Ch> val;
            return &val;
        }

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
        template<class Ch_, class error_handler, class pool> friend class json_document;

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
        const json_value<Ch>& at(const Ch* name, const Ch* nameend = NULL) const NOEXCEPT
        {
            assert(this->type_ == value_object);
            for (const json_value<Ch>* p = child_; p; p = p->next_)
            {
                if (nameend
                        ? internal::compare(name, nameend, p->name_, p->nameend_) == 0
                        : internal::compare(name, p->name_, p->nameend_) == 0)
                {
                    return *p;
                }
            }
            return *json_value<Ch>::null();
        }

        //! \brief For a json array (or object), accesses a member by index.
        //! Has time order O(n).
        //! \param index The index to access. Negative numbers start at the last position (-1 is the last). Zero starts at the first position.
        //! \return the json_value at the specified index.
        //! \return For an invalid index, a json_value of type value_null is returned.
        const json_value<Ch>& at(int index) const NOEXCEPT
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
            return p ? *p : *json_value<Ch>::null();
        }

        //! \brief For a json object, accesses a member by name.
        //! The name is case-sensitive. Has time order O(n).
        //! If the value does not exist, a json_value of type value_null is returned.
        const json_value<Ch>& operator [] (const Ch* name) const NOEXCEPT
        {
            return at(name);
        }

        //! \brief For a json array (or object), accesses a member by index.
        //! Has time order O(n).
        //! \param index The index to access. Negative numbers start at the last position (-1 is the last). Zero starts at the first position.
        //! \return the json_value at the specified index.
        //! \return For an invalid index, a json_value of type value_null is returned.
        const json_value<Ch>& operator [] (int index) const NOEXCEPT
        {
            return at(index);
        }

        //! \brief Adds the given value to the end of the array.
        //! \param val A json_value that has been allocated by any of the allocation functions in json_document.
        //! \return true if the json_value was added. Returns false if not an array or \c val is a NULL pointer or already exists in an array or object.
        bool array_add(json_value<Ch>* val) NOEXCEPT
        {
            if (!this->is_array() || val == 0 || val->owner_ != 0) return false;
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
            if (!this->is_array() || val == 0 || val->owner_ != 0) return false;
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
            if (!this->is_array()) return 0;
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

        //! \brief Replaces an existing array entry with the given entry.
        //! Has time order O(n).
        //! \param index The index to set. If the index doesn't exist, false is returned.
        //! \param val The value to set.
        bool array_set(int index, json_value<Ch>* val)
        {
            if (!this->is_array()) return false;

            json_value<Ch>* p;
            if (index >= 0)
            {
                p = child_;
                while (index > 0 && p)
                {
                    --index;
                    p = p->next_;
                }
            }
            else
            {
                // Negative index
                p = lastchild_;
                while (++index < 0 && p)
                {
                    p = p->prev_;
                }
            }
            if (index == 0)
            {
                if (!p)
                {
                    // Append to end
                    if (lastchild_)
                    {
                        lastchild_->next_ = val;
                        val->prev_ = lastchild_;
                        val->next_ = NULL;
                        lastchild_ = val;
                    }
                    else
                    {
                        child_ = lastchild_ = val;
                        val->prev_ = val->next_ = NULL;
                    }                        
                }
                else
                {
                    // Replace existing
                    if (p->prev_) { p->prev_->next_ = val; val->prev_ = p->prev_; } else { child_ = val; val->prev_ = NULL; }
                    if (p->next_) { p->next_->prev_ = val; val->next_ = p->next_; } else { lastchild_ = val; val->next_ = NULL; }
                    p->prev_ = p->next_ = NULL;
                }                    
                return true;
            }
            return false;
        }

        //! \brief Sets a value by name on a json object.
        //! Has time order O(n).
        //! \param name The case-sensitive name to assign to \c val. If the name is already in use, the previous value is replaced.
        //! \param val The value to set within the object that has been allocated by any of the allocation functions in json_document.
        //! \param old An optional pointer that receives the replaced value (or NULL if a value was not replaced).
        bool object_set(const Ch* name, json_value<Ch>* val, json_value<Ch>** old = 0) NOEXCEPT
        {
            if (old) *old = 0;
            if (!this->is_object()) return false;
            if (name == 0 || *name == Ch('\0')) return false;
            if (val == 0 || val->owner_ != 0) return false;
            val->name_ = const_cast<Ch*>(name);
            val->nameend_ = val->name_ + internal::length(name);
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
                add_child(val);
            }
            return true;
        }

        //! \brief Removes a value from a json object.
        //! Has time order O(n).
        //! \param name The case-sensitive name of the value to remove.
        //! \return The removed json_value object if found. NULL if nothing was found. The returned json_value will be automatically destroyed when the json_document that constructed it is destroyed.
        json_value<Ch>* object_remove(const Ch* name) NOEXCEPT
        {
            if (!this->is_object()) return 0;
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
            while (child_)
            {
                json_value<Ch>* next = p->next_;
                child_->owner_ = 0;
                child_->prev_ = child_->next_ = 0;
                child_ = next;
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
    //! \tparam pool Allows overriding the memory pool for this json_document object. See memory_pool for more information.
    template<class Ch = char, class error_handler = default_error_handler, class pool = memory_pool<> >
    class json_document : protected pool, protected error_handler
    {
        //! Disable copy constructor and assignment
        json_document(const json_document&);
        json_document& operator = (const json_document&);

    public:
        typedef Ch char_t;
        typedef error_handler error_handler_t;
        typedef pool pool_t;

        //! Constructor
        json_document()
            : root_(allocate_object())
        {}

        json_document(error_handler_t handler)
            : error_handler(handler)
            , root_(allocate_object())
        {}

        error_handler_t& get_error_handler() { return *this; }
        const error_handler_t& get_error_handler() const { return *this; }

        enum encoding
        {
            unknown = -1,   //!< Unknown encoding; must be determined by parser

            utf8,       //!< UTF-8 encoding
            utf16,      //!< UTF-16 encoding using native endianness
            utf16_swap, //!< UTF-16 encoding using non-native endianness
            utf32,      //!< UTF-32 encoding using native endianness
            utf32_swap, //!< UTF-32 encoding using non-native endianness

            num_encoding//!< Count of encoding values
        };

        //! \brief Parses a json document. The root object is reset before parsing.
        //! \tparam Flags The flags to use when parsing.
        //! \param data The json data to process. The parse() function will automatically determine encoding (utf-8, utf-16 big-endian/little-endian, utf-32 big-endian/little-endian).
        //! \param num_bytes The size of \c data in bytes. The default (-1) indicates that the data is NUL-terminated. Required if \c enc is \c unknown.
        //! \param enc The type of character encoding used by data. If \c unknown is specified, \c num_bytes must be a value other than (-1).
        template<int Flags> void parse(void* data, std::size_t num_bytes = std::size_t(-1), encoding enc = unknown)
        {
            assert((Flags & (parse_no_string_terminators | parse_force_string_terminators)) != (parse_no_string_terminators | parse_force_string_terminators)); // Mutual exclusion failure.
            assert(enc >= unknown && enc < num_encoding); // Invalid encoding value
            assert(enc != unknown || num_bytes != std::size_t(-1)); // Encoding must be known if NUL-terminated.

            if (data == NULL || num_bytes == 0)
            {
                FASTJSON_PARSE_ERROR_THIS("Expected '{' or '['", data);
            }

            if(enc == unknown)
            {
                // May throw an error
                enc = determine_encoding<Flags>((byte*)data, num_bytes);
            }
            assert(enc != unknown);

            // Compute the end
            const byte* end = ((byte*)data) + num_bytes;
            if (end < data)
            {
                // Data must be NUL-terminated, so use PTRDIFF_MAX distance (or the end of memory) as the end value
                assert(sizeof(byte*) == sizeof(std::size_t));
                std::ptrdiff_t encSize = internal::lookup_tables<0>::encoding_sizes[enc];
                end = ((byte*)data) + (PTRDIFF_MAX & -encSize);
                if (end < data)
                {
                    end = (byte*)(std::size_t(-1) & -encSize);
                    end += (std::size_t(data) & (encSize - 1));
                }
            }

            switch (enc)
            {
            case utf8:  parse_internal<Flags>((utf8_char*)data, (utf8_char*)end); break;
            case utf16: parse_internal<Flags>((utf16_char*)data, (utf16_char*)end); break;
            case utf16_swap: parse_internal<Flags | internal::parse_do_swap>((utf16_char*)data, (utf16_char*)end); break;
            case utf32: parse_internal<Flags>((utf32_char*)data, (utf32_char*)end); break;
            case utf32_swap: parse_internal<Flags | internal::parse_do_swap>((utf32_char*)data, (utf32_char*)end); break;
            default: FASTJSON_PARSE_ERROR_THIS("Unknown encoding type", data);
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
        Ch* allocate_string(const Ch* str, std::size_t len = std::size_t(~0))
        {
            if (str)
            {
                if (len == std::size_t(~0))
                {
                    len = internal::length(str);
                }
                Ch* newstr = (Ch*)alloc(sizeof(Ch) * (len + 1));
                Ch* p = newstr;
                while (len--)
                {
                    *p++ = *str++;
                }
                *p = Ch('\0');
                return newstr;
            }
            return internal::emptystr<Ch>();
        }

        //! \brief Allocates a json null value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_null_value()
        {
            json_value<Ch>* pval = allocate_value(value_null);
            return pval;
        }

        //! \brief Allocates a json boolean value that can be used for json_object::array_add, json_object::array_insert or json_object::object_set.
        //! \return The json_value representation. Will never be NULL (but the error_handler will be triggered if the memory_pool fails to allocate).
        json_value<Ch>* allocate_bool_value(bool val)
        {
            json_value<Ch>* pval = allocate_value(value_bool);
            if (val)
            {
                pval->value_ = (Ch*)internal::string_tables<Ch>::truestr;
                pval->valueend_ = pval->value_ + 4;
            }
            else
            {
                pval->value_ = (Ch*)internal::string_tables<Ch>::falsestr;
                pval->valueend_ = pval->value_ + 5;
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
            pval->value_ = const_cast<Ch*>(value);
            pval->valueend_ = pval->value_ + internal::length(value);
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
            Ch* bufend = buffer + 128;
            if (!internal::number_to_string(val, buffer, bufend))
            {
                // Actually a string due to infinite or NaN
                pval->type_ = value_string;
            }

            Ch* sval = allocate_string(buffer, bufend - buffer);
            pval->value_ = sval;
            pval->valueend_ = sval + (bufend - buffer);
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
        template<int Flags> encoding determine_encoding(byte* data, const std::size_t num_bytes)
        {
            if(num_bytes == std::size_t(-1))
            {
                FASTJSON_PARSE_ERROR_THIS("Encoding must be specified with NUL-terminated data", data);
            }

            const std::size_t m = num_bytes % 4;
            if (m != 2 && m != 0)
            {
                // Odd number of bytes; must be UTF-8.
                return utf8;
            }

            assert(sizeof(utf16_char) == 2); // Test assumption
            assert(sizeof(utf32_char) == 4); // Test assumption
            const utf8_char*  cutf8  = (const utf8_char*) data;
            const utf16_char* cutf16 = (const utf16_char*)data;
            const utf32_char* cutf32 = (const utf32_char*)data;

            // May still be UTF-8; need to check both bytes
            if (cutf8[0] && cutf8[1])
            {
                return utf8;
            }

            if (cutf16[0] && cutf16[1])
            {
                // Must be UTF-16
                return cutf16[0] < 256 ? utf16 : utf16_swap;
            }

            if (cutf32[0] == 0)
            {
                FASTJSON_PARSE_ERROR_THIS("Unable to determine encoding", data);
            }
            return cutf32[0] < 256 ? utf32 : utf32_swap;
        }

        // Uses placement new to instantiate a json_value from the memory_pool.
        json_value<Ch>* allocate_value(value_type type = value_null)
        {
            return new (alloc(sizeof(json_value<Ch>))) json_value<Ch>(type, internal::emptystr<Ch>(), internal::emptystr<Ch>());
        }

        // Uses placement new to instantiate a json_object from the memory_pool.
        json_object<Ch>* allocate_object(value_type type)
        {
            return new (alloc(sizeof(json_object<Ch>))) json_object<Ch>(type, internal::emptystr<Ch>(), internal::emptystr<Ch>());
        }

        // Internal function to allocate \c size bytes from the given memory_pool (pool).
        // Invokes the error_handler if memory allocation fails.
        byte* alloc(std::size_t size)
        {
            byte* p = pool::alloc(size);
            if (p == 0)
            {
                FASTJSON_PARSE_ERROR_THIS("Memory allocation failed", 0);
            }
            return p;
        }

        // Advances \c data until the character at the front is not of type \c StopPred.
        template<template <class> class StopPred, int Flags, class ChIn>
        static void skip(ChIn*& data, const ChIn* end)
        {
            StopPred<ChIn> pred;
            while ((data < end) && (pred(internal::read<Flags>(*data))))
            {
                ++data;
            }
        }

        // Internal parser based on character type
        template<int Flags, class ChIn> void parse_internal(ChIn* data, const ChIn* end)
        {
            skip_whitespace_and_comments<Flags>(data, end);

            if (data == end)
            {
                FASTJSON_PARSE_ERROR_THIS("Expected '{' or '['", data);
            }

            // Must encounter an object or an array
            if (internal::read<Flags>(*data) == ChIn('{'))
            {
                root_ = parse_object<Flags>(allocate_object(), ++data, end);
            }
            else if (internal::read<Flags>(*data) == ChIn('['))
            {
                root_ = parse_array<Flags>(allocate_object(), ++data, end);
            }
            else
            {
                FASTJSON_PARSE_ERROR_THIS("Expected '{' or '['", data);
            }

            skip_whitespace_and_comments<Flags>(data, end);

            if (data != end && internal::read<Flags>(*data) != ChIn(0))
            {
                FASTJSON_PARSE_ERROR_THIS("Expected end of document", data);
            }
        }

        template<int Flags, class ChIn> void skip_whitespace_and_comments(ChIn*& data, const ChIn* end)
        {
            if (!!(Flags & parse_comments) && data != end)
            {
                bool b;
                do
                {
                    b = false;

                    // Skip any whitespace
                    skip<internal::whitespace_pred, Flags>(data, end);

                    switch (internal::read<Flags>(*data))
                    {
                    case ChIn('#'):
                        // Line comment. Read until newline
                        ++data;
                        while (data != end && internal::read<Flags>(*data) != ChIn('\n'))
                            ++data;
                        b = true;
                        break;

                    case ChIn('/'):
                        // Line or multi-line comment
                        if ((data + 1) != end)
                        {
                            if (internal::read<Flags>(*(data + 1)) == ChIn('/'))
                            {
                                data += 2;
                                while (data != end && internal::read<Flags>(*data) != ChIn('\n'))
                                    ++data;
                                b = true;
                            }
                            else if (internal::read<Flags>(*(data + 1)) == ChIn('*'))
                            {
                                data += 2;
                                while (data != end)
                                {
                                    if ((end - data) >= 2 && internal::read<Flags>(*data) == ChIn('*') && internal::read<Flags>(*(data + 1)) == ChIn('/'))
                                    {
                                        data += 2;
                                        b = true;
                                        break;
                                    }
                                    ++data;
                                }
                            }
                        }
                        break;
                    }
                } while (b);
            }
            else
            {
                // Just skip whitespace
                skip<internal::whitespace_pred, Flags>(data, end);
            }
        }


        // Internal helper function for parsing a json object. Advances \c text past the object.
        template<int Flags, class ChIn> json_object<Ch>* parse_object(json_object<Ch>* val, ChIn*& data, const ChIn* end)
        {
            json_value<Ch>* lastchild = 0;
            val->type_ = value_object;

            skip_whitespace_and_comments<Flags>(data, end);

            // Check for empty object
            if (data != end && internal::read<Flags>(*data) == ChIn('}'))
            {
                ++data;
                return val;
            }

            // Not necessary, but a more correct parse error.
            if (data == end || internal::read<Flags>(*data) != ChIn('\"')) { FASTJSON_PARSE_ERROR_THIS("Expected end-of-object '}' or name (string)", data); }

            for (;;)
            {
                if (data == end || internal::read<Flags>(*data) != ChIn('\"')) { FASTJSON_PARSE_ERROR_THIS("Expected name (string)", data); }

                // Read the name
                Ch* namebegin;
                Ch* nameend;
                parse_string<Flags>(++data, end, namebegin, nameend);

                // ws : ws (name separator)
                skip_whitespace_and_comments<Flags>(data, end);
                if (data == end || internal::read<Flags>(*data) != ChIn(':')) { FASTJSON_PARSE_ERROR_THIS("Expected name separator (:)", data); }
                ++data;
                skip_whitespace_and_comments<Flags>(data, end);

                json_value<Ch>* child = parse_value<Flags>(data, end);
                child->name_ = namebegin;
                child->nameend_ = nameend;

                val->add_child(child);

                lastchild = child;

                // Eat whitespace and an optional value-separator
                skip_whitespace_and_comments<Flags>(data, end);

                if (data != end && internal::read<Flags>(*data) == ChIn(','))
                {
                    ++data;
                    skip_whitespace_and_comments<Flags>(data, end);

                    // Close off the last value
                    if ((Flags & (parse_no_string_terminators|parse_force_string_terminators)) == 0 && *lastchild->valueend_ != Ch('\0'))
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }

                    if (!!(Flags & parse_trailing_commas))
                    {
                        // If trailing commas are allowed, see if this is the end of the object
                        if (data != end && internal::read<Flags>(*data) == ChIn('}'))
                        {
                            // End of object
                            ++data;
                            break;
                        }
                    }
                }
                else if (data != end && internal::read<Flags>(*data) == ChIn('}'))
                {
                    // End of object
                    ++data;

                    // Close off the last value
                    if ((Flags & (parse_no_string_terminators|parse_force_string_terminators)) == 0 && *lastchild->valueend_ != Ch('\0'))
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                    break;
                }
                else
                {
                    FASTJSON_PARSE_ERROR_THIS("Expected value-separator ',' or end-of-object '}'", data);
                }
            }
            return val;
        }

        // Internal helper function for parsing a json array. Advances \c text past the array.
        template<int Flags, class ChIn> json_object<Ch>* parse_array(json_object<Ch>* val, ChIn*& data, const ChIn* end)
        {
            json_value<Ch>* lastchild = 0;
            val->type_ = value_array;

            skip_whitespace_and_comments<Flags>(data, end);

            // Check for empty array
            if (data != end && internal::read<Flags>(*data) == ChIn(']'))
            {
                ++data;
                return val;
            }

            for (;;)
            {
                json_value<Ch>* child = parse_value<Flags>(data, end);

                val->add_child(child);

                lastchild = child;

                // Eat whitespace
                skip_whitespace_and_comments<Flags>(data, end);

                if (data != end && internal::read<Flags>(*data) == ChIn(','))
                {
                    ++data;
                    skip_whitespace_and_comments<Flags>(data, end);

                    // Close off the last value
                    if ((Flags & (parse_no_string_terminators|parse_force_string_terminators)) == 0 && *lastchild->valueend_ != Ch('\0'))
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }

                    if (!!(Flags & parse_trailing_commas))
                    {
                        // If trailing commas are allowed, see if this is the end of the array
                        if (data != end && internal::read<Flags>(*data) == ChIn(']'))
                        {
                            // End of array
                            ++data;
                            break;
                        }
                    }
                }
                else if (data != end && internal::read<Flags>(*data) == ChIn(']'))
                {
                    // End of array
                    ++data;

                    // Close off the last value
                    if ((Flags & (parse_no_string_terminators|parse_force_string_terminators)) == 0 && *lastchild->valueend_ != Ch('\0'))
                    {
                        *lastchild->valueend_ = Ch('\0');
                    }
                    break;
                }
                else
                {
                    FASTJSON_PARSE_ERROR_THIS("Expected value-separator ',' or end-of-array ']'", data);
                }
            }
            return val;
        }

        // Internal helper function for measuring a string. This must be done if copy-and-translate
        // parse flags are used. Can trigger errors if a parse error occurs.
        // \param data In: The start of the string; Out: will be at the end of the string.
        // \param end The end of valid input data
        // \param outlen Receives the required string length (not including NUL) for the translated string
        // \return \c true if translation is required; false otherwise
        template<int Flags, class ChIn> bool measure_string(ChIn*& data, const ChIn* end, std::size_t& outlen)
        {
            bool translate_required = !!(Flags & internal::parse_do_swap) || (sizeof(Ch) != sizeof(ChIn)); // Always require translate if we're doing swapping or the output character size differs
            outlen = 0;
            while (data < end)
            {
                const ChIn c = internal::read<Flags>(*data);
                switch (c)
                {
                default:
                    outlen += internal::measure<Flags, Ch>(data, end, static_cast<error_handler&>(*this));
                    break;

                case ChIn('\"'): // End of string
                    return translate_required;

                case ChIn('\\'): // Escaped character
                    translate_required = true;
                    if ((data + 1) >= end)
                    {
                        FASTJSON_PARSE_ERROR_THIS("Invalid escaped character", data + 1);
                    }

                    switch (internal::read<Flags>(*++data))
                    {
                    default:
                        FASTJSON_PARSE_ERROR_THIS("Invalid escaped character", data);
                        break;

                    case ChIn('"'):
                    case ChIn('\\'):
                    case ChIn('/'):
                    case ChIn('b'): // Backspace
                    case ChIn('f'): // Form feed
                    case ChIn('n'): // Line feed
                    case ChIn('r'): // Carriage return
                    case ChIn('t'): // Tab
                        ++data;
                        ++outlen;
                        break;

                    case ChIn('u'): // UTF-16 character
                        --data; // Rewind to the backslash
                        if ((end - data) < 6)
                        {
                            FASTJSON_PARSE_ERROR_THIS("Invalid \\u escape sequence", data);
                        }

                        {
                            utf16_char cu[2];
                            cu[0] = internal::read_utf16<Flags>(data, static_cast<error_handler&>(*this));
                            if (cu[0] >= 0xd800 && cu[0] <= 0xdfff)
                            {
                                if ((end - data) < 6)
                                {
                                    FASTJSON_PARSE_ERROR_THIS("Expected UTF-16 surrogate pair", data);
                                }
                                // UTF-16 surrogate pair; read the next required character
                                cu[1] = internal::read_utf16<Flags>(data, static_cast<error_handler&>(*this));
                                if (cu[1] < 0xdc00 || cu[1] > 0xdfff)
                                {
                                    FASTJSON_PARSE_ERROR_THIS("Expected UTF-16 surrogate pair", data - 6);
                                }
                            }
                            // We're converting to whatever Ch is, so actually do the conversion into a dummy buffer that we can measure.
                            utf16_char* p = cu;
                            outlen += internal::measure<0, Ch>(p, p + 2, static_cast<error_handler&>(*this));
                        }
                        break;
                    }
                    break;

                case ChIn('\0'):
                    // Encountered a NUL character
                    FASTJSON_PARSE_ERROR_THIS("Expected end-of-string '\"'", data);
                    break;
                }
            }
            // Ran out of characters
            FASTJSON_PARSE_ERROR_THIS("Expected end-of-string '\"'", data);
        }

        template<int Flags, class ChIn> std::size_t measure_number(ChIn*& data, const ChIn* end)
        {
            const ChIn* start = data;

            // A minus is allowed as the first character
            if (data < end && internal::read<Flags>(*data) == ChIn('-'))
            {
                ++data;
            }

            // A single zero or a series of digits
            if (data < end && internal::read<Flags>(*data) == ChIn('0'))
            {
                ++data;
            }
            else
            {
                ChIn* skipstart = data;
                skip<internal::digit_pred,Flags>(data, end);
                if (data == skipstart) { FASTJSON_PARSE_ERROR_THIS("Expected digit", data); }
            }

            // Optional decimal point
            if (data < end && internal::read<Flags>(*data) == ChIn('.'))
            {
                ++data;
                ChIn* skipstart = data;
                skip<internal::digit_pred,Flags>(data, end);
                if (data == skipstart) { FASTJSON_PARSE_ERROR_THIS("Expected fractional digits", data); }
            }

            // Optional exponent
            if (data < end)
            {
                ChIn c = internal::read<Flags>(*data);
                if (c == ChIn('e') || c == ChIn('E'))
                {
                    ++data;

                    // Optional +/-
                    if (data < end)
                    {
                        c = internal::read<Flags>(*data);
                        if (c == ChIn('+') || c == ChIn('-'))
                        {
                            ++data;
                        }
                    }

                    // Digits
                    ChIn* skipstart = data;
                    skip<internal::digit_pred,Flags>(data, end);
                    if (data == skipstart) { FASTJSON_PARSE_ERROR_THIS("Expected exponent digits", data); }
                }
            }

            return (data - start);
        }

        // Internal helper function for parsing a json string. Advances \c text past the string.
        // \c begin and \c end receive the string pointers.
        // Translation is required if internal::parse_do_swap is set, Ch differs in size to ChIn or if the measure determines that characters must be translated
        // The following table indicates how allocation/copying will take place:
        //   sizeof(Ch) > sizeof(ChIn) - always requires allocation, translate
        //   parse_force_string_terminators - always requires allocation, translate
        //   parse_no_string_terminators - will destructively translate (if translate required) unless combined with parse_no_inline_translation, otherwise just point to inline data
        //   parse_no_inline_translation - alloc/copy if translate required. Destructive NUL termination happens if parse_no_string_terminators is not present.
        template<int Flags, class ChIn> void parse_string(ChIn*& data, const ChIn* end, Ch*& strbegin, Ch*& strend)
        {
            // How strings are parsed depends heavily on flags.
            bool require_alloc = (sizeof(Ch) > sizeof(ChIn)) || (Flags & parse_force_string_terminators);
            bool translate_required = true;
            ChIn* str_end = data;
            Ch* out = (Ch*)data;
            Ch* out_end = (Ch*)end;
            std::size_t outlen = std::size_t(-1);
            if (require_alloc || (Flags & (parse_force_string_terminators | parse_no_string_terminators | parse_no_inline_translation | internal::parse_do_swap)) != 0)
            {
                // Measure the string first
                translate_required = measure_string<Flags>(str_end, end, outlen);
            }

            if (((Flags & parse_no_string_terminators) != 0) && !translate_required)
            {
                // String doesn't need translation and we don't need string terminators. This is the fastest case.
                // We can early out.
                assert(sizeof(ChIn) == sizeof(Ch));
                assert((Flags & internal::parse_do_swap) == 0);
                strbegin = (Ch*)data;
                strend = (Ch*)str_end;
                data = (str_end + 1); // Skip the closing "
                return;
            }

            if ((Flags & (parse_no_inline_translation)) != 0)
            {
                require_alloc = true;
            }

            if (require_alloc)
            {
                // Must copy
                assert(outlen != std::size_t(-1));
                out = (Ch*)alloc(sizeof(Ch) * (outlen + 1));
                out_end = out + outlen + 1;
            }

            strbegin = out;

            while (data < end)
            {
                switch (internal::read<Flags>(*data))
                {
                default:
                    internal::convert<Flags>(data, end, out, out_end, static_cast<error_handler&>(*this));
                    break;

                case ChIn('\"'):  // End of string
                    // Even if parse_no_string_terminators is specified, this should be an allocated copy
                    *(strend = out) = Ch('\0');
                    ++data;
                    return;

                case ChIn('\\'):  // Escaped character
                    switch (internal::read<Flags>(*++data))
                    {
                    default:
                        FASTJSON_PARSE_ERROR_THIS("Invalid escaped character", data);
                        break;

                    case ChIn('"'):
                    case ChIn('\\'):
                    case ChIn('/'):
                        internal::convert<Flags>(data, end, out, out_end, static_cast<error_handler&>(*this));
                        break;

                    case ChIn('b'): // Backspace
                        *out++ = Ch('\x08'); ++data;
                        break;

                    case ChIn('f'): // Form feed
                        *out++ = Ch('\x0c'); ++data;
                        break;

                    case ChIn('n'): // Line feed
                        *out++ = Ch('\x0a'); ++data;
                        break;

                    case ChIn('r'): // carriage return
                        *out++ = Ch('\x0d'); ++data;
                        break;

                    case ChIn('t'): // tab
                        *out++ = Ch('\x09'); ++data;
                        break;

                    case ChIn('u'): // UTF-16 character
                        --data; // Rewind to the backslash
                        {
                            utf16_char c[2];
                            if ((end - data) < 6)
                            {
                                FASTJSON_PARSE_ERROR_THIS("Invalid \\u escape sequence", data);
                            }
                            c[0] = internal::read_utf16<Flags>(data, static_cast<error_handler&>(*this));
                            if (c[0] >= 0xd800 && c[0] <= 0xdfff)
                            {
                                if ((end - data) < 6)
                                {
                                    FASTJSON_PARSE_ERROR_THIS("Expected UTF-16 surrogate pair", data);
                                }
                                // UTF-16 surrogate pair; read the next required character
                                c[1] = internal::read_utf16<Flags>(data, static_cast<error_handler&>(*this));
                                if (c[1] < 0xdc00 || c[1] > 0xdfff)
                                {
                                    FASTJSON_PARSE_ERROR_THIS("Invalid UTF-16 surrogate pair", data - 6);
                                }
                            }
                            utf16_char* p = c;
                            internal::convert<0>(p, p + 2, out, out_end, static_cast<error_handler&>(*this));
                        }
                        break;
                    }
                    break;

                case Ch('\0'):
                    FASTJSON_PARSE_ERROR_THIS("Expected end-of-string '\"'", data);
                    break;
                }
            }

            FASTJSON_PARSE_ERROR_THIS("Expected end-of-string '\"'", data);
        }

        // Internal helper function for parsing a json number. Advances \c text past the number.
        template<int Flags, class ChIn> void parse_number(ChIn*& data, const ChIn* end, json_value<Ch>* val)
        {
            val->type_ = value_number;

            // Easiest case: if Ch and ChIn are the same size and no swap is required,
            // we can just point to the number in the data and move on.
            if (sizeof(Ch) == sizeof(ChIn) && (Flags & (parse_force_string_terminators|parse_no_string_terminators|internal::parse_do_swap)) == parse_no_string_terminators)
            {
                val->value_ = (Ch*)data;
                measure_number<Flags>(data, end);
                val->valueend_ = (Ch*)data;
                return;
            }

            // We need to copy the string in some cases
            bool require_alloc = false;
            if (sizeof(Ch) > sizeof(ChIn) || (Flags & (parse_force_string_terminators)) != 0)
            {
                require_alloc = true;
            }

            // Measure the string and copy
            ChIn* num_end = data;
            const std::size_t chars = measure_number<Flags>(num_end, end);
            Ch* out = (Ch*)data;
            if (require_alloc)
            {
                out = (Ch*)alloc(sizeof(Ch) * (chars + 1));
                *(out + chars) = Ch('\0');
            }

            val->value_ = out;

            const Ch* out_end = out + chars;
            while (data < num_end)
            {
                internal::convert<Flags>(data, num_end, out, out_end, static_cast<error_handler&>(*this));
            }

            val->valueend_ = (Ch*)out_end;
        }

        // Internal helper function for parsing a json_value.
        template<int Flags, class ChIn> json_value<Ch>* parse_value(ChIn*& data, const ChIn* end)
        {
            if (data == end)
            {
                FASTJSON_PARSE_ERROR_THIS("Expected value", data);
            }

            json_value<Ch>* val = 0;
            switch (internal::read<Flags>(*data))
            {
            case ChIn('-'):
            case ChIn('.'): // Not necessary, but will provide a better 'Expected digit' parse error rather than 'Expected value'
            case ChIn('0'): case ChIn('1'): case ChIn('2'): case ChIn('3'): case ChIn('4'):
            case ChIn('5'): case ChIn('6'): case ChIn('7'): case ChIn('8'): case ChIn('9'):
                parse_number<Flags>(data, end, val = allocate_value());
                return val;

            case ChIn('f'): // False
                if ((end - data) >= 5 && internal::read<Flags>(data[1]) == ChIn('a') && internal::read<Flags>(data[2]) == ChIn('l') && internal::read<Flags>(data[3]) == ChIn('s') && internal::read<Flags>(data[4]) == ChIn('e'))
                {
                    val = allocate_value(value_bool);
                    val->value_ = const_cast<Ch*>(&internal::string_tables<Ch>::falsestr[0]);
                    val->valueend_ = const_cast<Ch*>(&internal::string_tables<Ch>::falsestr[5]);
                    data += 5;
                    return val;
                }
                break;

            case ChIn('t'): // True
                if ((end - data) >= 4 && internal::read<Flags>(data[1]) == ChIn('r') && internal::read<Flags>(data[2]) == ChIn('u') && internal::read<Flags>(data[3]) == ChIn('e'))
                {
                    val = allocate_value(value_bool);
                    val->value_ = const_cast<Ch*>(&internal::string_tables<Ch>::truestr[0]);
                    val->valueend_ = const_cast<Ch*>(&internal::string_tables<Ch>::truestr[4]);
                    data += 4;
                    return val;
                }
                break;

            case ChIn('n'): // Null
                if ((end - data) >= 4 && internal::read<Flags>(data[1]) == ChIn('u') && internal::read<Flags>(data[2]) == ChIn('l') && internal::read<Flags>(data[3]) == ChIn('l'))
                {
                    val = allocate_value(value_null);
                    val->value_ = const_cast<Ch*>(&internal::string_tables<Ch>::nullstr[0]);
                    val->valueend_ = const_cast<Ch*>(&internal::string_tables<Ch>::nullstr[4]);
                    data += 4;
                    return val;
                }
                break;

            case ChIn('{'): // Object begin
                ++data;
                return parse_object<Flags>(allocate_object(), data, end);

            case ChIn('['): // Array begin
                ++data;
                return parse_array<Flags>(allocate_object(), data, end);

            case ChIn('"'): // String begin
                ++data;
                val = allocate_value(value_string);
                parse_string<Flags>(data, end, val->value_, val->valueend_);
                return val;
            }

            FASTJSON_PARSE_ERROR_THIS("Expected value", data);
        }

        json_object<Ch>* root_;
    };

    ///////////////////////////////////////////////////////////////////////////
    // Implementation

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

        template<class Ch>
        const Ch string_tables<Ch>::nullstr[5] =
        {
            Ch('n'), Ch('u'), Ch('l'), Ch('l'), Ch('\0')
        };

        template<class Ch>
        const Ch string_tables<Ch>::truestr[5] =
        {
            Ch('t'), Ch('r'), Ch('u'), Ch('e'), Ch('\0')
        };

        template<class Ch>
        const Ch string_tables<Ch>::falsestr[6] =
        {
            Ch('f'), Ch('a'), Ch('l'), Ch('s'), Ch('e'), Ch('\0')
        };

        template<int Dummy>
        const std::size_t lookup_tables<Dummy>::utf8_lengths[64] =
        {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // invalid
            2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 0, 0
        };

        template<int Dummy>
        const std::ptrdiff_t lookup_tables<Dummy>::encoding_sizes[5] =
        {
            1, 2, 2, 4, 4
        };
    }
}

#ifdef _MSC_VER
#pragma warning (pop)
#endif

#endif