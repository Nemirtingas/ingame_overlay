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

#include "InternalIncludes.h"

#ifdef INGAMEOVERLAY_USE_SPDLOG
static std::shared_ptr<spdlog::logger> _InGameOverlayLogger;

std::shared_ptr<spdlog::logger> GetLogger()
{
    return _InGameOverlayLogger;
}

void SetLogger(std::shared_ptr<spdlog::logger> logger)
{
    _InGameOverlayLogger = logger;
}

#endif