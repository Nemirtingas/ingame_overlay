#include "cppNSEvent.h"

#include <AppKit/AppKit.h>
#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>

/////////////////////
// cppNSEventMouse
cppNSEventMouse::cppNSEventMouse(objcNSEvent* nsevent):
    nsevent(nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEventMouse::cppNSEventMouse(cppNSEventMouse const& r):
    nsevent(r.nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEventMouse::cppNSEventMouse(cppNSEventMouse&& r):
    nsevent(r.nsevent)
{
    r.nsevent = nil;
}

cppNSEventMouse::~cppNSEventMouse()
{
    [(::NSEvent*)nsevent release];
}

cppCGFloat cppNSEventMouse::X()
{
    return [(::NSEvent*)nsevent locationInWindow].x;
}

cppCGFloat cppNSEventMouse::Y()
{
    return [(::NSEvent*)nsevent locationInWindow].y;
}

/////////////////////
// cppNSEventKey
cppNSEventKey::cppNSEventKey(objcNSEvent* nsevent):
    nsevent(nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEventKey::cppNSEventKey(cppNSEventKey const& r):
    nsevent(r.nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEventKey::cppNSEventKey(cppNSEventKey&& r):
    nsevent(r.nsevent)
{
    r.nsevent = nil;
}

cppNSEventKey::~cppNSEventKey()
{
    [(::NSEvent*)nsevent release];
}

unsigned short cppNSEventKey::KeyCode()
{
    return [(::NSEvent*)nsevent keyCode];
}

unsigned int cppNSEventKey::Modifier()
{
    return [(::NSEvent*)nsevent modifierFlags];
}

bool cppNSEventKey::IsARepeat()
{
    return ([(::NSEvent*)nsevent isARepeat] == YES ? true : false);
}

/////////////////////
// cppNSEventKey
cppNSEvent::cppNSEvent(objcNSEvent* nsevent):
    nsevent(nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEvent::cppNSEvent(cppNSEvent const& r):
    nsevent(r.nsevent)
{
    [(::NSEvent*)nsevent retain];
}

cppNSEvent::cppNSEvent(cppNSEvent&& r):
    nsevent(r.nsevent)
{
    r.nsevent = nil;
}

cppNSEvent::~cppNSEvent()
{
    [(::NSEvent*)nsevent release];
}

cppNSEventType cppNSEvent::Type()
{
    return (cppNSEventType)[(::NSEvent*)nsevent type];
}
