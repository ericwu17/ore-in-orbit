# Ore in Orbit

A new game, in early development. A fixed-timestep simulation driven from
an SDL2 + Dear ImGui (docking) + ImPlot frame loop, with a themed UI. So far
there is a main menu (Play, Credits); Play opens the list of saved games,
to start a new one or carry on with one. A game is a ship built on a
128x128 square grid, starting as a small patch in its middle. Hull tiles go down first, and
every building (hydrogen and asteroid collectors, pipes, transport
belts) needs a hull tile under it. Everything placed comes out of the
bridge's inventory, and removing it gives it back (with anything stored
inside it). Collectors gather at a set rate into an internal buffer (asteroid
chunks, or hydrogen) and stall when it is full. As in Mindustry there are no
inserters: buildings put their output straight onto neighboring belts (or
into neighboring buildings that take it), and belts carry items into
whatever they point at, the bridge included. Machines craft the recipe the
player picks for them (click one on the grid). Filter sorters route items
by kind, as in Mindustry: the one picked for it goes straight through, the
rest turn left or right. Pipes don't carry fluid yet.

C++20, SDL2, Dear ImGui (OpenGL3), ImPlot, and nlohmann::json (vendored in
`third_party/nlohmann/`, for save files). Runs on macOS, Linux and Windows.

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

## Saves

Every game is saved in its own file, in the `saves/` folder of the game's
per-user data directory:

- macOS: `~/Library/Application Support/OreInOrbit/OreInOrbit/saves/`
- Linux: `~/.local/share/OreInOrbit/OreInOrbit/saves/`
- Windows: `%APPDATA%\OreInOrbit\OreInOrbit\saves\`

The game saves when you leave it for the menu, when you close the window,
and every minute while you play. The save list (Play) loads a save (Load,
or double-click it), renames it (the pencil), copies it, or deletes it
(after asking). Saves are JSON, so you can read them; one the game can't
read stays in the list, greyed out, with the reason in its tooltip.

## Controls

- **Escape**: back to the main menu (from the game, the save list or the
  credits), saving the game.
- **Left click / drag**: build the selected item on the grid.
- **Left click a building**: inspect it; pick a machine's recipe or a
  filter sorter's item.
- **Right click / drag**: remove buildings and refund them; a second press
  removes the bare hull underneath.
- **Mouse wheel**: zoom the grid (about the cursor).
- **Middle drag, or hold Space and move the mouse**: pan the grid.
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
    collectors' rates and buffers (`CollectorSpec`), belts (`Conveyor`),
    machines and their recipes, the `make_building` factory and
    `Inventory` (the bridge's, and building buffers).
  - `ship.h/.cpp` — `Ship`: the build grid (hull, and buildings stored at
    their top-left cell), placement / removal rules, the starting ship, and
    moving items between neighboring buildings each tick.
  - `world.h/.cpp` — `World`: owns all state (the ship), `advance_tick()`
    steps it.
- `src/persistence/` — save files, also headless (the tests link it). The
  simulation knows nothing about saving beyond a `friend struct SaveAccess`
  in each class with private state to save.
  - `save_file.h/.cpp` — the save format, a JSON document: a header (name,
    when, play time) and the whole `World`, with items, directions, fluids
    and recipes written by name, and loading that checks everything (a bad
    save is refused with a reason, never trusted).
  - `save_slots.h/.cpp` — the saves folder: listing, writing (atomically,
    via a temporary file), loading, renaming, copying and deleting saves.
- `src/ui/` — the only code that knows Dear ImGui exists.
  - `ui.h` — the `Screen` enum and the per-screen draw functions
    `main.cpp` calls each frame.
  - `ui.cpp` — the main menu, save list, credits and game screens (the build grid and
    palette, with its zoom / pan camera, and the inspector), and the F1 / F2
    demo windows.
  - `ui_theme.h/.cpp` — the palette, ImGui / ImPlot styles, the embedded
    font and a few styled widgets. Every color comes from here.
- `src/app/` — `main.cpp` (window, GL context, screen switching,
  fixed-timestep loop, the game being played and when it is saved), the
  embedded app icon, and the macOS-only startup tweak.
