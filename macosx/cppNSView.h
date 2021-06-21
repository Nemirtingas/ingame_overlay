#pragma once

#include "cppNSObject.h"

struct objcNSView;

struct cppCGPoint
{
    cppCGFloat x;
    cppCGFloat y;
};

struct cppCGSize
{
    cppCGFloat width;
    cppCGFloat height;
};

struct cppNSRect
{
    cppCGPoint origin;
    cppCGSize size;
};

class cppNSView : public cppNSObject
{
public:
    cppNSView(objcNSView* nsview);
    cppNSView(cppNSView const& r);
    cppNSView(cppNSView && r);
    
    // This can be casted to NSView*
    inline objcNSView* NSView(){ return (objcNSView*)nsobject; }
    
    // Objective-C members
    cppNSRect Bounds();
    cppNSRect Frame();
};
