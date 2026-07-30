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

#include "RendererHook.h"

namespace InGameOverlay {

/// <summary>
/// Starts a detector to automatically find the renderer used by the application.
/// </summary>
/// <param name="restart">If the renderer has not been found but StopRendererDetection has been called, it will scan the renderers again.</param>
/// <param name="rendererToDetect">Set this to any combined RendererHookType_t value to filter the renderers you want to detect.</param>
/// <param name="preferSystemLibraries">Prefer hooking the system libraries instead of the first one found.</param>
/// <returns>True: detection done, False: detection can be called again</returns>
bool DetectRenderer(bool restart = false, RendererHookType_t rendererToDetect = RendererHookType_t::Any, bool preferSystemLibraries = true);

/// <summary>
/// Stops the detector, DetectRenderer will always return False
/// </summary>
void StopRendererDetection();

/// <summary>
/// Gets the detected renderer.
/// </summary>
/// <returns>The renderer hook if detection found it.</returns>
RendererHook_t* GetDetectedRenderer();

/// <summary>
/// Gets a specific renderer, no detection will be made.
/// </summary>
/// <param name="rendererToDetect">The type of renderer you want, a multiple renderer value will always return nullptr.</param>
/// <returns>The renderer hook.</returns>
RendererHook_t* GetRenderer(RendererHookType_t rendererToDetect, bool preferSystemLibraries = true);

/// <summary>
/// Free the detector allocated by DetectRenderer allowing a new detection.
/// </summary>
void FreeDetector();

}