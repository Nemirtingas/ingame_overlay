// Dear ImGui: standalone example application for OSX + Metal.

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#import <Foundation/Foundation.h>

#if TARGET_OS_OSX
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#endif

#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <imgui.h>
#include <backends/imgui_impl_metal.h>
#if TARGET_OS_OSX
#include "../common/imgui_impl_osx.h"
@interface AppViewController : NSViewController<NSWindowDelegate>
@end
#else
@interface AppViewController : UIViewController
@end
#endif

#include <sys/sysctl.h>
#include <mach-o/dyld_images.h>
#include <dlfcn.h>

#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include <objc/runtime.h>

static const char* ingame_overlay_test_library = "liboverlay_example.dylib";

//-----------------------------------------------------------------------------------
// Test scenarios, chosen with environment variables. tests/macos_metal/check_scenarios.py runs them
// and checks the overlay through its MCP server.
//
// METAL_TEST_PRESENT             How the drawable is presented:
//     drawable (default)             -[MTLCommandBuffer presentDrawable:]
//     drawable_at_time               -[MTLCommandBuffer presentDrawable:atTime:]
//     drawable_min_duration          -[MTLCommandBuffer presentDrawable:afterMinimumDuration:]
//     scheduled                      -[MTLDrawable present] from a scheduled handler, like MoltenVK
//     scheduled_at_time              -[MTLDrawable presentAtTime:] from a scheduled handler, like MoltenVK with a present time
//     after_scheduled                -[MTLDrawable present] after waitUntilScheduled
// METAL_TEST_OFFSCREEN_FIRST=1   Encode an offscreen render pass before the scene, like a shadow or G-buffer pass.
// METAL_TEST_DRAWABLE_SIZE=WxH   Fixed drawable size, different from the view's (lower resolution rendering).
// METAL_TEST_SAMPLE_COUNT=N      Multisampled scene, resolved into the drawable.
// METAL_TEST_FRAMEBUFFER_ONLY=0  Copyable drawables (MTKView's default is framebufferOnly).
// METAL_TEST_UNRETAINED=1        Command buffers from commandBufferWithUnretainedReferences.
// METAL_TEST_RENDER_THREAD=1     Render on a secondary thread, like most engines. The application's own
//                                ImGui isn't drawn then: its backend isn't thread safe either.
// METAL_TEST_EVENT_STORM=1       Post mouse events to the application continuously, to exercise the input
//                                path while the overlay renders.
//-----------------------------------------------------------------------------------

enum class TestPresentMode_t
{
    Drawable,
    DrawableAtTime,
    DrawableAfterMinimumDuration,
    ScheduledHandler,
    ScheduledHandlerAtTime,
    AfterScheduled,
};

struct TestOptions_t
{
    TestPresentMode_t Present = TestPresentMode_t::Drawable;
    bool OffscreenFirst = false;
    unsigned DrawableWidth = 0;
    unsigned DrawableHeight = 0;
    unsigned SampleCount = 1;
    int FramebufferOnly = -1;
    bool Unretained = false;
    bool RenderThread = false;
    bool EventStorm = false;
};

static TestOptions_t g_Test;

static bool TestFlag(const char* name)
{
    const char* value = getenv(name);
    return value != nullptr && strcmp(value, "0") != 0 && value[0] != '\0';
}

static void LoadTestOptions()
{
    struct { const char* Name; TestPresentMode_t Mode; } const presentModes[] = {
        { "drawable"             , TestPresentMode_t::Drawable                     },
        { "drawable_at_time"     , TestPresentMode_t::DrawableAtTime               },
        { "drawable_min_duration", TestPresentMode_t::DrawableAfterMinimumDuration },
        { "scheduled"            , TestPresentMode_t::ScheduledHandler             },
        { "scheduled_at_time"    , TestPresentMode_t::ScheduledHandlerAtTime       },
        { "after_scheduled"      , TestPresentMode_t::AfterScheduled               },
    };

    if (const char* present = getenv("METAL_TEST_PRESENT"))
    {
        bool found = false;
        for (auto const& mode : presentModes)
        {
            if (strcmp(present, mode.Name) == 0)
            {
                g_Test.Present = mode.Mode;
                found = true;
            }
        }
        if (!found)
            printf("Unknown METAL_TEST_PRESENT %s, using drawable\n", present);
    }

    g_Test.OffscreenFirst = TestFlag("METAL_TEST_OFFSCREEN_FIRST");
    if (const char* size = getenv("METAL_TEST_DRAWABLE_SIZE"))
        sscanf(size, "%ux%u", &g_Test.DrawableWidth, &g_Test.DrawableHeight);
    if (const char* sampleCount = getenv("METAL_TEST_SAMPLE_COUNT"))
        g_Test.SampleCount = (unsigned)atoi(sampleCount);
    if (const char* framebufferOnly = getenv("METAL_TEST_FRAMEBUFFER_ONLY"))
        g_Test.FramebufferOnly = atoi(framebufferOnly) != 0 ? 1 : 0;
    g_Test.Unretained = TestFlag("METAL_TEST_UNRETAINED");
    g_Test.RenderThread = TestFlag("METAL_TEST_RENDER_THREAD");
    g_Test.EventStorm = TestFlag("METAL_TEST_EVENT_STORM");
}

static id<MTLCommandBuffer> NewTestCommandBuffer(id<MTLCommandQueue> commandQueue)
{
    return g_Test.Unretained ? [commandQueue commandBufferWithUnretainedReferences] : [commandQueue commandBuffer];
}

// A pass into a texture that is never presented: the overlay must not end up there.
static void EncodeOffscreenPass(id<MTLCommandBuffer> commandBuffer)
{
    static id<MTLTexture> offscreen = nil;
    if (offscreen == nil)
    {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:256 height:256 mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget;
        descriptor.storageMode = MTLStorageModePrivate;
        offscreen = [commandBuffer.device newTextureWithDescriptor:descriptor];
    }

    MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    renderPass.colorAttachments[0].texture = offscreen;
    renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderPass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
    [[commandBuffer renderCommandEncoderWithDescriptor:renderPass] endEncoding];
}

static void PresentAndCommit(id<MTLCommandBuffer> commandBuffer, id<CAMetalDrawable> drawable)
{
    if (drawable == nil)
    {
        [commandBuffer commit];
        return;
    }

    switch (g_Test.Present)
    {
        case TestPresentMode_t::Drawable:
            [commandBuffer presentDrawable:drawable];
            [commandBuffer commit];
            break;

        case TestPresentMode_t::DrawableAtTime:
            [commandBuffer presentDrawable:drawable atTime:CACurrentMediaTime()];
            [commandBuffer commit];
            break;

        case TestPresentMode_t::DrawableAfterMinimumDuration:
            [commandBuffer presentDrawable:drawable afterMinimumDuration:1.0 / 120.0];
            [commandBuffer commit];
            break;

        case TestPresentMode_t::ScheduledHandler:
            [commandBuffer addScheduledHandler:^(id<MTLCommandBuffer>) { [drawable present]; }];
            [commandBuffer commit];
            break;

        case TestPresentMode_t::ScheduledHandlerAtTime:
            [commandBuffer addScheduledHandler:^(id<MTLCommandBuffer>) { [drawable presentAtTime:CACurrentMediaTime()]; }];
            [commandBuffer commit];
            break;

        case TestPresentMode_t::AfterScheduled:
            [commandBuffer commit];
            [commandBuffer waitUntilScheduled];
            [drawable present];
            break;
    }
}

@interface AppViewController () <MTKViewDelegate>
@property (nonatomic, readonly) MTKView *mtkView;
@property (nonatomic, strong) id <MTLDevice> device;
@property (nonatomic, strong) id <MTLCommandQueue> commandQueue;
@end

static int isProcessTranslated()
{
    int ret = 0;
    size_t size = sizeof(ret);

    // Call the sysctl and if successful return the result
    if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) != -1)
        return ret;

    // If "sysctl.proc_translated" is not present then must be native
    if (errno == ENOENT)
        return 0;

    return -1;
}

static std::string getExecutablePath()
{
    std::string exec_path("./");

    task_dyld_info dyld_info;
    task_t t;
    pid_t pid = getpid();
    task_for_pid(mach_task_self(), pid, &t);
    mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;

    if (task_info(t, TASK_DYLD_INFO, reinterpret_cast<task_info_t>(&dyld_info), &count) == KERN_SUCCESS)
    {
        dyld_all_image_infos *dyld_img_infos = reinterpret_cast<dyld_all_image_infos*>(dyld_info.all_image_info_addr);
        if (isProcessTranslated() == 1)
        {
            for (int i = 0; i < dyld_img_infos->infoArrayCount; ++i)
            {
                exec_path = dyld_img_infos->infoArray[i].imageFilePath;
                if (strcasestr(exec_path.c_str(), "rosetta") != nullptr)
                    continue;

                // In case of a translated process (Rosetta maybe ?), the executable path is not the first entry.
                size_t pos;
                while ((pos = exec_path.find("/./")) != std::string::npos)
                {
                    exec_path.replace(pos, 3, "/");
                }
                break;
            }
        }
        else
        {
            for (int i = 0; i < dyld_img_infos->infoArrayCount; ++i)
            {
                // For now I don't know how to be sure to get the executable path
                // but looks like the 1st entry is the executable path
                exec_path = dyld_img_infos->infoArray[i].imageFilePath;
                size_t pos;
                while ((pos = exec_path.find("/./")) != std::string::npos)
                {
                    exec_path.replace(pos, 3, "/");
                }
                break;
            }
        }
    }

    return exec_path;
}

static void DumpMethods(Class cls)
{
    for (Class current = cls;
         current != Nil;
         current = class_getSuperclass(current))
    {
        NSLog(@"=== METHODS %s ===", class_getName(current));

        unsigned int count = 0;
        Method *methods = class_copyMethodList(current, &count);

        for (unsigned int i = 0; i < count; ++i)
        {
            Method method = methods[i];

            SEL selector = method_getName(method);
            IMP imp = method_getImplementation(method);
            const char *types = method_getTypeEncoding(method);

            NSLog(@"  -[%s %s] IMP=%p types=%s",
                  class_getName(current),
                  sel_getName(selector),
                  imp,
                  types);
        }

        free(methods);
    }
}

static void DumpIvars(Class cls)
{
    for (Class current = cls;
         current != Nil;
         current = class_getSuperclass(current))
    {
        NSLog(@"=== IVARS %s ===", class_getName(current));

        unsigned int count = 0;
        Ivar *ivars = class_copyIvarList(current, &count);

        for (unsigned int i = 0; i < count; ++i)
        {
            Ivar ivar = ivars[i];

            NSLog(@"  %s : %s (offset=%td)",
                  ivar_getName(ivar),
                  ivar_getTypeEncoding(ivar),
                  ivar_getOffset(ivar));
        }

        free(ivars);
    }
}

static void DumpBaseObjectChain(id object)
{
    if (object == nil)
    {
        NSLog(@"<nil>");
        return;
    }

    id current = object;
    NSUInteger depth = 0;

    while (current != nil)
    {
        Class cls = object_getClass(current);

        NSLog(@"[%lu] object=%p class=%s",
              (unsigned long)depth,
              current,
              class_getName(cls));

        Ivar baseObjectIvar = class_getInstanceVariable(cls, "_baseObject");

        if (baseObjectIvar == NULL)
        {
            NSLog(@"[%lu] no _baseObject", (unsigned long)depth);
            break;
        }

        id next = object_getIvar(current, baseObjectIvar);

        if (next == nil)
        {
            NSLog(@"[%lu] _baseObject = nil",
                  (unsigned long)depth);
            break;
        }

        if (next == current)
        {
            NSLog(@"[%lu] _baseObject points to itself",
                  (unsigned long)depth);
            break;
        }

        current = next;
        ++depth;

        // Sécurité contre une chaîne anormalement longue.
        if (depth > 64)
        {
            NSLog(@"_baseObject chain too long");
            break;
        }
    }
}

//-----------------------------------------------------------------------------------
// AppViewController
//-----------------------------------------------------------------------------------

@implementation AppViewController

-(instancetype)initWithNibName:(nullable NSString *)nibNameOrNil bundle:(nullable NSBundle *)nibBundleOrNil
{
    self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];

    LoadTestOptions();

    _device = MTLCreateSystemDefaultDevice();
    _commandQueue = [_device newCommandQueue];

    if (!self.device)
    {
        NSLog(@"Metal is not supported");
        abort();
    }
    
    // Setup Dear ImGui context
    // FIXME: This example doesn't have proper cleanup...
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Renderer backend
    ImGui_ImplMetal_Init(_device);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    std::string exec_path = getExecutablePath();
    exec_path = exec_path.substr(0, exec_path.rfind("/") + 1) + ingame_overlay_test_library;
    void* overlay_hook = dlopen(exec_path.c_str(), RTLD_NOW);
    printf("Loading %s: %p\n", exec_path.c_str(), overlay_hook);
    
    return self;
}

-(MTKView *)mtkView
{
    return (MTKView *)self.view;
}

-(void)loadView
{
    self.view = [[MTKView alloc] initWithFrame:CGRectMake(0, 0, 1200, 800)];
}

-(void)viewDidLoad
{
    [super viewDidLoad];

    self.mtkView.device = self.device;
    self.mtkView.delegate = self;

    if (g_Test.DrawableWidth > 0 && g_Test.DrawableHeight > 0)
    {
        self.mtkView.autoResizeDrawable = NO;
        self.mtkView.drawableSize = CGSizeMake(g_Test.DrawableWidth, g_Test.DrawableHeight);
    }
    if (g_Test.SampleCount > 1)
        self.mtkView.sampleCount = g_Test.SampleCount;
    if (g_Test.FramebufferOnly >= 0)
        self.mtkView.framebufferOnly = g_Test.FramebufferOnly != 0;

    if (g_Test.RenderThread)
    {
        self.mtkView.paused = YES;
        self.mtkView.enableSetNeedsDisplay = NO;

        // Present from the layer: MTKView keeps its current drawable until -draw, which runs on the main thread.
        CAMetalLayer* layer = (CAMetalLayer*)self.mtkView.layer;
        [NSThread detachNewThreadWithBlock:^{
            for (;;)
            {
                @autoreleasepool
                {
                    [self renderThreadFrame:layer];
                }
            }
        }];
    }

    if (g_Test.EventStorm)
        [self startEventStorm];

#if TARGET_OS_OSX
    ImGui_ImplOSX_Init(self.view);
    [NSApp activateIgnoringOtherApps:YES];
#endif
}

-(void)drawInMTKView:(MTKView*)view
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize.x = view.bounds.size.width;
    io.DisplaySize.y = view.bounds.size.height;

#if TARGET_OS_OSX
    CGFloat framebufferScale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
#else
    CGFloat framebufferScale = view.window.screen.scale ?: UIScreen.mainScreen.scale;
#endif
    io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);

    id<MTLCommandBuffer> commandBuffer = NewTestCommandBuffer(self.commandQueue);
    if (g_Test.OffscreenFirst)
        EncodeOffscreenPass(commandBuffer);

    MTLRenderPassDescriptor* renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor == nil)
    {
        [commandBuffer commit];
        return;
    }
    
    // Start the Dear ImGui frame
    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
#if TARGET_OS_OSX
    ImGui_ImplOSX_NewFrame(view);
#endif
    // The drawable may not follow the view (METAL_TEST_DRAWABLE_SIZE): after the OSX backend, which sets the screen's scale.
    if (view.bounds.size.width > 0 && view.bounds.size.height > 0)
        io.DisplayFramebufferScale = ImVec2(view.drawableSize.width / view.bounds.size.width, view.drawableSize.height / view.bounds.size.height);
    ImGui::NewFrame();

    // Our state (make them static = more or less global) as a convenience to keep the example terse.
    static bool show_demo_window = true;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

    // Rendering
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();

    renderPassDescriptor.colorAttachments[0].clearColor = MTLClearColorMake(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    id <MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [renderEncoder pushDebugGroup:@"Dear ImGui rendering"];
    ImGui_ImplMetal_RenderDrawData(draw_data, commandBuffer, renderEncoder);

    //DumpBaseObjectChain(commandBuffer);
    //DumpBaseObjectChain(renderEncoder);
    
    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];

    // Present
    PresentAndCommit(commandBuffer, view.currentDrawable);
}

// METAL_TEST_RENDER_THREAD: a clear and a present, on a secondary thread.
-(void)renderThreadFrame:(CAMetalLayer*)layer
{
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (drawable == nil)
    {
        usleep(1000);
        return;
    }

    id<MTLCommandBuffer> commandBuffer = NewTestCommandBuffer(self.commandQueue);
    if (g_Test.OffscreenFirst)
        EncodeOffscreenPass(commandBuffer);

    MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    renderPass.colorAttachments[0].texture = drawable.texture;
    renderPass.colorAttachments[0].loadAction = MTLLoadActionClear;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
    renderPass.colorAttachments[0].clearColor = MTLClearColorMake(0.45, 0.55, 0.60, 1.0);
    [[commandBuffer renderCommandEncoderWithDescriptor:renderPass] endEncoding];

    PresentAndCommit(commandBuffer, drawable);
}

// METAL_TEST_EVENT_STORM: mouse moves and wheel events over the view, posted to the application's queue so
// that its local event monitors (the overlay's one included) see them.
-(void)startEventStorm
{
    [NSTimer scheduledTimerWithTimeInterval:0.002 repeats:YES block:^(NSTimer*) {
        NSWindow* window = self.view.window;
        if (window == nil)
            return;

        const NSRect bounds = self.view.bounds;
        for (int i = 0; i < 4; ++i)
        {
            const NSPoint location = NSMakePoint(bounds.size.width * (arc4random_uniform(1000) / 1000.0), bounds.size.height * (arc4random_uniform(1000) / 1000.0));
            NSEvent* move = [NSEvent mouseEventWithType:NSEventTypeMouseMoved location:location modifierFlags:0 timestamp:[NSProcessInfo processInfo].systemUptime
                                           windowNumber:window.windowNumber context:nil eventNumber:0 clickCount:0 pressure:0];
            [NSApp postEvent:move atStart:NO];
        }

        CGEventRef wheel = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, (int32_t)arc4random_uniform(3) - 1);
        if (wheel != nullptr)
        {
            NSEvent* scroll = [NSEvent eventWithCGEvent:wheel];
            if (scroll != nil)
                [NSApp postEvent:scroll atStart:NO];
            CFRelease(wheel);
        }
    }];
}

-(void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size
{
}

//-----------------------------------------------------------------------------------
// Input processing
//-----------------------------------------------------------------------------------

- (void)viewWillAppear
{
    [super viewWillAppear];
    self.view.window.delegate = self;
}

- (void)windowWillClose:(NSNotification *)notification
{
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    ImGui::DestroyContext();
}

@end

//-----------------------------------------------------------------------------------
// AppDelegate
//-----------------------------------------------------------------------------------

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) NSWindow *window;
@end

@implementation AppDelegate

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return YES;
}

-(instancetype)init
{
    if (self = [super init])
    {
        NSViewController *rootViewController = [[AppViewController alloc] initWithNibName:nil bundle:nil];
        self.window = [[NSWindow alloc] initWithContentRect:NSZeroRect
                                                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        self.window.contentViewController = rootViewController;
        [self.window center];
        [self.window makeKeyAndOrderFront:self];
    }
    return self;
}

@end

//-----------------------------------------------------------------------------------
// Application main() function
//-----------------------------------------------------------------------------------

int main(int, const char**)
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        AppDelegate *appDelegate = [[AppDelegate alloc] init];   // creates window
        [NSApp setDelegate:appDelegate];

        [NSApp activateIgnoringOtherApps:YES];
        [NSApp run];
    }
    return 0;
}
