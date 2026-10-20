# Sim Template

A starting point for small simulation games: a fixed-timestep simulation
driven from an SDL2 + Dear ImGui (docking) + ImPlot frame loop, with a
Factorio-style statistics framework and a themed UI. Stripped down from
[Letter Flow](../letter-flow); the simulation in `src/sim/world.*` is a
placeholder (items arrive in a queue at random and are processed one at a
time) so there is something to chart. Replace it with your own.

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
make && ./sim-template
```

Linux:

```sh
sudo apt install build-essential libsdl2-dev libgl-dev   # (or your distro's equivalent)
git submodule update --init   # Dear ImGui, ImPlot
nix-shell
make && ./sim-template
```

Windows (MSYS2, from the "MSYS2 MinGW 64-bit" shell — not plain cmd):

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 mingw-w64-x86_64-make
git submodule update --init   # Dear ImGui, ImPlot
mingw32-make && ./sim-template.exe
```

## Controls

- **Shift**: play / pause. **,** / **.**: slow down / speed up (1x to 32x).
  The transport bar at the top does the same with the mouse.
- **L** (or the HUD button): the statistics window.
- **F1 / F2**: the Dear ImGui / ImPlot demo windows (widget reference).

## Layout

One directory per layer under `src/`; includes name their layer
(`#include "sim/world.h"`).

- `src/sim/` — pure simulation, no SDL/imgui/OpenGL, so the tests can link
  it headless.
  - `game_types.h` — `Tick` and the in-game clock (60 ticks per real
    second, a 3-minute day) with conversion helpers.
  - `stats.h/.cpp` — `StatSeries`: one metric at three time resolutions
    (12-minute, 2-hour and 6-hour buckets, 120 of each), as fixed ring
    buffers, so memory stays bounded however long the game runs. Metrics are
    `Flow`s (summed per bucket) or `Gauge`s (per-bucket peak). A metric
    catalogue is an enum + a `StatInfo` array + a `stat_infos` overload;
    `StatTable<Metric>` holds one series per metric.
  - `world.h/.cpp` — `World`: owns all state, `advance_tick()` steps it.
    Placeholder.
- `src/ui/` — the only code that knows Dear ImGui exists.
  - `ui.h` — the public entry points `main.cpp` calls each frame.
  - `ui_internal.h` — UI state shared between the `ui_*.cpp` files
    (`ui::g_game`, reset in one go by `ResetGameUi`).
  - `ui.cpp` — time formatting and the dockspace (a left panel plus a
    passthrough central area).
  - `ui_hud.cpp` — the transport bar and the HUD card.
  - `ui_inspector.cpp` — the docked left panel. Placeholder.
  - `ui_stats.cpp` — the statistics window and its building blocks: time
    range buttons, `BeginTimeChart` / `PlotRate` / `PlotLevel` /
    `PlotAverage` / `PlotTickMarkers` / `EndTimeChart` (with a hover readout),
    `Sparkline`, `SortRows`, and summary tiles.
  - `ui_theme.h/.cpp` — the palette, ImGui / ImPlot styles, the embedded
    font and a few styled widgets. Every color comes from here.
- `src/app/` — `main.cpp` (window, GL context, fixed-timestep loop), the
  embedded app icon, and the macOS-only startup tweak.
- `tests/` — headless, assert-based tests (`test_stats.cpp`).

## Adding a statistic

1. Add a value to `GlobalStat` (before `Count`) and its `{key, kind}` entry
   to `kGlobalStatInfo` in `src/sim/stats.h`.
2. Record it from the simulation: `stats[GlobalStat::X].add(now, n)` for a
   flow, `.sample(now, value)` every tick for a gauge.
3. Chart it in `ui_stats.cpp`: `PlotRate` for flows, `PlotLevel` for gauges,
   between `BeginTimeChart` and `EndTimeChart`. The Overview tab's metrics
   table picks it up automatically.

For per-entity stats, add another metric enum with its own catalogue and
`stat_infos` overload, and give each entity a `StatTable<ThatEnum>`.
