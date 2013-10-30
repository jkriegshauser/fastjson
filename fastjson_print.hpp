#ifndef FASTJSON_PRINT_HPP_INCLUDED
#define FASTJSON_PRINT_HPP_INCLUDED

// Copyright (C) 2013 Joshua M. Kriegshauser
// Version 1.0
// Revision $DateTime: 2013/09/21 18:24:00 $
//! \file fastjson_print.hpp This file contains the fastjson print utility

#include "fastjson.hpp"

// Only include streams if not disabled
#ifndef FASTJSON_NO_STREAMS
    #include <ostream>
    #include <iterator>
#endif

namespace fastjson
{
    ///////////////////////////////////////////////////////////////////////////
    // Printing flags
    
    //! Prints with as little whitespace as possible
    const int no_whitespace = 0x10;

    //! Prefer spaces to tabs
    const int use_spaces = 0x20;

    const int indent_1_space  = 0x1; //!< Intent 1 space if spaces are preferred to tabs. Can be combined (OR'd) with other intent flag values.
    const int indent_2_spaces = 0x2; //!< Intent 2 space if spaces are preferred to tabs. Can be combined (OR'd) with other intent flag values.
    const int indent_4_spaces = 0x4; //!< Intent 4 space if spaces are preferred to tabs. Can be combined (OR'd) with other intent flag values. This is the default.
    const int indent_8_spaces = 0x8; //!< Intent 8 space if spaces are preferred to tabs. Can be combined (OR'd) with other intent flag values.

    // Internal flags
    const int skip_name = 0x10000; //!< Internal flag.

    ///////////////////////////////////////////////////////////////////////////
    // Internal

    //! \cond internal
    namespace internal
    {
        template<class OutputIterator, class Ch>
        inline OutputIterator copy_chars(OutputIterator out, const Ch* begin, const Ch* end)
        {
            while (begin != end)
            {
                *out++ = *begin++;
            }
            return out;
        }

        template<class Ch, class OutputIterator>
        inline OutputIterator emit_indent(OutputIterator out, int flags, int indent)
        {
            if ((flags & no_whitespace) == 0)
            {
                Ch c;
                if ((flags & use_spaces) == use_spaces)
                {
                    int spaces = (flags & 0xf);
                    if (spaces == 0) spaces = indent_4_spaces;
                    indent *= spaces;
                    c = Ch(' ');
                }
                else
                {
                    c = Ch('\t');
                }
                while (indent-- > 0)
                {
                    *out++ = c;
                }
            }
            return out;
        }

        template<class Ch, class OutputIterator>
        inline OutputIterator emit_utf16(OutputIterator out, wchar_t c) throw()
        {
            *out++ = Ch('\\');
            *out++ = Ch('u');
            *out++ = internal::lookup_tables<0>::lookup_hexchar[(c >> 12) & 0xf];
            *out++ = internal::lookup_tables<0>::lookup_hexchar[(c >>  8) & 0xf];
            *out++ = internal::lookup_tables<0>::lookup_hexchar[(c >>  4) & 0xf];
            *out++ = internal::lookup_tables<0>::lookup_hexchar[(c      ) & 0xf];
            return out;
        }

        // utf?-to-utf16 converter
        // Default implementation; generates compile error
        template<class Ch, size_t S = sizeof(Ch)>
        struct to_utf16 { };

        template<class Ch>
        struct to_utf16<Ch, 1> // utf-8 to utf-16
        {
            const static size_t lengths[64];
            template<class OutputIterator>
            OutputIterator operator () (OutputIterator out, const Ch*& begin, const Ch* end) const throw()
            {
                static_cast<void>(end);
                wchar_t c;
                const byte* p = (const byte*)begin;
                switch (lengths[*p >> 2])
                {
                default:
                    // Invalid utf-8
                    return out;

                case 1:
                    assert((end - begin) >= 1);
                    out = emit_utf16<Ch>(out, wchar_t(*p));
                    ++begin;
                    break;

                case 2:
                    assert((end - begin) >= 2);
                    c = (wchar_t)((p[0] & 0x1f) << 6);
                    c |= (p[1] & 0x3f);
                    out = emit_utf16<Ch>(out, c);
                    begin += 2;
                    break;

                case 3:
                    assert((end - begin) >= 3);
                    c = (wchar_t)((p[0] & 0xf) << 12);
                    c |= ((p[1] & 0x3f) << 6);
                    c |= ((p[2] & 0x3f));
                    out = emit_utf16<Ch>(out, c);
                    begin += 3;
                    break;

                case 4:
                    {
                        // Convert to utf-32
                        assert((end - begin) >= 4);
                        std::size_t s = (std::size_t)((p[0] & 0x7) << 18);
                        s |= ((p[1] & 0x3f) << 12);
                        s |= ((p[2] & 0x3f) << 6);
                        s |= ((p[3] & 0x3f));

                        // Emit surrogate pair
                        s -= 0x10000;
                        out = emit_utf16<Ch>(out, wchar_t(0xd800 | (s >> 10)));
                        out = emit_utf16<Ch>(out, wchar_t(0xdc00 | (s & 0x3ff)));
                    }
                    begin += 4;
                    break;
                }
                return out;
            }
        };

        template<class Ch>
        const size_t to_utf16<Ch, 1>::lengths[] = {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // invalid
            2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 0, 0,
        };

        template<class Ch>
        struct to_utf16<Ch, 2> // utf-16 to utf-16
        {
            template<class OutputIterator>
            OutputIterator operator () (OutputIterator out, const Ch*& begin, const Ch* end) const throw()
            {
                static_cast<void>(end);
                out = emit_utf16<Ch>(out, wchar_t(*begin));
                if (*begin >= Ch(0xd800) && *begin <= Ch(0xdfff))
                {
                    // Surrogate pair
                    assert((end - begin) >= 2);
                    ++begin;
                    assert(*begin >= Ch(0xdc00) && *begin <= Ch(0xdfff));
                    out = emit_utf16<Ch>(out, wchar_t(*begin));
                    ++begin;
                }
                return out;
            }
        };

        template<class Ch>
        struct to_utf16<Ch, 4> // utf-32 to utf-16
        {
            template<class OutputIterator>
            OutputIterator operator () (OutputIterator out, const Ch*& begin, const Ch* end) const throw()
            {
                static_cast<void>(end);
                assert(begin < end);
                //assert((*begin < Ch(0xd800) || (*begin > Ch(0xdfff) && *begin <= Ch(0x10ffff)) && *begin != Ch(0xffff));
                if (*begin < Ch(0x10000))
                {
                    out = emit_utf16<Ch>(out, wchar_t(*begin));
                }
                else
                {
                    // Surrogate pair
                    Ch c = *begin;
                    c -= 0x10000;
                    out = emit_utf16<Ch>(out, wchar_t(0xd800 | (c >> 10)));
                    out = emit_utf16<Ch>(out, wchar_t(0xdc00 | (c & 0x3ff)));
                }
                ++begin;
                return out;
            }
        };

        template<class OutputIterator, class Ch>
        inline OutputIterator emit_string(OutputIterator out, const Ch* begin, const Ch* end)
        {
            *out++ = Ch('"');
            while (begin < end)
            {
                switch (*begin)
                {
                case Ch('\\'):
                case Ch('"'):
                    *out++ = Ch('\\'); *out++ = *begin++;
                    break;

                case Ch('\x08'): // backspace
                    *out++ = Ch('\\'); *out++ = Ch('b');
                    ++begin;
                    break;

                case Ch('\x0c'): // form feed
                    *out++ = Ch('\\'); *out++ = Ch('f');
                    ++begin;
                    break;
                    
                case Ch('\r'): // carriage return
                    *out++ = Ch('\\'); *out++ = Ch('r');
                    ++begin;
                    break;

                case Ch('\n'): // newline/line feed
                    *out++ = Ch('\\'); *out++ = Ch('n');
                    ++begin;
                    break;

                case Ch('\t'): // tab
                    *out++ = Ch('\\'); *out++ = Ch('t');
                    ++begin;
                    break;

                // Values that must be escaped
                case Ch('\x00'): case Ch('\x01'): case Ch('\x02'): case Ch('\x03'): case Ch('\x04'): case Ch('\x05'): case Ch('\x06'): case Ch('\x07'):
                                                                   case Ch('\x0B'):                                   case Ch('\x0E'): case Ch('\x0F'):
                case Ch('\x10'): case Ch('\x11'): case Ch('\x12'): case Ch('\x13'): case Ch('\x14'): case Ch('\x15'): case Ch('\x16'): case Ch('\x17'):
                case Ch('\x18'): case Ch('\x19'): case Ch('\x1A'): case Ch('\x1B'): case Ch('\x1C'): case Ch('\x1D'): case Ch('\x1E'): case Ch('\x1F'):
                    out = emit_utf16<Ch>(out, wchar_t(*begin++));
                    break;

                default:
                    if (*begin < 0 || *begin > Ch('\x7f'))
                    {
                        out = internal::to_utf16<Ch>()(out, begin, end); // Advances begin
                    }
                    else
                    {
                        *out++ = *begin++;
                    }
                }
            }
            *out++ = Ch('"');
            return out;
        }

        template<class OutputIterator, class Ch>
        inline OutputIterator print_value(OutputIterator out, const json_value<Ch>& val, int flags, int indent)
        {
            emit_indent<Ch>(out, flags, indent);

            // Print the name if it exists and we're not supposed to skip it.
            if (val.name() != val.nameend() && (flags & skip_name) == 0)
            {
                out = emit_string(out, val.name(), val.nameend());
                *out++ = Ch(':');

                if ((flags & no_whitespace) == 0)
                {
                    *out++ = Ch(' ');
                }
            }

            // Nothing deeper should skip the name
            flags &= ~skip_name;

            switch (val.type())
            {
            case value_null:
            case value_bool:
            case value_number:
                // Value should already be formatted
                out = copy_chars(out, val.as_string(), val.as_string_end());
                break;

            case value_string:
                out = emit_string(out, val.as_string(), val.as_string_end());
                break;

            case value_array:
            case value_object:
                {
                    Ch start, end;
                    bool array;
                    const json_object<Ch>* oval = val.as_object();
                    if (val.type() == value_object) start = Ch('{'), end = Ch('}'), array = false; else start = Ch('['), end = Ch(']'), array = true;
                    *out++ = start;
                    bool first = true;
                    for (const json_value<Ch>* p = oval->first_child(); p; p = p->next_sibling())
                    {
                        if (!first)
                        {
                            *out++ = Ch(',');
                            if (array && (flags & no_whitespace) == 0)
                            {
                                *out++ = Ch(' ');
                            }
                        }

                        if (!array && (flags & no_whitespace) == 0)
                        {
                            *out++ = Ch('\n');
                        }

                        print_value(out, *p, flags, array ? 0 : indent + 1);
                        first = false;
                    }

                    if (!first && !array && (flags & no_whitespace) == 0)
                    {
                        *out++ = Ch('\n');
                        out = emit_indent<Ch>(out, flags, indent);
                    }
                    *out++ = end;
                }
                break;
            }
            return out;
        }
    }
    //! \endcond

    ///////////////////////////////////////////////////////////////////////////
    // Printing

    //! \brief Prints json to given output iterator
    //! \param out Output iterator to print to.
    //! \param doc Document to be printed
    //! \param flags Flags controlling how json is printed
    //! \return Output iterator pointing to position immediately after last character of printed text
    template<class OutputIterator, class Ch, class T_POOL>
    inline OutputIterator print(OutputIterator out, const json_document<Ch, T_POOL>& doc, int flags = 0)
    {
        return internal::print_value(out, doc.root(), flags, 0);
    }

    //! \brief Prints the object or array to the given output iterator
    //! \param out Output iterator to print to.
    //! \param value The object or array to be printed
    //! \param flags Flags controlling how json is printed
    //! \return Output iterator pointing to position immediately after last character of printed text
    template<class OutputIterator, class Ch>
    inline OutputIterator print(OutputIterator out, const json_object<Ch>& value, int flags = 0)
    {
        return internal::print_value(out, value, flags|skip_name, 0);
    }

#ifndef FASTJSON_NO_STREAMS
    //! \brief Prints json to given output stream.
    //! \param out Output stream to print to.
    //! \param doc Document to be printed
    //! \param flags Flags controlling how json is printed
    //! \return Output stream.
    template<class Ch, class T_POOL>
    inline std::basic_ostream<Ch>& print(std::basic_ostream<Ch>& out, const json_document<Ch, T_POOL>& doc, int flags = 0)
    {
        std::ostream_iterator<Ch, Ch> iter(out);
        print(iter, doc, flags);
        return out;
    }

    //! \brief Prints formatted json to given output stream. Uses default printing flags. Use print() function to customize printing process.
    //! \param out Output stream to print to.
    //! \param doc Document to be printed.
    //! \return Output stream.
    template<class Ch, class T_POOL>
    inline std::basic_ostream<Ch>& operator << (std::basic_ostream<Ch>& out, const json_document<Ch, T_POOL>& doc)
    {
        return print(out, node);
    }

    //! \brief Prints the json object or array to the given output stream.
    //! \param out Output stream to print to.
    //! \param value The object or array to be printed.
    //! \param flags Flags controlling how json is printed.
    //! \return Output stream.
    template<class Ch>
    inline std::basic_ostream<Ch>& print(std::basic_ostream<Ch>& out, const json_object<Ch>& value, int flags = 0)
    {
        std::ostream_iterator<Ch, Ch> iter(out);
        print(iter, value, flags|skip_name);
        return out;
    }

    //! \brief Prints the formatted json object or array to the given output stream. Uses default printing flags. Use print() function to customize printing process.
    //! \param out Output stream to print to.
    //! \param value The object or array to be printed.
    //! \return Output stream.
    template<class Ch>
    inline std::basic_ostream<Ch>& operator << (std::basic_ostream<Ch>& out, const json_object<Ch>& value)
    {
        return print(out, value);
    }
#endif
}

#endif