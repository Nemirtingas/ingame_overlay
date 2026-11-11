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

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace InGameOverlay {

// Just enough JSON for JSON-RPC requests. Numbers are kept as doubles, an id is echoed back through its raw text.
class McpJson_t
{
public:
    enum class Type_e { Null, Bool, Number, String, Array, Object };

    Type_e Type = Type_e::Null;
    bool Bool = false;
    double Number = 0.0;
    std::string String;
    std::vector<McpJson_t> Array;
    std::vector<std::pair<std::string, McpJson_t>> Object;
    std::string Raw;

    // Parses a whole document, nesting is limited because the input isn't trusted.
    static bool Parse(std::string const& text, McpJson_t& out);

    // Returns nullptr if this isn't an object or if the key is missing.
    McpJson_t const* Find(char const* key) const;

    bool IsString() const { return Type == Type_e::String; }
    bool IsNumber() const { return Type == Type_e::Number; }
    bool IsBool() const { return Type == Type_e::Bool; }
    bool IsObject() const { return Type == Type_e::Object; }
};

// Returns the quoted and escaped JSON string.
std::string McpJsonQuote(std::string const& text);

// Returns a number with 3 decimals, whatever the locale of the host application. Not finite values become 0.
std::string McpJsonFloat(float value);

// Returns a number with 9 significant digits, whatever the locale of the host application. Not finite values become null.
std::string McpJsonDouble(double value);

}
