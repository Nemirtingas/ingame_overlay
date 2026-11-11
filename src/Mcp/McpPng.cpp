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

#include "McpPng.h"

#include <algorithm>

namespace InGameOverlay {

namespace {

uint32_t Crc32(uint8_t const* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void AppendBigEndian(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 24));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

void AppendChunk(std::vector<uint8_t>& out, char const* type, std::vector<uint8_t> const& data)
{
    AppendBigEndian(out, static_cast<uint32_t>(data.size()));

    const size_t crcStart = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    AppendBigEndian(out, Crc32(out.data() + crcStart, out.size() - crcStart));
}

}

std::vector<uint8_t> McpEncodePng(uint32_t width, uint32_t height, std::vector<uint8_t> const& rgb)
{
    constexpr size_t MaxStoredBlockSize = 65535;

    const size_t rowSize = size_t(width) * 3;

    // Each row is prefixed by its filter: 0, no filter.
    std::vector<uint8_t> scanlines;
    scanlines.reserve((rowSize + 1) * height);
    for (uint32_t y = 0; y < height; ++y)
    {
        scanlines.push_back(0);
        scanlines.insert(scanlines.end(), rgb.begin() + y * rowSize, rgb.begin() + (y + 1) * rowSize);
    }

    uint32_t adlerA = 1;
    uint32_t adlerB = 0;
    for (const uint8_t byte : scanlines)
    {
        adlerA = (adlerA + byte) % 65521;
        adlerB = (adlerB + adlerA) % 65521;
    }

    std::vector<uint8_t> zlib;
    zlib.reserve(scanlines.size() + scanlines.size() / MaxStoredBlockSize * 5 + 16);
    zlib.push_back(0x78);
    zlib.push_back(0x01);
    for (size_t offset = 0; offset < scanlines.size() || offset == 0;)
    {
        const size_t size = std::min(scanlines.size() - offset, MaxStoredBlockSize);
        const bool isLast = offset + size == scanlines.size();
        zlib.push_back(isLast ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(size));
        zlib.push_back(static_cast<uint8_t>(size >> 8));
        zlib.push_back(static_cast<uint8_t>(~size));
        zlib.push_back(static_cast<uint8_t>(~size >> 8));
        zlib.insert(zlib.end(), scanlines.begin() + offset, scanlines.begin() + offset + size);
        offset += size;
        if (isLast)
            break;
    }
    AppendBigEndian(zlib, (adlerB << 16) | adlerA);

    std::vector<uint8_t> header;
    AppendBigEndian(header, width);
    AppendBigEndian(header, height);
    header.push_back(8); // Bit depth
    header.push_back(2); // Color type: RGB
    header.push_back(0); // Compression
    header.push_back(0); // Filter
    header.push_back(0); // No interlace

    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    AppendChunk(png, "IHDR", header);
    AppendChunk(png, "IDAT", zlib);
    AppendChunk(png, "IEND", {});
    return png;
}

std::string McpBase64(std::vector<uint8_t> const& data)
{
    static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3)
    {
        const size_t remaining = data.size() - i;
        const uint32_t block = (uint32_t(data[i]) << 16)
            | (remaining > 1 ? uint32_t(data[i + 1]) << 8 : 0)
            | (remaining > 2 ? uint32_t(data[i + 2]) : 0);

        out += Alphabet[(block >> 18) & 0x3F];
        out += Alphabet[(block >> 12) & 0x3F];
        out += remaining > 1 ? Alphabet[(block >> 6) & 0x3F] : '=';
        out += remaining > 2 ? Alphabet[block & 0x3F] : '=';
    }
    return out;
}

}
