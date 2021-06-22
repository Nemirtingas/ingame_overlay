#include "cppNSView.h"

#include <AppKit/AppKit.h>

cppNSView::cppNSView(objcNSView* nsview):
    cppNSObject((objcNSObject*)nsview)
{}

cppNSView::cppNSView(cppNSView const& r):
    cppNSObject(r)
{}

cppNSView::cppNSView(cppNSView&& r):
    cppNSObject(reinterpret_cast<cppNSView&&>(r))
{}

cppNSRect cppNSView::Bounds()
{
    NSRect r = [(::NSView*)nsobject bounds];
    return cppNSRect{{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}

cppNSRect cppNSView::Frame()
{
    NSRect r = [(::NSView*)nsobject frame];
    return cppNSRect{{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}
