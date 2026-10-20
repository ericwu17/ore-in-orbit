// main.cpp — application bootstrap and frame loop (SDL2 + OpenGL3).
//
// Structure copied from imgui/examples/example_sdl2_opengl3, with the
// simulation and UI calls plugged into the frame loop:
//   1. poll events          (SDL -> ImGui backends)
//   2. advance simulation   (while playing; fixed timestep: exactly
//                            kTicksPerSecond per second)
//   3. build UI             (immediate mode: re-describe every frame)
//   4. render               (ImGui draw data -> OpenGL)
//
// This file is shared by all platforms (macOS, Linux, Windows). The only
// platform-specific piece is the macOS key press-and-hold tweak, which lives
// in platform_macos.mm behind ApplyMacOSTweaks().

#ifdef __APPLE__
#include "app/platform_macos.h"
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#include "app/app_icon.h"
#include "sim/game_types.h"
#include "sim/world.h"
#include "ui/ui.h"
#include "ui/ui_theme.h"

#include <cstdio>
#include <string>

#include <SDL.h>
#include <SDL_opengl.h>

#ifdef _WIN32
#include <windows.h>  // SetProcessDPIAware()
#endif

// Advance the simulation at a FIXED timestep: the game ticks 60 times per
// second no matter what the display refresh rate is (60/120/144 Hz all work).
// Real time since the last frame goes into an accumulator; whole ticks are
// drained from it.
static void step_world(World& world, float delta_time, float& tick_accumulator)
{
    constexpr float kSecondsPerTick = 1.0f / (float)kTicksPerSecond;
    tick_accumulator += delta_time;
    // After a long hitch, don't try to catch up forever.
    if (tick_accumulator > 0.25f)
        tick_accumulator = 0.25f;
    while (tick_accumulator >= kSecondsPerTick)
    {
        world.advance_tick();
        tick_accumulator -= kSecondsPerTick;
    }
}

// The app takes no command-line arguments, but main keeps this signature:
// on Windows SDL_main.h renames main to SDL_main(int, char*[]), which
// SDL2main calls, so a parameterless main would not build there.
int main(int, char**)
{
    // Setup SDL
#ifdef _WIN32
    ::SetProcessDPIAware();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }
#ifdef SDL_HINT_IME_SHOW_UI
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#endif

    // Select GL version + let the backend select a GLSL version
#if defined(__APPLE__)
    // GL 3.2 Core + GLSL 150
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);  // always required on Mac
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    // GL 3.0 + GLSL 130
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    float main_scale = ImGui_ImplSDL2_GetContentScaleForDisplay(0);
    const Uint32 window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    SDL_Window* window = SDL_CreateWindow("Ore in Orbit", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (int)(1280 * main_scale), (int)(800 * main_scale), window_flags);
    if (window == nullptr)
    {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }
    SetAppIcon(window);

#ifdef __APPLE__
    ApplyMacOSTweaks();  // disable the press-and-hold accent popup
#endif

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (gl_context == nullptr)
    {
        printf("Error: SDL_GL_CreateContext(): %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1);  // enable vsync

    // Persistence: the window layout (imgui.ini) lives in SDL's per-user
    // app-data directory (SDL_GetPrefPath takes an org and an app name —
    // created if needed), which is also the place for save files:
    //   macOS:   ~/Library/Application Support/OreInOrbit/OreInOrbit/
    //   Linux:   ~/.local/share/OreInOrbit/OreInOrbit/
    //   Windows: %APPDATA%\OreInOrbit\OreInOrbit
    // ImGui's default IniFilename is relative to the working directory, which we don't want.
    char* pref_path = SDL_GetPrefPath("OreInOrbit", "OreInOrbit");
    const std::string pref_dir = pref_path ? pref_path : "";
    SDL_free(pref_path);
    const std::string ini_path = pref_dir + "imgui.ini";  // must outlive the ImGui context

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = ini_path.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup the font, Dear ImGui / ImPlot styles and DPI scaling (ui_theme.h)
    ui::theme::ApplyTheme(main_scale);

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(nullptr);

    // Our state: which screen is up, and the whole simulation, which the
    // frame loop below steps forward at a fixed tick rate while playing.
    // Pressing Play from the menu starts a fresh World.
    Screen screen = Screen::MainMenu;
    World world;
    float tick_accumulator = 0.0f;
    const ImVec4 clear_color = ui::theme::kMap;  // the parchment background behind every screen

    // Main loop
    bool done = false;
    while (!done)
    {
        // 1. Poll and handle events (inputs, window resize, etc.)
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // 2. Advance the simulation (fixed timestep, see step_world).
        if (screen == Screen::Playing)
            step_world(world, io.DeltaTime, tick_accumulator);

        // 3. Build this frame's UI: the current screen, then the demo windows
        //    on top.
        const Screen previous_screen = screen;
        switch (screen)
        {
        case Screen::MainMenu: DrawMainMenu(screen); break;
        case Screen::Playing:  DrawPlaying(screen); break;
        case Screen::Credits:  DrawCredits(screen); break;
        }
        DrawDevWindows();
        if (screen == Screen::Playing && previous_screen == Screen::MainMenu)
        {
            world = World{};
            tick_accumulator = 0.0f;
        }

        // 4. Rendering
        ImGui::Render();
        int display_w, display_h;
        SDL_GetWindowSizeInPixels(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
