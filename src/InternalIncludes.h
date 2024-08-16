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

#include "BaseHook.h"

#if defined(_WIN32) || defined(WIN32) || defined(_WIN64) || defined(WIN64)

#include <windows.h>
#ifdef GetModuleHandle
#undef GetModuleHandle
#endif

#endif

#ifdef INGAMEOVERLAY_USE_SPDLOG
#define SPDLOG_ACTIVE_LEVEL 0
#include <spdlog/spdlog.h>

std::shared_ptr<spdlog::logger> GetLogger();
void SetLogger(std::shared_ptr<spdlog::logger> logger);

#define INGAMEOVERLAY_SPDLOG_LOGGER_NAME "RendererDetectorDebugLogger"
#define INGAMEOVERLAY_SPDLOG_LOG_FORMAT "[%H:%M:%S.%e](%t)[%l] - %!{%#} - %v"

#define INGAMEOVERLAY_TRACE(...) SPDLOG_LOGGER_TRACE(GetLogger(), __VA_ARGS__)
#define INGAMEOVERLAY_DEBUG(...) SPDLOG_LOGGER_DEBUG(GetLogger(), __VA_ARGS__)
#define INGAMEOVERLAY_INFO(...)  SPDLOG_LOGGER_INFO(GetLogger(), __VA_ARGS__)
#define INGAMEOVERLAY_WARN(...)  SPDLOG_LOGGER_WARN(GetLogger(), __VA_ARGS__)
#define INGAMEOVERLAY_ERROR(...) SPDLOG_LOGGER_ERROR(GetLogger(), __VA_ARGS__)

#else

#define INGAMEOVERLAY_TRACE(...)
#define INGAMEOVERLAY_DEBUG(...)
#define INGAMEOVERLAY_INFO(...)
#define INGAMEOVERLAY_WARN(...)
#define INGAMEOVERLAY_ERROR(...)

#endif