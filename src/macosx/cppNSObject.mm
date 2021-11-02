#include "cppNSObject.h"

#include <AppKit/AppKit.h>

static SEL SelectorFromString(const char* selector)
{
    NSString* nsStr = [[NSString alloc] initWithCString:selector encoding:NSUTF8StringEncoding];
    return NSSelectorFromString(nsStr);
}

cppNSObject::cppNSObject(objcNSObject* nsobject):
    nsobject(nsobject)
{
    // Increment ref counter
    [(::NSObject*)nsobject retain];
}

cppNSObject::cppNSObject(cppNSObject const& r):
    nsobject(r.nsobject)
{
    // Increment ref counter
    [(::NSObject*)nsobject retain];
}

cppNSObject::cppNSObject(cppNSObject && r):
    nsobject(r.nsobject)
{
    r.nsobject = nil;
}

cppNSObject::~cppNSObject()
{
    // Decrement ref counter
    [(::NSObject*)nsobject release];
}

bool cppNSObject::RespondsToSelector(const char* selector)
{
    SEL sel = SelectorFromString(selector);
    return ([(::NSObject*)nsobject respondsToSelector:sel] == YES ? true : false);
}

