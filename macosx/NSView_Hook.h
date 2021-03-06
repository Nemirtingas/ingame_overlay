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

#include "../Base_Hook.h"

#include "cppNSEvent.h"
#include "cppNSView.h"

typedef struct _WrapperNSEvent WrapperNSEvent;

class NSView_Hook : public Base_Hook
{
public:
    static constexpr const char* DLL_NAME = "AppKit";

private:
    static NSView_Hook* _inst;

    // Variables
    bool hooked;
    bool initialized;
    class ObjCHookWrapper* nsview_hook;

    std::string library_name;

    // Functions
    NSView_Hook();

    std::function<bool(bool)> key_combination_callback;

    static bool handleNSEvent(cppNSEvent* cppevent, cppNSView* cppview);
    
public:
    virtual ~NSView_Hook();

    void resetRenderState();
    bool prepareForOverlay();

    bool start_hook(std::function<bool(bool)>& key_combination_callback);
    static NSView_Hook* Inst();
    virtual const char* get_lib_name() const;

};
