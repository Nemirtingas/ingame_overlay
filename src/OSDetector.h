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

#if defined(WIN64) || defined(_WIN64) || defined(__MINGW64__) || defined(WIN32) || defined(_WIN32) || defined(__MINGW32__)
#define INGAMEOVERLAY_OS_WINDOWS

#if defined(_M_IX86)
#define INGAMEOVERLAY_ARCH_X86
#elif defined(_M_AMD64)
#define INGAMEOVERLAY_ARCH_X64
#elif defined(_M_ARM)
#define INGAMEOVERLAY_ARCH_ARM
#elif defined(_M_ARM64)
#define INGAMEOVERLAY_ARCH_ARM64
#else
#error "Unhandled arch"
#endif
#elif defined(__linux__) || defined(linux)
#define INGAMEOVERLAY_OS_LINUX

#if defined(__i386__) || defined(__i386) || defined(i386)
#define INGAMEOVERLAY_ARCH_X86
#elif defined(__x86_64__) || defined(__x86_64) || defined(__amd64) || defined(__amd64__)
#define INGAMEOVERLAY_ARCH_X64
#elif defined(__arm__)
#define INGAMEOVERLAY_ARCH_ARM
#elif defined(__aarch64__)
#define INGAMEOVERLAY_ARCH_ARM64
#else
#error "Unhandled arch"
#endif
#elif defined(__APPLE__)
#define INGAMEOVERLAY_OS_APPLE

#if defined(__i386__) || defined(__i386) || defined(i386)
#define INGAMEOVERLAY_ARCH_X86
#elif defined(__x86_64__) || defined(__x86_64) || defined(__amd64) || defined(__amd64__)
#define INGAMEOVERLAY_ARCH_X64
#elif defined(__arm__)
#define INGAMEOVERLAY_ARCH_ARM
#elif defined(__aarch64__)
#define INGAMEOVERLAY_ARCH_ARM64
#else
#error "Unhandled arch"
#endif
#else
// #error "Unknown OS"
#endif