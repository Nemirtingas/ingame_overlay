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

// Its way easier to redefine a simple interface than to use Windows::Gaming::Input. It links against C++20 and coroutines for whatever reason.

#pragma once

#include <windows.h>

#include <atomic>

namespace SimpleWindowsGamingInput {
    struct EventRegistrationToken
    {
        int64_t value;
    };

    struct HSTRING_;
    typedef HSTRING_* HSTRING;

    struct HSTRING_HEADER
    {
        union {
            PVOID Reserved1;
#if defined(_WIN64)
            char Reserved2[24];
#else
            char Reserved2[20];
#endif
        } Reserved;
    };

    typedef enum TrustLevel
    {
        BaseTrust = 0,
        PartialTrust = (BaseTrust + 1),
        FullTrust = (PartialTrust + 1)
    } 	TrustLevel;

    MIDL_INTERFACE("AF86E2E0-B12D-4c6a-9C5A-D7AA65101E90")
    IInspectable : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG * iidCount, IID * *iids) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trustLevel) = 0;
    };

    struct EventHandlerIUnknown
    {
        IID _InterfaceId;
        std::atomic_int _Refcount;
        HRESULT(*_Callback)(SimpleWindowsGamingInput::IInspectable* sender, void* arg);

    protected:
        EventHandlerIUnknown(IID const& interfaceId, decltype(_Callback) callback);

    public:
        virtual HRESULT STDMETHODCALLTYPE QueryInterface(IID const& riid, void** ppvObject);

        virtual ULONG STDMETHODCALLTYPE AddRef(void);

        virtual ULONG STDMETHODCALLTYPE Release(void);

        virtual HRESULT STDMETHODCALLTYPE Invoke(SimpleWindowsGamingInput::IInspectable* sender, void* arg);
    };

    MIDL_INTERFACE("1baf6522-5f64-42c5-8267-b9fe2215bfbd")
    IGameController : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE add_HeadsetConnected(
            /* __FITypedEventHandler_2_Windows__CGaming__CInput__CIGameController_Windows__CGaming__CInput__CHeadset* */ void *value,
            SimpleWindowsGamingInput::EventRegistrationToken * token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_HeadsetConnected(
            SimpleWindowsGamingInput::EventRegistrationToken token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_HeadsetDisconnected(
            /* __FITypedEventHandler_2_Windows__CGaming__CInput__CIGameController_Windows__CGaming__CInput__CHeadset* */ void *value,
            SimpleWindowsGamingInput::EventRegistrationToken* token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_HeadsetDisconnected(
            SimpleWindowsGamingInput::EventRegistrationToken token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_UserChanged(
            /* __FITypedEventHandler_2_Windows__CGaming__CInput__CIGameController_Windows__CSystem__CUserChangedEventArgs* */ void *value,
            SimpleWindowsGamingInput::EventRegistrationToken* token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_UserChanged(
            SimpleWindowsGamingInput::EventRegistrationToken token
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Headset(
            /* ABI::Windows::Gaming::Input::IHeadset** */ void** value
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_IsWireless(
            boolean* value
            ) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_User(
            /* ABI::Windows::System::IUser** */ void** value
            ) = 0;
    };

    enum GameControllerSwitchPosition : int
    {
        GameControllerSwitchPosition_Center = 0,
        GameControllerSwitchPosition_Up = 1,
        GameControllerSwitchPosition_UpRight = 2,
        GameControllerSwitchPosition_Right = 3,
        GameControllerSwitchPosition_DownRight = 4,
        GameControllerSwitchPosition_Down = 5,
        GameControllerSwitchPosition_DownLeft = 6,
        GameControllerSwitchPosition_Left = 7,
        GameControllerSwitchPosition_UpLeft = 8,
    };

    enum GameControllerButtonLabel : int
    {
        GameControllerButtonLabel_None = 0,
        GameControllerButtonLabel_XboxBack = 1,
        GameControllerButtonLabel_XboxStart = 2,
        GameControllerButtonLabel_XboxMenu = 3,
        GameControllerButtonLabel_XboxView = 4,
        GameControllerButtonLabel_XboxUp = 5,
        GameControllerButtonLabel_XboxDown = 6,
        GameControllerButtonLabel_XboxLeft = 7,
        GameControllerButtonLabel_XboxRight = 8,
        GameControllerButtonLabel_XboxA = 9,
        GameControllerButtonLabel_XboxB = 10,
        GameControllerButtonLabel_XboxX = 11,
        GameControllerButtonLabel_XboxY = 12,
        GameControllerButtonLabel_XboxLeftBumper = 13,
        GameControllerButtonLabel_XboxLeftTrigger = 14,
        GameControllerButtonLabel_XboxLeftStickButton = 15,
        GameControllerButtonLabel_XboxRightBumper = 16,
        GameControllerButtonLabel_XboxRightTrigger = 17,
        GameControllerButtonLabel_XboxRightStickButton = 18,
        GameControllerButtonLabel_XboxPaddle1 = 19,
        GameControllerButtonLabel_XboxPaddle2 = 20,
        GameControllerButtonLabel_XboxPaddle3 = 21,
        GameControllerButtonLabel_XboxPaddle4 = 22,
        GameControllerButtonLabel_Mode = 23,
        GameControllerButtonLabel_Select = 24,
        GameControllerButtonLabel_Menu = 25,
        GameControllerButtonLabel_View = 26,
        GameControllerButtonLabel_Back = 27,
        GameControllerButtonLabel_Start = 28,
        GameControllerButtonLabel_Options = 29,
        GameControllerButtonLabel_Share = 30,
        GameControllerButtonLabel_Up = 31,
        GameControllerButtonLabel_Down = 32,
        GameControllerButtonLabel_Left = 33,
        GameControllerButtonLabel_Right = 34,
        GameControllerButtonLabel_LetterA = 35,
        GameControllerButtonLabel_LetterB = 36,
        GameControllerButtonLabel_LetterC = 37,
        GameControllerButtonLabel_LetterL = 38,
        GameControllerButtonLabel_LetterR = 39,
        GameControllerButtonLabel_LetterX = 40,
        GameControllerButtonLabel_LetterY = 41,
        GameControllerButtonLabel_LetterZ = 42,
        GameControllerButtonLabel_Cross = 43,
        GameControllerButtonLabel_Circle = 44,
        GameControllerButtonLabel_Square = 45,
        GameControllerButtonLabel_Triangle = 46,
        GameControllerButtonLabel_LeftBumper = 47,
        GameControllerButtonLabel_LeftTrigger = 48,
        GameControllerButtonLabel_LeftStickButton = 49,
        GameControllerButtonLabel_Left1 = 50,
        GameControllerButtonLabel_Left2 = 51,
        GameControllerButtonLabel_Left3 = 52,
        GameControllerButtonLabel_RightBumper = 53,
        GameControllerButtonLabel_RightTrigger = 54,
        GameControllerButtonLabel_RightStickButton = 55,
        GameControllerButtonLabel_Right1 = 56,
        GameControllerButtonLabel_Right2 = 57,
        GameControllerButtonLabel_Right3 = 58,
        GameControllerButtonLabel_Paddle1 = 59,
        GameControllerButtonLabel_Paddle2 = 60,
        GameControllerButtonLabel_Paddle3 = 61,
        GameControllerButtonLabel_Paddle4 = 62,
        GameControllerButtonLabel_Plus = 63,
        GameControllerButtonLabel_Minus = 64,
        GameControllerButtonLabel_DownLeftArrow = 65,
        GameControllerButtonLabel_DialLeft = 66,
        GameControllerButtonLabel_DialRight = 67,
        GameControllerButtonLabel_Suspension = 68,
    };

    enum GameControllerSwitchKind : int
    {
        GameControllerSwitchKind_TwoWay = 0,
        GameControllerSwitchKind_FourWay = 1,
        GameControllerSwitchKind_EightWay = 2,
    };

    MIDL_INTERFACE("7cad6d91-a7e1-4f71-9a78-33e9c5dfea62")
    IRawGameController : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE get_AxisCount(INT32 * value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ButtonCount(INT32* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_ForceFeedbackMotors(/* __FIVectorView_1_Windows__CGaming__CInput__CForceFeedback__CForceFeedbackMotor** */ void** value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_HardwareProductId(UINT16* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_HardwareVendorId(UINT16* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_SwitchCount(INT32* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetButtonLabel(INT32 buttonIndex, GameControllerButtonLabel* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetCurrentReading(UINT32 buttonArrayLength, boolean* buttonArray, UINT32 switchArrayLength, GameControllerSwitchPosition* switchArray, UINT32 axisArrayLength, DOUBLE* axisArray, UINT64* timestamp) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetSwitchKind(INT32 switchIndex, GameControllerSwitchKind* value) = 0;
    };

    struct GamepadVibration
    {
        DOUBLE LeftMotor;
        DOUBLE RightMotor;
        DOUBLE LeftTrigger;
        DOUBLE RightTrigger;
    };

    enum GamepadButtons : unsigned int
    {
        GamepadButtons_None = 0,
        GamepadButtons_Menu = 0x1,
        GamepadButtons_View = 0x2,
        GamepadButtons_A = 0x4,
        GamepadButtons_B = 0x8,
        GamepadButtons_X = 0x10,
        GamepadButtons_Y = 0x20,
        GamepadButtons_DPadUp = 0x40,
        GamepadButtons_DPadDown = 0x80,
        GamepadButtons_DPadLeft = 0x100,
        GamepadButtons_DPadRight = 0x200,
        GamepadButtons_LeftShoulder = 0x400,
        GamepadButtons_RightShoulder = 0x800,
        GamepadButtons_LeftThumbstick = 0x1000,
        GamepadButtons_RightThumbstick = 0x2000,
        GamepadButtons_Paddle1 = 0x4000,
        GamepadButtons_Paddle2 = 0x8000,
        GamepadButtons_Paddle3 = 0x10000,
        GamepadButtons_Paddle4 = 0x20000,
    };

    struct GamepadReading
    {
        UINT64 Timestamp;
        GamepadButtons Buttons;
        DOUBLE LeftTrigger;
        DOUBLE RightTrigger;
        DOUBLE LeftThumbstickX;
        DOUBLE LeftThumbstickY;
        DOUBLE RightThumbstickX;
        DOUBLE RightThumbstickY;
    };

    MIDL_INTERFACE("bc7bb43c-0a69-3903-9e9d-a50f86a45de5")
    IGamepad : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE get_Vibration(SimpleWindowsGamingInput::GamepadVibration* value) = 0;
        virtual HRESULT STDMETHODCALLTYPE put_Vibration(SimpleWindowsGamingInput::GamepadVibration value) = 0;
        virtual HRESULT STDMETHODCALLTYPE GetCurrentReading(SimpleWindowsGamingInput::GamepadReading* value) = 0;
    };

    struct RawGameControllerEventHandler : public SimpleWindowsGamingInput::EventHandlerIUnknown
    {
    public:
        RawGameControllerEventHandler(HRESULT(*callback)(SimpleWindowsGamingInput::IInspectable* sender, IRawGameController* gamepad));
    };

    struct GamepadEventHandler : public SimpleWindowsGamingInput::EventHandlerIUnknown
    {
    public:
        GamepadEventHandler(HRESULT(*callback)(SimpleWindowsGamingInput::IInspectable* sender, IGamepad* gamepad));
    };

    struct IVectorView : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE GetAt(unsigned index, void** item) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Size(unsigned* size) = 0;
        virtual HRESULT STDMETHODCALLTYPE IndexOf(void* value, unsigned* index, boolean* found) = 0;
    };

    template<typename T>
    struct VectorView : public SimpleWindowsGamingInput::IVectorView
    {
    public:
        HRESULT STDMETHODCALLTYPE GetAt(unsigned index, T* item)
        {
            return static_cast<SimpleWindowsGamingInput::IVectorView*>(this)->GetAt(index, (void**)item);
        }

        HRESULT STDMETHODCALLTYPE IndexOf(T value, unsigned* index, boolean* found)
        {
            return static_cast<SimpleWindowsGamingInput::IVectorView*>(this)->IndexOf(value, index, found);
        }
    };

    MIDL_INTERFACE("eb8d0792-e95a-4b19-afc7-0a59f8bf759e")
    IRawGameControllerStatics : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE add_RawGameControllerAdded(RawGameControllerEventHandler* value, SimpleWindowsGamingInput::EventRegistrationToken * token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_RawGameControllerAdded(SimpleWindowsGamingInput::EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_RawGameControllerRemoved(RawGameControllerEventHandler* value, SimpleWindowsGamingInput::EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_RawGameControllerRemoved(SimpleWindowsGamingInput::EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_RawGameControllers(VectorView<IRawGameController*>** value) = 0;
        //virtual HRESULT STDMETHODCALLTYPE FromGameController(
        //    ABI::Windows::Gaming::Input::IGameController* gameController,
        //    ABI::Windows::Gaming::Input::IRawGameController** value
        //    ) = 0;
    };

    MIDL_INTERFACE("8bbce529-d49c-39e9-9560-e47dde96b7c8")
    IGamepadStatics : public SimpleWindowsGamingInput::IInspectable
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE add_GamepadAdded(GamepadEventHandler* value, EventRegistrationToken * token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_GamepadAdded(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE add_GamepadRemoved(GamepadEventHandler* value, EventRegistrationToken* token) = 0;
        virtual HRESULT STDMETHODCALLTYPE remove_GamepadRemoved(EventRegistrationToken token) = 0;
        virtual HRESULT STDMETHODCALLTYPE get_Gamepads(VectorView<IGamepad*>** value) = 0;
    };

    IRawGameControllerStatics* GetRawGameControllerStatics();
    IGamepadStatics* GetGamepadStatics();
}