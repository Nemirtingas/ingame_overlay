#include "cppNSEvent.h"

#include <AppKit/AppKit.h>
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>

/////////////////////
// cppNSEventMouse
cppCGFloat cppNSEventMouse::X()
{
    return [(::NSEvent*)nsobject locationInWindow].x;
}

cppCGFloat cppNSEventMouse::Y()
{
    return [(::NSEvent*)nsobject locationInWindow].y;
}

/////////////////////
// cppNSEventKey
unsigned short cppNSEventKey::KeyCode()
{
    return [(::NSEvent*)nsobject keyCode];
}

unsigned int cppNSEventKey::Modifier()
{
    return [(::NSEvent*)nsobject modifierFlags];
}

bool cppNSEventKey::IsARepeat()
{
    return ([(::NSEvent*)nsobject isARepeat] == YES ? true : false);
}

/////////////////////
// cppNSEventKey
cppNSEvent::cppNSEvent(objcNSEvent* nsevent):
    cppNSObject((objcNSObject*)nsevent)
{}

cppNSEvent::cppNSEvent(cppNSEvent const& r):
    cppNSObject(r)
{}

cppNSEvent::cppNSEvent(cppNSEvent && r):
    cppNSObject(reinterpret_cast<cppNSEvent&&>(r))
{}

cppNSEventType cppNSEvent::Type()
{
    return (cppNSEventType)[(::NSEvent*)nsobject type];
}
