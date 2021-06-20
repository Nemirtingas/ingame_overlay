#pragma once

#include "cppObjcTypes.h"

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

class cppNSView
{
    objcNSView* nsview;
public:
    cppNSView(objcNSView* nsview);
    cppNSView(cppNSView const& r);
    cppNSView(cppNSView && r);
    ~cppNSView();
    
    // This can be casted to NSView*
    inline objcNSView* NSView(){ return nsview; }
    
    // Objective-C members
    cppNSRect Bounds();
    cppNSRect Frame();
};
