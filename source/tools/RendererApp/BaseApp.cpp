#include "BaseApp.h"
#include <Platform.h>

WS_PRAGMA_DIAGNOSTIC_PUSH()
// Caused by winnt.h because SDL2 changes packing, see https://bugzilla.libsdl.org/show_bug.cgi?id=4159
WS_PRAGMA_DIAGNOSTIC_IGNORED_MSC(4121)  // warning C4121:
                                        // 'JOBOBJECT_IO_RATE_CONTROL_INFORMATION_NATIVE_V2':
                                        // alignment of a member was sensitive to packing
#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/platform.h>
WS_PRAGMA_DIAGNOSTIC_POP()

// List of stupid defines from Xlib.h, included by SDL_syswm
#undef False
#undef True
#undef None
#undef Status
#include <thread>

int BaseApp::init()
{
    stopRunning = false;

    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error initializing SDL: %s\n", SDL_GetError());
        return -1;
    }

    // Create the main window
    mainWindow = SDL_CreateWindow(
        // clang-format off
        "Main window",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        windowWidth, windowHeight,
        SDL_WINDOW_SHOWN
        // clang-format on
        );
    if (!mainWindow) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Error creating window: %s\n", SDL_GetError());
        return -1;
    }

    // Initialize the renderer
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(mainWindow, &wmi)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Couldn't get window information: %s", SDL_GetError());
        return -1;
    }

    bgfx::PlatformData pd;
    switch (wmi.subsystem)
    {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    case SDL_SYSWM_WINDOWS:
        pd.ndt = nullptr;
        pd.nwh = wmi.info.win.window;
        break;
#endif
#if defined(SDL_VIDEO_DRIVER_X11)
    case SDL_SYSWM_X11:
        pd.ndt = wmi.info.x11.display;
        pd.nwh = (void*)(uintptr_t)wmi.info.x11.window;
        break;
#endif
#if defined(SDL_VIDEO_DRIVER_COCOA)
    case SDL_SYSWM_COCOA:
        pd.ndt = nullptr;
        pd.nwh = wmi.info.cocoa.window;
        break;
#endif
#if defined(SDL_VIDEO_DRIVER_VIVANTE)
    case SDL_SYSWM_VIVANTE:
        pd.ndt = wmi.info.vivante.display;
        pd.nwh = wmi.info.vivante.window;
        break;
#endif
    default: SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Unsupported window system");
    }

    for (int sysCursorIndex = 0; sysCursorIndex < SDL_NUM_SYSTEM_CURSORS; sysCursorIndex++)
    {
        systemCursors[sysCursorIndex] = SDL_CreateSystemCursor(SDL_SystemCursor(sysCursorIndex));
    }

    // Let bgfx create the rendering context and back buffers
    pd.context      = nullptr;
    pd.backBuffer   = nullptr;
    pd.backBufferDS = nullptr;
    bgfx::setPlatformData(pd);

    // Call this before init to tell bgfx this is the render thread
    bgfx::renderFrame();
    return 0;
}

bool BaseApp::initAppThread()
{
    if (!bgfx::init()) return false;
    bgfx::reset(uint32_t(windowWidth), uint32_t(windowHeight), 0 | BGFX_RESET_VSYNC);
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    // Set view 0 clear state.
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);

    // Clear the window
    bgfx::frame();
    return true;
}

void BaseApp::shutdownAppThread() { bgfx::shutdown(); }

void BaseApp::shutdown()
{
    for (auto& systemCursorPtr : systemCursors)
    {
        SDL_FreeCursor(systemCursorPtr);
        systemCursorPtr = nullptr;
    }

    SDL_DestroyWindow(mainWindow);
    mainWindow = nullptr;

    SDL_Quit();
}

void BaseApp::executeLoopOnce()
{
    {
        InputsManager::ScopedTransfer transfer{inputsManager};

        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            transfer.PushEvent(event);
        }

        {
            Inputs::MouseState mouseState;
            mouseState.buttonsMask = SDL_GetMouseState(&mouseState.x, &mouseState.y);
            if (!(SDL_GetWindowFlags(mainWindow) & SDL_WINDOW_MOUSE_FOCUS)) {
                mouseState.x = mouseState.y = std::numeric_limits<int>::lowest();
            }
            transfer.PushMouseState(mouseState);
        }

        // Done transferring events
    }

    bgfx::RenderFrame::Enum ret = bgfx::renderFrame();
    if (ret == bgfx::RenderFrame::Exiting) requireExit();
}

void BaseApp::executeAppLoopOnce()
{
    static bool  debugDisplay = false;
    const Inputs inputs       = inputsManager.receiveAndProcessEvents();
    for (const SDL_Event& event : inputs.events)
    {
        // Default handling of some events
        switch (event.type)
        {
        case SDL_QUIT: requireExit(); break;
        case SDL_KEYUP:
            if (event.key.keysym.sym == SDLK_F1) {
                debugDisplay = !debugDisplay;
            }
            break;
        default: break;
        }
    }
    bgfx::setDebug(BGFX_DEBUG_TEXT | (debugDisplay ? BGFX_DEBUG_STATS : 0));
    // Set view 0 default viewport.
    bgfx::setViewRect(0, 0, 0, uint16_t(windowWidth), uint16_t(windowHeight));

    // This dummy draw call is here to make sure that view 0 is cleared
    // if no other draw calls are submitted to view 0.
    bgfx::touch(0);

    // Advance to next frame. Rendering thread will be kicked to
    // process submitted rendering primitives.
    bgfx::frame();
}

void BaseApp::run()
{
    std::thread appThread{&BaseApp::runAppThread, this};
    while (!stopRunning)
    {
        executeLoopOnce();
    }
    // Wait for destruction of the context
    while (bgfx::RenderFrame::NoContext != bgfx::renderFrame())
    {
    };
    appThread.join();
}

void BaseApp::runAppThread()
{
    if (!initAppThread()) {
        requireExit();
        shutdownAppThread();
        return;
    }
    while (!stopRunning)
    {
        executeAppLoopOnce();
    }
    shutdownAppThread();
    // End of the app thread
}
