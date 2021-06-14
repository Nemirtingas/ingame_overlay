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

#include <cstdint>
#include <vector>
#include <string>
#include <memory>

union image_pixel_t
{
    uint32_t pixel;
    struct pixel_channels_t
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    } channels;
};

class ImageManager;
class Image
{
    int32_t _width;
    int32_t _height;
    std::vector<image_pixel_t> _img; // This should be able to be used in any Renderer. Its a contiguous array of RGBA values

public:
    enum class image_type
    {
        png,
        jpeg,
        bmp
    };

    // Size is the number of image_pixel_t, not the raw bytes count. Destructive, use with care
    inline void reserve_img_buffer(int32_t width, int32_t height) { _img.resize(size_t(width) * size_t(height)); _width = width; _height = height; }

    inline void*       get_raw_pointer()       { return _img.data(); }
    inline void const* get_raw_pointer() const { return _img.data(); }

    inline image_pixel_t*       pixels()       { return _img.data(); }
    inline image_pixel_t const* pixels() const { return _img.data(); }

    inline size_t raw_size() const { return _img.size() * sizeof(image_pixel_t); }
    inline size_t size() const { return _img.size(); }

    inline int32_t width() const { return _width; }
    inline void    width(int32_t width) { _width = width; }

    inline int32_t height() const { return _height; }
    inline void    height(int32_t height) { _height = height; }

    inline image_pixel_t& get_pixel(int32_t x, int32_t y)       { return _img[size_t(_width) * y + x]; }
    inline image_pixel_t  get_pixel(int32_t x, int32_t y) const { return _img[size_t(_width) * y + x]; }

    // Copy an existing image, optional: resize when copying. If it fails, the pointer will be nullptr
    inline std::shared_ptr<Image> copy_image(int32_t resize_width = 0, int32_t resize_height = 0)
    {
        return copy_image(get_raw_pointer(), width(), height(), resize_width, resize_height);
    }

    inline bool save_image_to_file(std::string const& image_path, image_type img_type = image_type::png)
    {
        return save_image_to_file(image_path, get_raw_pointer(), width(), height(), 4, img_type);
    }

    // Build an empty image
    static std::shared_ptr<Image> create_image(void const* data, int32_t width, int32_t height);
    // Load image from path, optinnal: resize when loading. If it fails, the pointer will be nullptr
    static std::shared_ptr<Image> load_image(std::string const& image_path, int32_t resize_width = 0, int32_t resize_height = 0);
    // Load image from buffer, optional: resize when loading. If it fails, the pointer will be nullptr
    static std::shared_ptr<Image> load_image(void const* data, size_t data_len, int32_t resize_width = 0, int32_t resize_height = 0);

    // Copy an existing image from a buffer, optional: resize when copying. If it fails, the pointer will be nullptr
    static std::shared_ptr<Image> copy_image(void const* data, int32_t width, int32_t height, int32_t resize_width = 0, int32_t resize_height = 0);

    static bool save_image_to_file(std::string const& image_path, void* data, int32_t width, int32_t height, int32_t channels, Image::image_type img_type = Image::image_type::png);
};
