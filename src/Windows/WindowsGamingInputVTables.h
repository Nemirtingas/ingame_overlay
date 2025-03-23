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

enum class IRawGameControllerVTable
{
    // IUnknown
    QueryInterface,
    AddRef,
    Release,

    // IInspectable
    GetIids,
    GetRuntimeClassName,
    GetTrustLevel,

    // IRawGameController
    get_AxisCount,
    get_ButtonCount,
    get_ForceFeedbackMotors,
    get_HardwareProductId,
    get_SwitchCount,
    GetButtonLabel,
    GetCurrentReading,
    GetSwitchKind,
};

enum class IGamepadVTable
{
    // IUnknown
    QueryInterface,
    AddRef,
    Release,

    // IInspectable
    GetIids,
    GetRuntimeClassName,
    GetTrustLevel,

    // IGamepad
    get_Vibration,
    put_Vibration,
    GetCurrentReading,
};