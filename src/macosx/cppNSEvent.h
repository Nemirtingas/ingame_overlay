#pragma once

#include "cppNSObject.h"

struct objcNSEvent;

enum cppNSEventType : unsigned int {        /* various types of events */
    cppNSEventTypeLeftMouseDown             = 1,
    cppNSEventTypeLeftMouseUp               = 2,
    cppNSEventTypeRightMouseDown            = 3,
    cppNSEventTypeRightMouseUp              = 4,
    cppNSEventTypeMouseMoved                = 5,
    cppNSEventTypeLeftMouseDragged          = 6,
    cppNSEventTypeRightMouseDragged         = 7,
    cppNSEventTypeMouseEntered              = 8,
    cppNSEventTypeMouseExited               = 9,
    cppNSEventTypeKeyDown                   = 10,
    cppNSEventTypeKeyUp                     = 11,
    cppNSEventTypeFlagsChanged              = 12,
    cppNSEventTypeAppKitDefined             = 13,
    cppNSEventTypeSystemDefined             = 14,
    cppNSEventTypeApplicationDefined        = 15,
    cppNSEventTypePeriodic                  = 16,
    cppNSEventTypeCursorUpdate              = 17,
    cppNSEventTypeScrollWheel               = 22,
    cppNSEventTypeTabletPoint               = 23,
    cppNSEventTypeTabletProximity           = 24,
    cppNSEventTypeOtherMouseDown            = 25,
    cppNSEventTypeOtherMouseUp              = 26,
    cppNSEventTypeOtherMouseDragged         = 27,
    /* The following event types are available on some hardware on 10.5.2 and later */
//    NSEventTypeGesture NS_ENUM_AVAILABLE_MAC(10_5)       = 29,
//    NSEventTypeMagnify NS_ENUM_AVAILABLE_MAC(10_5)       = 30,
//    NSEventTypeSwipe   NS_ENUM_AVAILABLE_MAC(10_5)       = 31,
//    NSEventTypeRotate  NS_ENUM_AVAILABLE_MAC(10_5)       = 18,
//    NSEventTypeBeginGesture NS_ENUM_AVAILABLE_MAC(10_5)  = 19,
//    NSEventTypeEndGesture NS_ENUM_AVAILABLE_MAC(10_5)    = 20,
//
//#if __LP64__
//    NSEventTypeSmartMagnify NS_ENUM_AVAILABLE_MAC(10_8) = 32,
//#endif
//    NSEventTypeQuickLook NS_ENUM_AVAILABLE_MAC(10_8) = 33,
//
//#if __LP64__
//    NSEventTypePressure NS_ENUM_AVAILABLE_MAC(10_10_3) = 34,
//    NSEventTypeDirectTouch NS_ENUM_AVAILABLE_MAC(10_10) = 37,
//#endif
};

enum cppNSEventModifierFlags : unsigned int {
    cppNSEventModifierFlagCapsLock           = 1 << 16, // Set if Caps Lock key is pressed.
    cppNSEventModifierFlagShift              = 1 << 17, // Set if Shift key is pressed.
    cppNSEventModifierFlagControl            = 1 << 18, // Set if Control key is pressed.
    cppNSEventModifierFlagOption             = 1 << 19, // Set if Option or Alternate key is pressed.
    cppNSEventModifierFlagCommand            = 1 << 20, // Set if Command key is pressed.
    cppNSEventModifierFlagNumericPad         = 1 << 21, // Set if any key in the numeric keypad is pressed.
    cppNSEventModifierFlagHelp               = 1 << 22, // Set if the Help key is pressed.
    cppNSEventModifierFlagFunction           = 1 << 23, // Set if any function key is pressed.
    
    // Used to retrieve only the device-independent modifier flags, allowing applications to mask off the device-dependent modifier flags, including event coalescing information.
    cppNSEventModifierFlagDeviceIndependentFlagsMask    = 0xffff0000UL
};

enum cppKeyCodes : unsigned short {
    cppkVK_ANSI_A                    = 0x00,
    cppkVK_ANSI_S                    = 0x01,
    cppkVK_ANSI_D                    = 0x02,
    cppkVK_ANSI_F                    = 0x03,
    cppkVK_ANSI_H                    = 0x04,
    cppkVK_ANSI_G                    = 0x05,
    cppkVK_ANSI_Z                    = 0x06,
    cppkVK_ANSI_X                    = 0x07,
    cppkVK_ANSI_C                    = 0x08,
    cppkVK_ANSI_V                    = 0x09,
    cppkVK_ANSI_B                    = 0x0B,
    cppkVK_ANSI_Q                    = 0x0C,
    cppkVK_ANSI_W                    = 0x0D,
    cppkVK_ANSI_E                    = 0x0E,
    cppkVK_ANSI_R                    = 0x0F,
    cppkVK_ANSI_Y                    = 0x10,
    cppkVK_ANSI_T                    = 0x11,
    cppkVK_ANSI_1                    = 0x12,
    cppkVK_ANSI_2                    = 0x13,
    cppkVK_ANSI_3                    = 0x14,
    cppkVK_ANSI_4                    = 0x15,
    cppkVK_ANSI_6                    = 0x16,
    cppkVK_ANSI_5                    = 0x17,
    cppkVK_ANSI_Equal                = 0x18,
    cppkVK_ANSI_9                    = 0x19,
    cppkVK_ANSI_7                    = 0x1A,
    cppkVK_ANSI_Minus                = 0x1B,
    cppkVK_ANSI_8                    = 0x1C,
    cppkVK_ANSI_0                    = 0x1D,
    cppkVK_ANSI_RightBracket         = 0x1E,
    cppkVK_ANSI_O                    = 0x1F,
    cppkVK_ANSI_U                    = 0x20,
    cppkVK_ANSI_LeftBracket          = 0x21,
    cppkVK_ANSI_I                    = 0x22,
    cppkVK_ANSI_P                    = 0x23,
    cppkVK_ANSI_L                    = 0x25,
    cppkVK_ANSI_J                    = 0x26,
    cppkVK_ANSI_Quote                = 0x27,
    cppkVK_ANSI_K                    = 0x28,
    cppkVK_ANSI_Semicolon            = 0x29,
    cppkVK_ANSI_Backslash            = 0x2A,
    cppkVK_ANSI_Comma                = 0x2B,
    cppkVK_ANSI_Slash                = 0x2C,
    cppkVK_ANSI_N                    = 0x2D,
    cppkVK_ANSI_M                    = 0x2E,
    cppkVK_ANSI_Period               = 0x2F,
    cppkVK_ANSI_Grave                = 0x32,
    cppkVK_ANSI_KeypadDecimal        = 0x41,
    cppkVK_ANSI_KeypadMultiply       = 0x43,
    cppkVK_ANSI_KeypadPlus           = 0x45,
    cppkVK_ANSI_KeypadClear          = 0x47,
    cppkVK_ANSI_KeypadDivide         = 0x4B,
    cppkVK_ANSI_KeypadEnter          = 0x4C,
    cppkVK_ANSI_KeypadMinus          = 0x4E,
    cppkVK_ANSI_KeypadEquals         = 0x51,
    cppkVK_ANSI_Keypad0              = 0x52,
    cppkVK_ANSI_Keypad1              = 0x53,
    cppkVK_ANSI_Keypad2              = 0x54,
    cppkVK_ANSI_Keypad3              = 0x55,
    cppkVK_ANSI_Keypad4              = 0x56,
    cppkVK_ANSI_Keypad5              = 0x57,
    cppkVK_ANSI_Keypad6              = 0x58,
    cppkVK_ANSI_Keypad7              = 0x59,
    cppkVK_ANSI_Keypad8              = 0x5B,
    cppkVK_ANSI_Keypad9              = 0x5C,

    cppkVK_Return                    = 0x24,
    cppkVK_Tab                       = 0x30,
    cppkVK_Space                     = 0x31,
    cppkVK_Delete                    = 0x33,
    cppkVK_Escape                    = 0x35,
    cppkVK_Command                   = 0x37,
    cppkVK_Shift                     = 0x38,
    cppkVK_CapsLock                  = 0x39,
    cppkVK_Option                    = 0x3A,
    cppkVK_Control                   = 0x3B,
    cppkVK_RightCommand              = 0x36,
    cppkVK_RightShift                = 0x3C,
    cppkVK_RightOption               = 0x3D,
    cppkVK_RightControl              = 0x3E,
    cppkVK_Function                  = 0x3F,
    cppkVK_F17                       = 0x40,
    cppkVK_VolumeUp                  = 0x48,
    cppkVK_VolumeDown                = 0x49,
    cppkVK_Mute                      = 0x4A,
    cppkVK_F18                       = 0x4F,
    cppkVK_F19                       = 0x50,
    cppkVK_F20                       = 0x5A,
    cppkVK_F5                        = 0x60,
    cppkVK_F6                        = 0x61,
    cppkVK_F7                        = 0x62,
    cppkVK_F3                        = 0x63,
    cppkVK_F8                        = 0x64,
    cppkVK_F9                        = 0x65,
    cppkVK_F11                       = 0x67,
    cppkVK_F13                       = 0x69,
    cppkVK_F16                       = 0x6A,
    cppkVK_F14                       = 0x6B,
    cppkVK_F10                       = 0x6D,
    cppkVK_F12                       = 0x6F,
    cppkVK_F15                       = 0x71,
    cppkVK_Help                      = 0x72,
    cppkVK_Home                      = 0x73,
    cppkVK_PageUp                    = 0x74,
    cppkVK_ForwardDelete             = 0x75,
    cppkVK_F4                        = 0x76,
    cppkVK_End                       = 0x77,
    cppkVK_F2                        = 0x78,
    cppkVK_PageDown                  = 0x79,
    cppkVK_F1                        = 0x7A,
    cppkVK_LeftArrow                 = 0x7B,
    cppkVK_RightArrow                = 0x7C,
    cppkVK_DownArrow                 = 0x7D,
    cppkVK_UpArrow                   = 0x7E
};

class cppNSEventMouse : public cppNSObject
{
    cppNSEventMouse();
    virtual ~cppNSEventMouse();
public:
    // This can be casted to NSEvent*
    inline objcNSEvent* NSEvent(){ return (objcNSEvent*)nsobject; }
    
    // Objective-C members
    cppCGFloat X();
    cppCGFloat Y();
};

class cppNSEventKey : public cppNSObject
{
    cppNSEventKey();
    virtual ~cppNSEventKey();
public:
    // This can be casted to NSEvent*
    inline objcNSEvent* NSEvent(){ return (objcNSEvent*)nsobject; }
    
    // Objective-C members
    unsigned short KeyCode();
    unsigned int Modifier();
    bool IsARepeat();
};

class cppNSEvent : public cppNSObject
{
public:
    cppNSEvent(objcNSEvent* nsevent);
    cppNSEvent(cppNSEvent const& r);
    cppNSEvent(cppNSEvent&& r);
    // This can be casted to NSEvent*
    inline objcNSEvent* NSEvent(){ return (objcNSEvent*)nsobject; }
    
    // Objective-C members
    cppNSEventType Type();
    inline cppNSEventMouse* Mouse(){ return reinterpret_cast<cppNSEventMouse*>(this); }
    inline cppNSEventKey* Key(){ return reinterpret_cast<cppNSEventKey*>(this); }
};
