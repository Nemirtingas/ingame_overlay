#pragma once

class ObjCHookWrapper
{
    void* hook_nsview;
    bool hooked;
    
public:
    ObjCHookWrapper();
    ~ObjCHookWrapper();
    bool HookMainWindow();
    void UnhookMainWindow();
    void* GetNSView();
};

#ifdef __cplusplus
extern "C" {
#endif

void get_window_size_from_sharedApplication(double* width, double* height);

#ifdef __cplusplus
}
#endif
