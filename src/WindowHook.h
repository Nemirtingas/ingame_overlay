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
 * You should have received a copy of the GNU Lesser General Public License
 * along with the ingame overlay project; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "OSDetector.h"

#if defined(INGAMEOVERLAY_OS_WINDOWS)
#include "Windows/WindowsHook.h"
#elif defined(INGAMEOVERLAY_OS_LINUX)
#include "Linux/X11Hook.h"
#elif defined(INGAMEOVERLAY_OS_APPLE)
#include "MacOSX/NSViewHook.h"
#endif

namespace InGameOverlay {

#if defined(INGAMEOVERLAY_OS_WINDOWS)
using WindowHook_t = WindowsHook_t;
#elif defined(INGAMEOVERLAY_OS_LINUX)
using WindowHook_t = X11Hook_t;
#elif defined(INGAMEOVERLAY_OS_APPLE)
using WindowHook_t = NSViewHook_t;
#endif

} // namespace InGameOverlay
