#include "cppNSView.h"

#include <AppKit/AppKit.h>

cppNSView::cppNSView(objcNSView* nsview):
    nsview(nsview)
{
    // Increment ref counter
    [(::NSView*)nsview retain];
}

cppNSView::cppNSView(cppNSView const& r):
    nsview(r.nsview)
{
    // Increment ref counter
    [(::NSView*)nsview retain];
}

cppNSView::cppNSView(cppNSView&& r):
    nsview(r.nsview)
{
    r.nsview = nil;
}

cppNSView::~cppNSView()
{
    // Decrement ref counter
    [(::NSView*)nsview release];
}

cppNSRect cppNSView::Bounds()
{
    NSRect r = [(::NSView*)nsview bounds];
    return cppNSRect{{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}

cppNSRect cppNSView::Frame()
{
    NSRect r = [(::NSView*)nsview frame];
    return cppNSRect{{r.origin.x, r.origin.y}, {r.size.width, r.size.height}};
}
