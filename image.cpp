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

#include "image.h"

#include <fstream>
#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BITMAP
#define STBI_NO_STDIO
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include <stb_image_write.h>

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>


std::shared_ptr<Image> Image::create_image(void const* data, int32_t width, int32_t height)
{
    std::shared_ptr<Image> res = std::make_shared<Image>();

    res->reserve_img_buffer(width, height);
    if (data != nullptr)
        memcpy(res->get_raw_pointer(), data, res->raw_size());

    return res;
}

std::shared_ptr<Image> Image::load_image(std::string const& image_path, int32_t resize_width, int32_t resize_height)
{
    std::shared_ptr<Image> res;

    std::ifstream f(image_path, std::ios::in | std::ios::binary);
    if (f.is_open())
    {
        f.seekg(0, std::ios::end);
        size_t file_size = f.tellg();
        f.seekg(0, std::ios::beg);

        char* buff = new char[file_size];
        f.read(buff, file_size);

        res = load_image(buff, file_size, resize_width, resize_height);
        delete[]buff;
    }

    return res;
}

std::shared_ptr<Image> Image::load_image(void const* data, size_t data_len, int32_t resize_width, int32_t resize_height)
{
    std::shared_ptr<Image> res;
    int32_t width, height;

    stbi_uc* buffer = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(data), data_len, &width, &height, nullptr, 4);
    if (buffer == nullptr)
        return res;

    if ((resize_width > 0 && width != resize_width) || (resize_height > 0 && height != resize_height))
    {
        res = copy_image(buffer, width, height, resize_width, resize_height);
    }
    else
    {
        res = std::make_shared<Image>();
        res->reserve_img_buffer(width, height);
        memcpy(res->get_raw_pointer(), buffer, res->raw_size());
    }

    stbi_image_free(buffer);

    return res;
}

std::shared_ptr<Image> Image::copy_image(void const* data, int32_t width, int32_t height, int32_t resize_width, int32_t resize_height)
{
    std::shared_ptr<Image> res = std::make_shared<Image>();

    if ((width == resize_width || resize_width <= 0) && (height == resize_height || resize_height <= 0))
    {// Same size, don't bother resizing
        res->reserve_img_buffer(width, height);
        memcpy(res->get_raw_pointer(), data, res->raw_size());
    }
    else
    {
        res->reserve_img_buffer(resize_width, resize_height);

        if (stbir_resize(data, width, height, 0,
            res->get_raw_pointer(), resize_width, resize_height, 0,
            STBIR_TYPE_UINT8, 4, 3, 0,
            STBIR_EDGE_ZERO, STBIR_EDGE_ZERO,
            STBIR_FILTER_DEFAULT, STBIR_FILTER_DEFAULT,
            STBIR_COLORSPACE_SRGB, nullptr) != 1)
        {
            res.reset();
        }
    }

    return res;
}

bool Image::save_image_to_file(std::string const& image_path, void* data, int32_t width, int32_t height, int32_t channels, image_type img_type)
{
    switch (img_type)
    {
        case image_type::png: return stbi_write_png(image_path.c_str(), width, height, channels, data, 0) == 1;
        case image_type::jpeg: return stbi_write_jpg(image_path.c_str(), width, height, channels, data, 0) == 1;
        case image_type::bmp: return stbi_write_bmp(image_path.c_str(), width, height, channels, data) == 1;
        default: return false;
    }
}
