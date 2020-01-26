// dear imgui: standalone example application for OSX + OpenGL2, using legacy fixed pipeline
// If you are new to dear imgui, see examples/README.txt and documentation at the top of imgui.cpp.

#include "imgui.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_opengl2.h"
#include "netclient/netclient.h"
#include <stdio.h>
#include <string>
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/glu.h>

//-----------------------------------------------------------------------------------
// ImGuiExampleView
//-----------------------------------------------------------------------------------

@interface ImGuiExampleView : NSOpenGLView
{
    struct nc_client* netClient;
    NSTimer* animationTimer;
    int serverPort;
}
@end

@implementation ImGuiExampleView

- (instancetype)initWithFrame:(NSRect)frameRect 
                pixelFormat:(NSOpenGLPixelFormat *)format
                serverPort:(int)port
{
    serverPort = port;
    return [self initWithFrame:frameRect pixelFormat:format];
}

-(void)animationTimerFired:(NSTimer*)timer
{
    [self setNeedsDisplay:YES];
}

struct stateInfo {
    int32_t state = 0;
    int16_t last_acked = 0;
    int16_t last_nacked = 0;
    std::string acks;
};

static stateInfo netClientState;

static void state_update(int32_t state, void* user_data)
{
    stateInfo* info = (stateInfo*)user_data;
    info->state = state;
    info->acks.clear();
}

static void packet_cb(uint16_t id, int32_t res, void* user_data)
{
    char buff[128];
    sprintf(buff, "%c%d", res == 0 ? ' ' : '~', id);

    stateInfo* info = (stateInfo*)user_data;
    if(res == 0) {
        info->last_acked = id;
    } else {
        info->last_nacked = id;
    }

    info->acks += buff;
    size_t limit = 150;
    if(info->acks.size() > limit) {
        info->acks = info->acks.substr(info->acks.size() - limit);
    }
}

static int send_cb(uint16_t id, void* buffer, uint32_t nbytes) {
    return 0;
} 

static int recv_cb(uint16_t id, const void* buffer, uint32_t nbytes) {
    return 0;
}

-(void)prepareOpenGL
{
    [super prepareOpenGL];

#ifndef DEBUG
    GLint swapInterval = 1;
    [[self openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];
    if (swapInterval == 0)
        NSLog(@"Error: Cannot set swap interval.");
#endif

    nc_config config;
    netclient_make_default(&config);
    config.server_address = "127.0.0.1";
    config.server_port = serverPort;
    config.state_callback = state_update;
    config.packet_callback = packet_cb;
    config.send_callback = send_cb;
    config.recv_callback = recv_cb;
    config.user_data = &netClientState;
    netClient = netclient_create(&config);
}

-(void)updateAndDrawDemoView
{
    if(netClient) {
        netclient_update(netClient);
    }

    // Start the Dear ImGui frame
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplOSX_NewFrame(self);
    ImGui::NewFrame();

    // Global data for the demo
    static bool show_demo_window = false;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    static char g_hostname[128];
    strcpy(g_hostname, "127.0.0.1");
    static int g_port = serverPort;


    bool running = netClient != nullptr;
    ImGui::Begin("Net Client");
    ImGui::InputText("Host", g_hostname, IM_ARRAYSIZE(g_hostname));
    ImGui::InputInt("Port", &g_port, 0, 9000);
    ImGui::Text("Status: %s", running ? "Running" : "Stopped");
    ImGui::Text("State: %d", netClientState.state);
    ImGui::Text("Last Acked: %d", netClientState.last_acked);
    ImGui::Text("Last Nacked: %d", netClientState.last_nacked);
    ImGui::Text("Ack Log: %s", netClientState.acks.c_str());

    if(ImGui::Button(netClient ? "Stop" : "Start")){
        if(netClient) {
            netclient_destroy(netClient);
            netClient = nullptr;
        } else {
            nc_config config;
            netclient_make_default(&config);
            config.state_callback = state_update;
            config.packet_callback = packet_cb;
            config.send_callback = send_cb;
            config.recv_callback = recv_cb;
            config.user_data = &netClientState;
            config.server_address = g_hostname;
            config.server_port = g_port;
            netClient = netclient_create(&config);
        }
    }

    if(netClient && running) {
        nc_transport_info info;
        bool isConnected = 0 == netclient_transport_info(netClient, &info);

        if(isConnected && ImGui::Button("Disconnect")) {
            netclient_disconnect(netClient);
        }
        if(!isConnected && ImGui::Button("Connect")) {
            netclient_connect(netClient, g_hostname, g_port);
        }

        if(isConnected) {
            ImGui::Text("Last Recv: %d", info.last_received);
            ImGui::Text("Last Sent: %d", info.last_sent);
            ImGui::Text("Last Ackd: %d", info.last_acked);
            ImGui::Text("Last Ackd Bitmask: %d", info.last_acked_bitmask);
        }
    }
    ImGui::End();

	// Rendering
	ImGui::Render();
	[[self openGLContext] makeCurrentContext];

    ImDrawData* draw_data = ImGui::GetDrawData();
    GLsizei width  = (GLsizei)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    GLsizei height = (GLsizei)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    glViewport(0, 0, width, height);

	glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL2_RenderDrawData(draw_data);

    // Present
    [[self openGLContext] flushBuffer];

    if (!animationTimer)
        animationTimer = [NSTimer scheduledTimerWithTimeInterval:0.017 target:self selector:@selector(animationTimerFired:) userInfo:nil repeats:YES];
}

-(void)reshape
{
    [[self openGLContext] update];
    [self updateAndDrawDemoView];
}

-(void)drawRect:(NSRect)bounds
{
    [self updateAndDrawDemoView];
}

-(BOOL)acceptsFirstResponder
{
    return (YES);
}

-(BOOL)becomeFirstResponder
{
    return (YES);
}

-(BOOL)resignFirstResponder
{
    return (YES);
}

-(void)dealloc
{
    animationTimer = nil;
    if(netClient) {
        netclient_destroy(netClient);
    }
}

// Forward Mouse/Keyboard events to dear imgui OSX back-end. It returns true when imgui is expecting to use the event.
-(void)keyUp:(NSEvent *)event           { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)keyDown:(NSEvent *)event         { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)flagsChanged:(NSEvent *)event    { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseDown:(NSEvent *)event       { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseUp:(NSEvent *)event         { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseMoved:(NSEvent *)event      { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseDragged:(NSEvent *)event    { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)scrollWheel:(NSEvent *)event     { ImGui_ImplOSX_HandleEvent(event, self); }

@end

//-----------------------------------------------------------------------------------
// ImGuiExampleAppDelegate
//-----------------------------------------------------------------------------------

@interface ImGuiExampleAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, readonly) NSWindow* window;
@end

@implementation ImGuiExampleAppDelegate
@synthesize window = _window;

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
    return YES;
}

-(NSWindow*)window
{
    if (_window != nil)
        return (_window);

    NSRect viewRect = NSMakeRect(100.0, 100.0, 100.0 + 1280.0, 100 + 720.0);

    _window = [[NSWindow alloc] initWithContentRect:viewRect styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:YES];
    [_window setTitle:@"Dear ImGui OSX+OpenGL2 Example"];
    [_window setAcceptsMouseMovedEvents:YES];
    [_window setOpaque:YES];
    [_window makeKeyAndOrderFront:NSApp];

    return (_window);
}

-(void)setupMenu
{
	NSMenu* mainMenuBar = [[NSMenu alloc] init];
    NSMenu* appMenu;
    NSMenuItem* menuItem;

    appMenu = [[NSMenu alloc] initWithTitle:@"Dear ImGui OSX+OpenGL2 Example"];
    menuItem = [appMenu addItemWithTitle:@"Quit Dear ImGui OSX+OpenGL2 Example" action:@selector(terminate:) keyEquivalent:@"q"];
    [menuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];

    menuItem = [[NSMenuItem alloc] init];
    [menuItem setSubmenu:appMenu];

    [mainMenuBar addItem:menuItem];

    appMenu = nil;
    [NSApp setMainMenu:mainMenuBar];
}

-(void)dealloc
{
    _window = nil;
}

-(void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	// Make the application a foreground application (else it won't receive keyboard events)
	ProcessSerialNumber psn = {0, kCurrentProcess};
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);

	// Menu
    [self setupMenu];

    int dst_port = [[[[NSProcessInfo processInfo] arguments] objectAtIndex:1] intValue];

    NSOpenGLPixelFormatAttribute attrs[] =
    {
        NSOpenGLPFADoubleBuffer,
        NSOpenGLPFADepthSize, 32,
        0
    };

    NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    ImGuiExampleView* view = [[ImGuiExampleView alloc] initWithFrame:self.window.frame pixelFormat:format serverPort:dst_port];
    format = nil;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_6)
        [view setWantsBestResolutionOpenGLSurface:YES];
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    [self.window setContentView:view];

    if ([view openGLContext] == nil)
        NSLog(@"No OpenGL Context!");

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplOSX_Init();
    ImGui_ImplOpenGL2_Init();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);
}

@end

int main(int argc, const char* argv[])
{
    if(argc < 2) 
        return -1;

	@autoreleasepool
	{
		NSApp = [NSApplication sharedApplication];
		ImGuiExampleAppDelegate* delegate = [[ImGuiExampleAppDelegate alloc] init];
		[[NSApplication sharedApplication] setDelegate:delegate];
		[NSApp run];
	}
	return NSApplicationMain(argc, argv);
}
