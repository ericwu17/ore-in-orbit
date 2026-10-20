# Ore in Orbit

A new game, in early development. A fixed-timestep simulation driven from
an SDL2 + Dear ImGui (docking) + ImPlot frame loop, with a themed UI. So far
there is a main menu (Play, Credits); Play starts a new game (held in
memory only): a ship built on a 128x128 square grid, starting as a small
patch in its middle. Hull tiles go down first, and
every building (hydrogen and asteroid collectors, pipes, transport
belts) needs a hull tile under it. Everything placed comes out of the
bridge's inventory, and removing it gives it back (with anything stored
inside it). Collectors gather at a set rate into an internal buffer (asteroid
chunks, or hydrogen) and stall when it is full.

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
- **Left click / drag**: build the selected item on the grid.
- **Right click / drag**: remove buildings and refund them; a second press
  removes the bare hull underneath.
- **Mouse wheel**: zoom the grid (about the cursor).
- **Middle drag, or Space + left drag**: pan the grid.
- **1–9**: pick the item to build. **R**: rotate the transport belt.
- **F1 / F2**: the Dear ImGui / ImPlot demo windows (widget reference).

## Layout

One directory per layer under `src/`; includes name their layer
(`#include "sim/world.h"`).

- `src/sim/` — pure simulation, no SDL/imgui/OpenGL, so the tests can link
  it headless.
  - `game_types.h` — `Tick` and the tick rate (60 per real second),
    `GridPos`, `GridSize`, `Direction`, and `ItemKind` (what the inventory holds).
  - `building.h/.cpp` — the `Building` class hierarchy (`Bridge`,
    `Collector` → hydrogen / asteroid, `Pipe`, `TransportBelt`),
    each kind's footprint (`footprint_of`, e.g. 2x2 collectors), the
    collectors' rates and buffers (`CollectorSpec`), the `make_building`
    factory and `Inventory` (the bridge's, and building buffers).
  - `ship.h/.cpp` — `Ship`: the build grid (hull, and buildings stored at
    their top-left cell), placement / removal rules and the starting ship.
  - `world.h/.cpp` — `World`: owns all state (the ship), `advance_tick()`
    steps it.
- `src/ui/` — the only code that knows Dear ImGui exists.
  - `ui.h` — the `Screen` enum and the per-screen draw functions
    `main.cpp` calls each frame.
  - `ui.cpp` — the main menu, credits and game screens (the build grid and
    palette, with its zoom / pan camera), and the F1 / F2 demo windows.
  - `ui_theme.h/.cpp` — the palette, ImGui / ImPlot styles, the embedded
    font and a few styled widgets. Every color comes from here.
- `src/app/` — `main.cpp` (window, GL context, screen switching,
  fixed-timestep loop), the
  embedded app icon, and the macOS-only startup tweak.
