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

static bool GetKeyState( unsigned short inKeyCode )
{
    unsigned char keyMap[16];
    GetKeys((BigEndianUInt32*) &keyMap);
    return ((keyMap[inKeyCode >> 3] >> (inKeyCode & 7)) & 1) != 0;
}

static bool IgnoreEvent(NSEvent* event)
{
    NSView* view = [[event window] contentView];
    switch([event type])
    {
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeLeftMouseDown:
        case NSEventTypeLeftMouseUp:
        case NSEventTypeMouseMoved:
        {
            NSPoint p = [event locationInWindow];
            NSRect bounds = [view bounds];

            // 5 pixels outside the window and 5 pixels inside.
            if ((p.x >= -5.0f && p.x < 5.0f) ||
                (p.x >= (bounds.size.width - 5)) ||
                (p.y >= -5.0f && p.y < 5.0f) ||
                (p.y >= (bounds.size.height - 5)))
            {// Allow window resize
                return false;
            }
        }

        case NSEventTypeRightMouseDown:
        case NSEventTypeRightMouseUp:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp:
            return true;
    }

    return false;
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
    
        Method ns_method = class_getClassMethod([NSEvent class], @selector(pressedMouseButtons));
        pressedMouseButtons = (decltype(pressedMouseButtons))method_setImplementation(ns_method, (IMP)&NSView_Hook::MypressedMouseButtons);

        ns_method = class_getClassMethod([NSEvent class], @selector(mouseLocation));
        mouseLocation = (decltype(mouseLocation))method_setImplementation(ns_method, (IMP)&NSView_Hook::MymouseLocation);

        NSInteger mask = NSEventMaskAny;
        /*
        NSEventMaskKeyDown | NSEventMaskKeyUp |
        NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp |
        NSEventMaskRightMouseDown | NSEventMaskRightMouseDragged | NSEventMaskRightMouseUp |
        NSEventMaskOtherMouseDown | NSEventMaskOtherMouseDragged | NSEventMaskOtherMouseUp |
        NSEventMaskMouseMoved |
        NSEventMaskFlagsChanged |
        NSEventMaskScrollWheel;
        */

        _EventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask : mask handler : ^ NSEvent * (NSEvent * event)
        {
            auto* inst = NSView_Hook::Inst();
            NSView* view = [[event window]contentView];
            bool hide_app_inputs = inst->ApplicationInputsHidden;
            bool hide_overlay_inputs = inst->OverlayInputsHidden;
    
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
    
                                if (inst->OverlayInputsHidden)
                                    hide_overlay_inputs = true;

                                if(inst->ApplicationInputsHidden)
                                {
                                    hide_app_inputs = true;
    
                                    // Save the last known cursor pos when opening the overlay
                                    // so we can spoof the mouseLocation return value.
                                    inst->_SavedLocation = mouseLocation(event, @selector(mouseLocation));
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
            }

            //ImGui::GetIO().SetAppAcceptingEvents(!hide_overlay_inputs);
    
            if (!hide_overlay_inputs)
                ImGui_ImplOSX_HandleEvent(event, view);
    
            if (hide_app_inputs && IgnoreEvent(event))
                return nil;
    
            return event;
        }];

        _Hooked = true;
    }
    return true;
}

void NSView_Hook::HideAppInputs(bool hide)
{
    ApplicationInputsHidden = hide;
}

void NSView_Hook::HideOverlayInputs(bool hide)
{
    OverlayInputsHidden = hide;
}

void NSView_Hook::ResetRenderState()
{
    if (_Initialized)
    {
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
        double width, height;
        NSApplication* app = [NSApplication sharedApplication];
        _Window = [app _mainWindow];

        NSView* view = [_Window contentView];
        NSSize size = [view frame].size;

        ImGui::GetIO().DisplaySize = ImVec2((float)size.width, (float)size.height);

        ImGui_ImplOSX_Init(view);

        _Initialized = true;
    }

    ImGui_ImplOSX_NewFrame([_Window contentView]);
    return true;
}

NSPoint NSView_Hook::MymouseLocation(id self, SEL sel)
{
    NSView_Hook* inst = NSView_Hook::Inst();
    if (inst->ApplicationInputsHidden)
        return inst->_SavedLocation;

    return inst->mouseLocation(self, sel);
}

NSInteger NSView_Hook::MypressedMouseButtons(id self, SEL sel)
{
    NSView_Hook* inst = NSView_Hook::Inst();
    if (inst->ApplicationInputsHidden)
        return 0;

    return inst->pressedMouseButtons(self, sel);
}



/////////////////////////////////////////////////////////////////////////////////////

NSView_Hook::NSView_Hook() :
    _Initialized(false),
    _Hooked(false),
    _EventMonitor(nil),
    _Window(nil),
    _SavedLocation{},
    pressedMouseButtons(nullptr),
    mouseLocation(nullptr),
    KeyCombinationPushed(false),
    ApplicationInputsHidden(false),
    OverlayInputsHidden(true)
{
}

NSView_Hook::~NSView_Hook()
{
    SPDLOG_INFO("NSView Hook removed");

    ResetRenderState();

    Method ns_method = class_getClassMethod([NSEvent class], @selector(pressedMouseButtons));
    method_setImplementation(ns_method, (IMP)pressedMouseButtons);

    ns_method = class_getClassMethod([NSEvent class], @selector(mouseLocation));
    method_setImplementation(ns_method, (IMP)mouseLocation);

    [NSEvent removeMonitor :_EventMonitor];
    _EventMonitor = nil;

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
