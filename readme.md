# Ore in Orbit

A new game, in early development. A fixed-timestep simulation driven from
an SDL2 + Dear ImGui (docking) + ImPlot frame loop, with a themed UI. So far
there is a main menu (Play, Credits); Play starts the simulation in
`src/sim/world.*`, which is still empty (it only counts ticks) and draws
nothing.

C++20, SDL2, Dear ImGui (OpenGL3), ImPlot. Runs on macOS, Linux and Windows.

## Build & run

The top-level `Makefile` detects the host OS and includes the matching
per-platform makefile (`Makefile.macos` / `Makefile.linux` /
`Makefile.windows`), so a bare `make` works everywhere. Those only set the
platform's compiler, libraries and extra sources; the source lists and every
rule live in `Makefile.common`. `make test` builds and runs the headless
tests (one binary per `tests/test_*.cpp`).

macOS:

```sh
brew install sdl2
git submodule update --init   # Dear ImGui, ImPlot
make && ./ore-in-orbit
```

Linux:

```sh
sudo apt install build-essential libsdl2-dev libgl-dev   # (or your distro's equivalent)
git submodule update --init   # Dear ImGui, ImPlot
nix-shell
make && ./ore-in-orbit
```

Windows (MSYS2, from the "MSYS2 MinGW 64-bit" shell — not plain cmd):

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 mingw-w64-x86_64-make
git submodule update --init   # Dear ImGui, ImPlot
mingw32-make && ./ore-in-orbit.exe
```

## Controls

- **Escape**: back to the main menu (from the game or the credits).
- **F1 / F2**: the Dear ImGui / ImPlot demo windows (widget reference).

## Layout

One directory per layer under `src/`; includes name their layer
(`#include "sim/world.h"`).

- `src/sim/` — pure simulation, no SDL/imgui/OpenGL, so the tests can link
  it headless.
  - `game_types.h` — `Tick` and the tick rate (60 per real second).
  - `world.h/.cpp` — `World`: owns all state, `advance_tick()` steps it.
- `src/ui/` — the only code that knows Dear ImGui exists.
  - `ui.h` — the `Screen` enum and the per-screen draw functions
    `main.cpp` calls each frame.
  - `ui.cpp` — the main menu, credits and (empty) game screens, and the
    F1 / F2 demo windows.
  - `ui_theme.h/.cpp` — the palette, ImGui / ImPlot styles, the embedded
    font and a few styled widgets. Every color comes from here.
- `src/app/` — `main.cpp` (window, GL context, screen switching,
  fixed-timestep loop), the
  embedded app icon, and the macOS-only startup tweak.
