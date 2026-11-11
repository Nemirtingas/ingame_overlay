/*
 * Copyright (C) Nemirtingas
 * This file is part of the ingame overlay project
 *
 * The ingame overlay project is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The ingame overlay project is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "McpJson.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace InGameOverlay {

namespace {

constexpr int MaxDepth = 32;

class Parser_t
{
    char const* _Cur;
    char const* _End;

    void _SkipSpaces()
    {
        while (_Cur != _End && (*_Cur == ' ' || *_Cur == '\t' || *_Cur == '\r' || *_Cur == '\n'))
            ++_Cur;
    }

    bool _Literal(char const* literal)
    {
        const size_t len = strlen(literal);
        if (static_cast<size_t>(_End - _Cur) < len || memcmp(_Cur, literal, len) != 0)
            return false;

        _Cur += len;
        return true;
    }

    bool _Hex4(uint32_t& out)
    {
        if (_End - _Cur < 4)
            return false;

        out = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = *_Cur++;
            out <<= 4;
            if (c >= '0' && c <= '9')      out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    static void _AppendUtf8(std::string& out, uint32_t cp)
    {
        if (cp < 0x80)
        {
            out += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool _String(std::string& out)
    {
        if (_Cur == _End || *_Cur != '"')
            return false;

        ++_Cur;
        while (_Cur != _End)
        {
            const char c = *_Cur++;
            if (c == '"')
                return true;

            if (static_cast<unsigned char>(c) < 0x20)
                return false;

            if (c != '\\')
            {
                out += c;
                continue;
            }

            if (_Cur == _End)
                return false;

            switch (*_Cur++)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    uint32_t cp;
                    if (!_Hex4(cp))
                        return false;

                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        uint32_t low;
                        if (!_Literal("\\u") || !_Hex4(low) || low < 0xDC00 || low > 0xDFFF)
                            return false;

                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    {
                        return false;
                    }
                    _AppendUtf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool _Number(McpJson_t& out)
    {
        char const* cur = _Cur;
        auto digits = [&]()
        {
            char const* start = cur;
            while (cur != _End && *cur >= '0' && *cur <= '9')
                ++cur;
            return cur != start;
        };

        if (cur != _End && *cur == '-')
            ++cur;

        char const* integer = cur;
        if (!digits() || (*integer == '0' && cur - integer > 1))
            return false;

        if (cur != _End && *cur == '.')
        {
            ++cur;
            if (!digits())
                return false;
        }

        if (cur != _End && (*cur == 'e' || *cur == 'E'))
        {
            ++cur;
            if (cur != _End && (*cur == '+' || *cur == '-'))
                ++cur;
            if (!digits())
                return false;
        }

        // strtod depends on the locale of the host application, so only the digits are read through it when there is a fraction.
        const std::string text(_Cur, cur);
        char const* dot = strchr(text.c_str(), '.');
        double value = 0.0;
        if (dot == nullptr)
        {
            value = strtod(text.c_str(), nullptr);
        }
        else
        {
            char const* exponent = strpbrk(text.c_str(), "eE");
            const std::string fraction = exponent != nullptr ? std::string(dot + 1, exponent) : std::string(dot + 1);
            const std::string whole = std::string(text.c_str(), dot) + fraction + (exponent != nullptr ? std::string(exponent) : std::string());
            value = strtod(whole.c_str(), nullptr) / std::pow(10.0, static_cast<double>(fraction.size()));
        }

        out.Type = McpJson_t::Type_e::Number;
        out.Number = value;
        _Cur = cur;
        return true;
    }

    bool _Value(McpJson_t& out, int depth)
    {
        if (depth > MaxDepth)
            return false;

        _SkipSpaces();
        if (_Cur == _End)
            return false;

        char const* start = _Cur;
        bool res = false;
        switch (*_Cur)
        {
            case 'n': res = _Literal("null"); out.Type = McpJson_t::Type_e::Null; break;
            case 't': res = _Literal("true"); out.Type = McpJson_t::Type_e::Bool; out.Bool = true; break;
            case 'f': res = _Literal("false"); out.Type = McpJson_t::Type_e::Bool; out.Bool = false; break;
            case '"': out.Type = McpJson_t::Type_e::String; res = _String(out.String); break;
            case '[':
            {
                ++_Cur;
                out.Type = McpJson_t::Type_e::Array;
                _SkipSpaces();
                if (_Cur != _End && *_Cur == ']')
                {
                    ++_Cur;
                    res = true;
                    break;
                }

                while (true)
                {
                    out.Array.emplace_back();
                    if (!_Value(out.Array.back(), depth + 1))
                        return false;

                    _SkipSpaces();
                    if (_Cur == _End)
                        return false;

                    const char c = *_Cur++;
                    if (c == ']')
                        break;
                    if (c != ',')
                        return false;
                }
                res = true;
                break;
            }
            case '{':
            {
                ++_Cur;
                out.Type = McpJson_t::Type_e::Object;
                _SkipSpaces();
                if (_Cur != _End && *_Cur == '}')
                {
                    ++_Cur;
                    res = true;
                    break;
                }

                while (true)
                {
                    std::string key;
                    _SkipSpaces();
                    if (!_String(key))
                        return false;

                    _SkipSpaces();
                    if (_Cur == _End || *_Cur++ != ':')
                        return false;

                    out.Object.emplace_back(std::move(key), McpJson_t{});
                    if (!_Value(out.Object.back().second, depth + 1))
                        return false;

                    _SkipSpaces();
                    if (_Cur == _End)
                        return false;

                    const char c = *_Cur++;
                    if (c == '}')
                        break;
                    if (c != ',')
                        return false;
                }
                res = true;
                break;
            }
            default: res = _Number(out); break;
        }

        if (res)
            out.Raw.assign(start, _Cur);

        return res;
    }

public:
    Parser_t(std::string const& text):
        _Cur(text.data()),
        _End(text.data() + text.size())
    {
    }

    bool Document(McpJson_t& out)
    {
        if (!_Value(out, 0))
            return false;

        _SkipSpaces();
        return _Cur == _End;
    }
};

}

bool McpJson_t::Parse(std::string const& text, McpJson_t& out)
{
    out = McpJson_t{};
    return Parser_t(text).Document(out);
}

McpJson_t const* McpJson_t::Find(char const* key) const
{
    if (Type != Type_e::Object)
        return nullptr;

    for (auto const& entry : Object)
    {
        if (entry.first == key)
            return &entry.second;
    }
    return nullptr;
}

std::string McpJsonQuote(std::string const& text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (const char c : text)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buffer[8];
                    snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                }
                else
                {
                    out += c;
                }
        }
    }
    out += '"';
    return out;
}

// The host application may have changed the locale, printf could write a decimal comma.
std::string McpJsonFloat(float value)
{
    if (!std::isfinite(value))
        return "0";

    const long long thousandths = std::llround(static_cast<double>(std::fabs(value)) * 1000.0);
    std::string fraction = std::to_string(thousandths % 1000);
    fraction.insert(0, 3 - fraction.size(), '0');
    return (value < 0.0f ? "-" : "") + std::to_string(thousandths / 1000) + "." + fraction;
}

std::string McpJsonDouble(double value)
{
    if (!std::isfinite(value))
        return "null";

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.9g", value);
    // Only the decimal separator depends on the locale with %g.
    for (char* c = buffer; *c != 0; ++c)
    {
        if (*c == ',')
            *c = '.';
    }
    return buffer;
}

}
