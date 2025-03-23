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

#include "SimpleWindowsGamingInput.h"

namespace SimpleWindowsGamingInput
{

#define DEFINE_GUID_(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        static const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID_(IID___FIEventHandler_1_Windows__CGaming__CInput__CRawGameController, 0x00621c22, 0x42e8, 0x529f, 0x92, 0x70, 0x83, 0x6b, 0x32, 0x93, 0x1d, 0x72);
DEFINE_GUID_(IID___FIEventHandler_1_Windows__CGaming__CInput__CGamepad, 0x8a7639ee, 0x624a, 0x501a, 0xbb, 0x53, 0x56, 0x2d, 0x1e, 0xc1, 0x1b, 0x52);

DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIArcadeStickStatics, 0x5c37b8c8, 0x37b1, 0x4ad8, 0x94, 0x58, 0x20, 0x0f, 0x1a, 0x30, 0x01, 0x8e);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIArcadeStickStatics2, 0x52b5d744, 0xbb86, 0x445a, 0xb5, 0x9c, 0x59, 0x6f, 0x0e, 0x2a, 0x49, 0xdf);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIFlightStickStatics, 0x5514924a, 0xfecc, 0x435e, 0x83, 0xdc, 0x5c, 0xec, 0x8a, 0x18, 0xa5, 0x20);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIGameController, 0x1baf6522, 0x5f64, 0x42c5, 0x82, 0x67, 0xb9, 0xfe, 0x22, 0x15, 0xbf, 0xbd);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIGameControllerBatteryInfo, 0xdcecc681, 0x3963, 0x4da6, 0x95, 0x5d, 0x55, 0x3f, 0x3b, 0x6f, 0x61, 0x61);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIGamepadStatics, 0x8bbce529, 0xd49c, 0x39e9, 0x95, 0x60, 0xe4, 0x7d, 0xde, 0x96, 0xb7, 0xc8);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIGamepadStatics2, 0x42676dc5, 0x0856, 0x47c4, 0x92, 0x13, 0xb3, 0x95, 0x50, 0x4c, 0x3a, 0x3c);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIRacingWheelStatics, 0x3ac12cd5, 0x581b, 0x4936, 0x9f, 0x94, 0x69, 0xf1, 0xe6, 0x51, 0x4c, 0x7d);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIRacingWheelStatics2, 0xe666bcaa, 0xedfd, 0x4323, 0xa9, 0xf6, 0x3c, 0x38, 0x40, 0x48, 0xd1, 0xed);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIRawGameController, 0x7cad6d91, 0xa7e1, 0x4f71, 0x9a, 0x78, 0x33, 0xe9, 0xc5, 0xdf, 0xea, 0x62);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIRawGameController2, 0x43c0c035, 0xbb73, 0x4756, 0xa7, 0x87, 0x3e, 0xd6, 0xbe, 0xa6, 0x17, 0xbd);
DEFINE_GUID_(IID___x_ABI_CWindows_CGaming_CInput_CIRawGameControllerStatics, 0xeb8d0792, 0xe95a, 0x4b19, 0xaf, 0xc7, 0x0a, 0x59, 0xf8, 0xbf, 0x75, 0x9e);

DEFINE_GUID_(IID_IAgileObject, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
DEFINE_GUID_(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID_(IID_IMarshal, 0x00000003, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

typedef HRESULT (WINAPI WindowsCreateStringReference_t)(PCWSTR sourceString, UINT32 length, HSTRING_HEADER* hstringHeader, HSTRING* string);
typedef HRESULT (WINAPI RoGetActivationFactory_t)(HSTRING activatableClassId, IID const& iid, void** factory);

static constexpr WCHAR COMBASE_DLL_NAME[] = L"combase.dll";
static constexpr WCHAR RuntimeClass_Windows_Gaming_Input_RawGameController[] = L"Windows.Gaming.Input.RawGameController";
static constexpr WCHAR RuntimeClass_Windows_Gaming_Input_Gamepad[] = L"Windows.Gaming.Input.Gamepad";

EventHandlerIUnknown::EventHandlerIUnknown(IID const& interfaceId, decltype(_Callback) callback):
    _InterfaceId(interfaceId),
    _Refcount(1),
    _Callback(callback)
{
}

HRESULT STDMETHODCALLTYPE EventHandlerIUnknown::QueryInterface(IID const& riid, void** ppvObject)
{
    if (!ppvObject) {
        return E_INVALIDARG;
    }

    *ppvObject = NULL;
    if (riid == IID_IUnknown || riid == IID_IAgileObject || riid == _InterfaceId) {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }
    else if (riid == IID_IMarshal)
    {
        return E_OUTOFMEMORY;
    }
    else
    {
        return E_NOINTERFACE;
    }
}

ULONG STDMETHODCALLTYPE EventHandlerIUnknown::AddRef(void)
{
    return _Refcount.fetch_add(1);
}

ULONG STDMETHODCALLTYPE EventHandlerIUnknown::Release(void)
{
    return _Refcount.fetch_add(-1);
}

HRESULT STDMETHODCALLTYPE EventHandlerIUnknown::Invoke(SimpleWindowsGamingInput::IInspectable* sender, void* arg)
{
    return _Callback(sender, arg);
}

RawGameControllerEventHandler::RawGameControllerEventHandler(HRESULT(*callback)(SimpleWindowsGamingInput::IInspectable*, IRawGameController*)) :
    EventHandlerIUnknown(IID___FIEventHandler_1_Windows__CGaming__CInput__CRawGameController, (decltype(_Callback))callback)
{
}

GamepadEventHandler::GamepadEventHandler(HRESULT(*callback)(SimpleWindowsGamingInput::IInspectable*, IGamepad*)) :
    EventHandlerIUnknown(IID___FIEventHandler_1_Windows__CGaming__CInput__CGamepad, (decltype(_Callback))callback)
{
}

IRawGameControllerStatics* GetRawGameControllerStatics()
{
    auto hCombase = GetModuleHandleW(COMBASE_DLL_NAME);
    if (hCombase == nullptr || hCombase == INVALID_HANDLE_VALUE)
        return nullptr;

    auto _WindowsCreateStringReference = (WindowsCreateStringReference_t*)GetProcAddress(hCombase, "WindowsCreateStringReference");
    auto _RoGetActivationFactory = (RoGetActivationFactory_t*)GetProcAddress(hCombase, "RoGetActivationFactory");

    IRawGameControllerStatics* rawControllerStatics = nullptr;

    if (_WindowsCreateStringReference == nullptr || _RoGetActivationFactory == nullptr)
        return nullptr;

    HSTRING_HEADER h{};
    HSTRING hstr;
    auto hr = _WindowsCreateStringReference(RuntimeClass_Windows_Gaming_Input_RawGameController, wcslen(RuntimeClass_Windows_Gaming_Input_RawGameController), &h, &hstr);
    if (!SUCCEEDED(hr))
        return nullptr;

    hr = _RoGetActivationFactory(hstr, IID___x_ABI_CWindows_CGaming_CInput_CIRawGameControllerStatics, (void**)&rawControllerStatics);
    if (!SUCCEEDED(hr))
        return nullptr;

    return rawControllerStatics;
}

IGamepadStatics* GetGamepadStatics()
{
    auto hCombase = GetModuleHandleW(COMBASE_DLL_NAME);
    if (hCombase == nullptr || hCombase == INVALID_HANDLE_VALUE)
        return nullptr;

    auto _WindowsCreateStringReference = (WindowsCreateStringReference_t*)GetProcAddress(hCombase, "WindowsCreateStringReference");
    auto _RoGetActivationFactory = (RoGetActivationFactory_t*)GetProcAddress(hCombase, "RoGetActivationFactory");

    IGamepadStatics* gamepadStatics = nullptr;

    if (_WindowsCreateStringReference == nullptr || _RoGetActivationFactory == nullptr)
        return nullptr;

    HSTRING_HEADER h{};
    HSTRING hstr;
    auto hr = _WindowsCreateStringReference(RuntimeClass_Windows_Gaming_Input_Gamepad, wcslen(RuntimeClass_Windows_Gaming_Input_Gamepad), &h, &hstr);
    if (!SUCCEEDED(hr))
        return nullptr;

    hr = _RoGetActivationFactory(hstr, IID___x_ABI_CWindows_CGaming_CInput_CIGamepadStatics, (void**)&gamepadStatics);
    if (!SUCCEEDED(hr))
        return nullptr;

    return gamepadStatics;
}

}