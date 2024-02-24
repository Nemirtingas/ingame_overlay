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

#include <InGameOverlay/RendererHook.h>

#include "../InternalIncludes.h"

#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <objc/runtime.h>

namespace InGameOverlay {

class NSViewHook_t :
    public BaseHook_t
{
public:
    static constexpr const char* DLL_NAME = "AppKit";

private:
    static NSViewHook_t* _Instance;

    // Variables
    bool _Hooked;
    bool _Initialized;
    id _EventMonitor;
    NSWindow* _Window;
    NSPoint _SavedLocation;

    NSInteger(*pressedMouseButtons)(id self, SEL sel);
    NSPoint (*mouseLocation)(id self, SEL sel);

    // Hooked functions
    static NSInteger MypressedMouseButtons(id self, SEL sel);
    static NSPoint MymouseLocation(id self, SEL sel);

    // Functions
    NSViewHook_t();

public:
    std::function<void()> KeyCombinationCallback;
    std::set<int> NativeKeyCombination;
    bool KeyCombinationPushed;
    bool ApplicationInputsHidden;
    bool OverlayInputsHidden;

    std::string LibraryName;

    virtual ~NSViewHook_t();

    void ResetRenderState();
    bool PrepareForOverlay();

    bool StartHook(std::function<void()>& key_combination_callback, std::set<InGameOverlay::ToggleKey> const& toggle_keys);
    void HideAppInputs(bool hide);
    void HideOverlayInputs(bool hide);
    static NSViewHook_t* Inst();
    virtual std::string GetLibraryName() const;

};

}// namespace InGameOverlay