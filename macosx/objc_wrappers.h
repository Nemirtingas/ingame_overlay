
#include "cppNSEvent.h"
#include "cppNSView.h"

struct WrapperNSView;

// Use a new struct, so we can access the private field
typedef struct _WrapperNSOpenGLContext
{
    /*NSObject*/void* _super;
    WrapperNSView* view;
    /*CGLContextObject*/void* _CGLContextObject;
} WrapperNSOpenGLContext;

class ObjCHookWrapper
{
    void* hook_nsview;
    bool hooked;
    
public:
    ObjCHookWrapper();
    ~ObjCHookWrapper();
    bool HookMainWindow();
    void UnhookMainWindow();
    cppNSView GetNSView();
    void SetEventHandler(bool(*event_handler)(cppNSEvent*, cppNSView*));
};

#ifdef __cplusplus
extern "C" {
#endif

void __cdecl NSOpenGLContext_flushBuffer(WrapperNSOpenGLContext* self, const char* sel);

void get_window_size_from_NSOpenGLContext(WrapperNSOpenGLContext*, double* width, double* height);
void get_window_size_from_sharedApplication(double* width, double* height);

#ifdef __cplusplus
}
#endif
