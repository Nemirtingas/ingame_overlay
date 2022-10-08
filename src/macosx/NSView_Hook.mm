/*
 * Copyright (C) 2019-2020 Nemirtingas
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

#include "NSView_Hook.h"

#include <imgui.h>
#include <backends/imgui_impl_osx.h>
#include <System/Library.h>

//#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include <objc/runtime.h>

NSInteger MypressedMouseButtons(id self, SEL sel);
NSInteger(*_pressedMouseButtons)(id self, SEL sel);

NSPoint savedLocation;
NSPoint MymouseLocation(id self, SEL sel);
NSPoint(*_mouseLocation)(id self, SEL sel);

bool GetKeyState( unsigned short inKeyCode )
{
    unsigned char keyMap[16];
    GetKeys((BigEndianUInt32*) &keyMap);
    return ((keyMap[inKeyCode >> 3] >> (inKeyCode & 7)) & 1) != 0;
}

@interface NSViewHook : NSObject
{
    id eventsMonitor;

    @public
        NSWindow* window;
}

- (void)StartHook:(NSWindow*)view;
-(void)StopHook;
@end

@implementation NSViewHook

- (void)StartHook:(NSWindow*)window;
{
    int mask = NSEventMaskAny;
    /*
   NSEventMaskKeyDown | NSEventMaskKeyUp |
   NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp |
   NSEventMaskRightMouseDown | NSEventMaskRightMouseDragged | NSEventMaskRightMouseUp |
   NSEventMaskOtherMouseDown | NSEventMaskOtherMouseDragged | NSEventMaskOtherMouseUp |
   NSEventMaskMouseMoved |
   NSEventMaskFlagsChanged |
   NSEventMaskScrollWheel;
    */

    self->window = window;

    Method ns_method = class_getClassMethod([NSEvent class], @selector(pressedMouseButtons));
    _pressedMouseButtons = (decltype(_pressedMouseButtons))method_setImplementation(ns_method, (IMP)MypressedMouseButtons);

    ns_method = class_getClassMethod([NSEvent class], @selector(mouseLocation));
    _mouseLocation = (decltype(_mouseLocation))method_setImplementation(ns_method, (IMP)MymouseLocation);

    eventsMonitor = [NSEvent addLocalMonitorForEventsMatchingMask : mask handler : ^ NSEvent * (NSEvent * event) {
        NSView* view = [[event window]contentView];
        auto* inst = NSView_Hook::Inst();
        bool hide_app_inputs = inst->HideApplicationInputs;
        bool hide_overlay_inputs = inst->HideOverlayInputs;

        switch ([event type])
        {
            case NSEventTypeKeyDown:
            case NSEventTypeKeyUp:
            {
                if ([event isARepeat] != YES)
                {
                    int key_count = 0;
                    for (auto const& key : inst->NativeKeyCombination)
                    {
                        if (GetKeyState(key))
                            ++key_count;
                    }

                    if (key_count == inst->NativeKeyCombination.size())
                    {// All shortcut keys are pressed
                        if (!inst->KeyCombinationPushed)
                        {
                            inst->KeyCombinationCallback();

                            if (inst->HideOverlayInputs)
                                hide_overlay_inputs = true;

                            if(inst->HideApplicationInputs)
                            {
                                hide_app_inputs = true;

                                // Save the last known cursor pos when opening the overlay
                                // so we can spoof the mouseLocation return value.
                                savedLocation = inst->_mouseLocation(self, sel);
                            }
                            inst->KeyCombinationPushed = true;
                        }
                    }
                    else
                    {
                        inst->KeyCombinationPushed = false;
                    }
                }
            }
            break;

            case NSEventTypeLeftMouseDragged:
            case NSEventTypeLeftMouseDown:
            case NSEventTypeLeftMouseUp:
            case NSEventTypeMouseMoved:
            {
                NSPoint p = [event locationInWindow];
                NSRect bounds = [view bounds];

                // 3 pixels outside the window and 2 pixels inside.
                if ((p.x >= -4.0f && p.x <= 3.0f) ||
                    (p.x >= (bounds.size.width - 2) && p.x <= (bounds.size.width + 3.0f)) ||
                    (p.y >= -4.0f && p.y <= 3.0f) ||
                    (p.y >= (bounds.size.height - 2) && p.y <= (bounds.size.height + 3.0f)))
                {// Allow window resize
                    hide_overlay_inputs = false;
                }
            }
            break;
        }

        if (!hide_overlay_inputs)
            ImGui_ImplOSX_HandleEvent(event, view);

        return (hide_app_inputs ? nil : event);
    }];
}

- (void)StopHook
{
    Method ns_method = class_getClassMethod([NSEvent class], @selector(pressedMouseButtons));
    method_setImplementation(ns_method, (IMP)_pressedMouseButtons);

    ns_method = class_getClassMethod([NSEvent class], @selector(mouseLocation));
    method_setImplementation(ns_method, (IMP)_mouseLocation);

    [NSEvent removeMonitor : eventsMonitor] ;
    eventsMonitor = nil;
}

@end

NSPoint MymouseLocation(id self, SEL sel)
{
    if (NSView_Hook::Inst()->HideAppInputs)
        return savedLocation;

    return _mouseLocation(self, sel);
}

NSInteger MypressedMouseButtons(id self, SEL sel)
{
    if (NSView_Hook::Inst()->HideAppInputs)
        return 0;

    return _pressedMouseButtons(self, sel);
}


void get_window_size_from_sharedApplication(double* width, double* height)
{
    NSApplication* app = [NSApplication sharedApplication];
    NSWindow* window = [app _mainWindow];
    NSView* view = window.contentView;
    NSSize size = [view frame].size;
    *width = size.width;
    *height = size.height;
}

constexpr decltype(NSView_Hook::DLL_NAME) NSView_Hook::DLL_NAME;

NSView_Hook* NSView_Hook::_inst = nullptr;

uint32_t ToggleKeyToNativeKey(ingame_overlay::ToggleKey k)
{
    struct {
        ingame_overlay::ToggleKey lib_key;
        uint32_t native_key;
    } mapping[] = {
        { ingame_overlay::ToggleKey::ALT  , kVK_Option   },
        { ingame_overlay::ToggleKey::CTRL , kVK_Control  },
        { ingame_overlay::ToggleKey::SHIFT, kVK_Shift    },
        { ingame_overlay::ToggleKey::TAB  , kVK_Tab      },
        { ingame_overlay::ToggleKey::F1   , kVK_F1       },
        { ingame_overlay::ToggleKey::F2   , kVK_F2       },
        { ingame_overlay::ToggleKey::F3   , kVK_F3       },
        { ingame_overlay::ToggleKey::F4   , kVK_F4       },
        { ingame_overlay::ToggleKey::F5   , kVK_F5       },
        { ingame_overlay::ToggleKey::F6   , kVK_F6       },
        { ingame_overlay::ToggleKey::F7   , kVK_F7       },
        { ingame_overlay::ToggleKey::F8   , kVK_F8       },
        { ingame_overlay::ToggleKey::F9   , kVK_F9       },
        { ingame_overlay::ToggleKey::F10  , kVK_F10      },
        { ingame_overlay::ToggleKey::F11  , kVK_F11      },
        { ingame_overlay::ToggleKey::F12  , kVK_F12      },
    };

    for (auto const& item : mapping)
    {
        if (item.lib_key == k)
            return item.native_key;
    }

    return 0;
}

bool NSView_Hook::StartHook(std::function<void()>& _key_combination_callback, std::set<ingame_overlay::ToggleKey> const& toggle_keys)
{
    if (!_Hooked)
    {
        if(!_key_combination_callback)
        {
            SPDLOG_ERROR("Failed to hook NSView: No key combination callback.");
            return false;
        }

        if (toggle_keys.empty())
        {
            SPDLOG_ERROR("Failed to hook NSView: No key combination.");
            return false;
        }

        void* hAppKit = System::Library::GetLibraryHandle(DLL_NAME);
        if (hAppKit == nullptr)
        {
            SPDLOG_WARN("Failed to hook NSView: Cannot find {}", DLL_NAME);
            return false;
        }

        System::Library::Library libAppKit;
        LibraryName = System::Library::GetLibraryPath(hAppKit);
        if (!libAppKit.OpenLibrary(LibraryName, false))
        {
            SPDLOG_WARN("Failed to hook NSView: Cannot load {}", LibraryName);
            return false;
        }

        SPDLOG_INFO("Hooked NSView");
        KeyCombinationCallback = std::move(_key_combination_callback);

        for (auto& key : toggle_keys)
        {
            uint32_t k = ToggleKeyToNativeKey(key);
            if (k != 0)
            {
                NativeKeyCombination.insert(k);
            }
        }

        _Hooked = true;
    }
    return true;
}

void NSView_Hook::HideAppInputs(bool hide)
{
    HideApplicationInputs = hide;
}

void NSView_Hook::HideOverlayInputs(bool hide)
{
    HideOverlayInputs = hide;
}

void NSView_Hook::ResetRenderState()
{
    if (_Initialized)
    {
        [(NSViewHook*)_NSViewHook StopHook] ;

        _Initialized = false;
        
        HideAppInputs(false);
        HideOverlayInputs(true);

        ImGui_ImplOSX_Shutdown();
    }
}

bool NSView_Hook::PrepareForOverlay()
{
    if (!_Hooked)
        return false;

    if (!_Initialized)
    {
        NSApplication* app = [NSApplication sharedApplication];
        NSWindow* window = [app _mainWindow];

        NSViewHook* hook = [[NSViewHook alloc]init];
        if (hook == nil)
        {
            SPDLOG_WARN("Failed to start NSView hook.");
            return false;
        }

        [hook StartHook : window];

        _NSViewHook = hook;

        double width, height;

        get_window_size_from_sharedApplication(&width, &height);

        ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);

        ImGui_ImplOSX_Init([window contentView]);

        _Initialized = true;
    }

    ImGui_ImplOSX_NewFrame([((NSViewHook*)_NSViewHook)->window contentView]);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////

NSView_Hook::NSView_Hook() :
    _Initialized(false),
    _HideApplicationInputs(false),
    _HideOverlayInputs(true),
    _NSViewHook(nullptr),
    _Hooked(false),
    KeyCombinationPushed(false)
{
}

NSView_Hook::~NSView_Hook()
{
    SPDLOG_INFO("NSView Hook removed");

    ResetRenderState();

    NSViewHook* hook = (NSViewHook*)_NSViewHook;
    [hook release] ;
    _NSViewHook = nil;

    _inst = nullptr;
}

NSView_Hook* NSView_Hook::Inst()
{
    if (_inst == nullptr)
        _inst = new NSView_Hook;

    return _inst;
}

std::string NSView_Hook::GetLibraryName() const
{
    return LibraryName;
}