
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#include <backends/imgui_impl_osx.h>

#include "objc_wrappers.h"

@interface NSViewHook : NSObject
{
    id eventsMonitor;
    
@public
    NSWindow* window;
    bool (*EventHandler)(cppNSEvent*, cppNSView*);
}

-(void)StartHook:(NSWindow*)view;
-(void)StopHook;
@end

@implementation NSViewHook

-(void)StartHook:(NSWindow*)window;
{
    int mask = NSEventMaskKeyDown | NSEventMaskKeyUp |
               NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp |
               NSEventMaskRightMouseDown | NSEventMaskRightMouseDragged | NSEventMaskRightMouseUp |
               NSEventMaskOtherMouseDown | NSEventMaskOtherMouseDragged | NSEventMaskOtherMouseUp |
               NSEventMaskMouseMoved |
               NSEventMaskFlagsChanged |
               NSEventMaskScrollWheel;
    
    self->window = window;
    
    eventsMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^ NSEvent*(NSEvent* event){
        bool ignore_event = false;
        if(EventHandler != nil)
        {
            cppNSEvent cppevent((objcNSEvent*)event);
            cppNSView cppview((objcNSView*)[[event window] contentView]);
            
            // Forward Mouse/Keyboard events to Dear ImGui OSX backend.
            ignore_event = EventHandler(&cppevent, &cppview);
        }
        
        return (ignore_event ? nil : event);
    }];
}

-(void)StopHook
{
    [NSEvent removeMonitor:eventsMonitor];
    eventsMonitor = nil;
}

@end

//void get_window_size_from_NSOpenGLContext(WrapperNSOpenGLContext* context, double* width, double* height)
//{
//    NSView* view = (NSView*)context->view;
//    NSSize size = [view frame].size;
//    *width = size.width;
//    *height = size.height;
//}

void get_window_size_from_sharedApplication(double* width, double* height)
{
    NSApplication* app = [NSApplication sharedApplication];
    NSWindow* window = [app _mainWindow];
    NSView* view = window.contentView;
    NSSize size = [view frame].size;
    *width = size.width;
    *height = size.height;
}

ObjCHookWrapper::ObjCHookWrapper():
    hook_nsview(nullptr),
    hooked(false)
{
}

ObjCHookWrapper::~ObjCHookWrapper()
{
    UnhookMainWindow();
    NSViewHook* hook = (NSViewHook*)hook_nsview;
    [hook release];
    hook_nsview = nil;
}

bool ObjCHookWrapper::HookMainWindow()
{
    if(hooked)
        return true;
    
    hooked = true;
    
    NSApplication* app = [NSApplication sharedApplication];
    NSWindow* window = [app _mainWindow];

    NSViewHook* hook = [[NSViewHook alloc] init];
    if(hook == nil)
        return false;
    
    [hook StartHook:window];
    
    hook_nsview = hook;
    
    return true;
}

void ObjCHookWrapper::UnhookMainWindow()
{
    if(!hooked)
        return;
    
    [(NSViewHook*)hook_nsview StopHook];

    hooked = false;
}

cppNSView ObjCHookWrapper::GetNSView()
{
    NSWindow* window = ((NSViewHook*)hook_nsview)->window;
    NSView* view = [window contentView];
    return cppNSView((objcNSView*)view);
}

void ObjCHookWrapper::SetEventHandler(bool(*event_handler)(cppNSEvent*, cppNSView*))
{
    ((NSViewHook*)hook_nsview)->EventHandler = event_handler;
}
