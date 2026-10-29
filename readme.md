# Ore in Orbit

A new game, in early development. A fixed-timestep simulation driven from
an SDL2 + Dear ImGui (docking) + ImPlot frame loop, with a themed UI. So far
there is a main menu (Play, Credits); Play opens the list of saved games,
to start a new one or carry on with one.

A game is a ship built on a 128x128 square grid. Every ship has a layout,
which fixes its hull's shape and size, its 6x6 bridge and its ion
thrusters (engines), all centered on the grid; a game starts with a Skiff,
the smallest. Every building (asteroid collectors, transport belts,
machines) needs hull under it, and the hull never grows. Everything placed
comes out of the bridge's inventory, and removing it gives it back (with
anything stored inside it).

Each collector is aimed at one raw resource (iron ore, copper ore, silica
or carbon; the tractor array can also gather iridium ore) and gathers it
at a set rate into an internal buffer, stalling when it is full. As in
Mindustry there are no inserters: buildings put their output straight onto
neighboring belts (or into neighboring buildings that take it), and belts
carry items into whatever they point at, the bridge included. Machines
craft the recipe the player picks for them, and collectors gather what the
player aims them at (click one on the grid). The belt blocks work as in
Mindustry too: filter sorters route items by kind (the one picked for it
goes straight through, the rest turn left or right), and reverse sorters
the other way round (the one picked turns, the rest go straight); junctions
let two belt lines cross, each item leaving by the side opposite the one it
came in by; routers pass each item on to their other three sides in turn.

Progress comes in milestones, shown at the top of the right panel: deliver
the items each one asks for to the bridge (by belt; hand-crafting doesn't
count) to unlock the next tier of recipes. Locked recipes show greyed out,
marked with the milestone that unlocks them.

Not everything works yet: the belt tunnel moves nothing, and the power
buildings (solar panel, solar array, battery bank) are there for a power
system that doesn't exist yet. They can still be unlocked and built, and
their tooltips and the inspector say they do nothing (`is_unfinished` in
`src/sim/building.h` lists them).

The player commands a fleet. "Build a new ship...", in the right panel's
Fleet section, opens a window to pick a layout and build a ship in it from
the bridge of the ship on screen: it takes a ship starter pack (unlocked by
the "Deep space" milestone) and the layout's cost, in raw and processed
resources.
The layouts, smallest first: Skiff (from the start), Hauler (from "Deep
space"), Frigate ("Escape velocity") and Carrier ("Frontier"); bigger ones
have more engines. Every ship keeps running while
another is on screen; the tabs above the grid (or `[` and `]`) switch
between them, and double-clicking a tab renames that ship. Milestones are
the fleet's: a delivery to any bridge counts, and a recipe unlocked is
unlocked on every ship. "Send cargo..." moves items from one bridge's
inventory to another's, between ships in the same sector: each 50 items
use up a cargo pod, and they land 5 seconds later (cargo, like
hand-crafting, doesn't count toward a milestone).

Beyond home lies a star map of sectors (M, or the Map button above the
grid), a new one for each game. Each sector has its own yields of the raw
resources (as a % of home's: a collector aimed at something a sector lacks
stalls there) and a threat, which grows with the distance from home. A
sector a ship has been to is visited; its neighbors show on the map, and
their yields and threat once surveyed: visited, or next to a ship with a
survey scanner. Ships jump to a linked sector at once, when every ion
thruster their layout has is charged (10 fuel cells each, refined from
carbon and delivered by belt). The jump burns all their fuel.

Sectors with a threat are hostile: they send waves of enemies at every
ship in them, the first 30 seconds after it arrives, then one every 45
seconds, bigger each time and with heavier ships the higher the threat
(drones, gunships, dreadnoughts). Enemies come in from one side, fly at
the nearest building and shoot it; a building shot to pieces is gone, with
nothing refunded, except an engine, which is knocked offline (taking no
fuel, and keeping the ship from jumping) until it mends; and a ship whose
bridge falls is lost (with every ship,
the game: the last save stays as it was, to try again from). Turrets fight
back while they have ammunition, delivered by belt: the autocannon fires
slug magazines fast and short, the railgun (from the "Frontier" milestone)
rail slugs slow, far and hard. Once a sector has sent all its waves and
they're beaten, it's cleared, for good. Jumping away leaves the enemies
behind. Buildings mend slowly while no enemy is near.

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
make && ./ore-in-orbit
```

On NixOS, run `nix-shell` first instead of installing the packages:
`shell.nix` provides SDL2 and the GL / X11 / Wayland libraries SDL loads at
runtime.

Windows (MSYS2, from the "MSYS2 MinGW 64-bit" shell — not plain cmd):

```sh
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-SDL2 mingw-w64-x86_64-make
git submodule update --init   # Dear ImGui, ImPlot
mingw32-make && ./ore-in-orbit.exe
```

## Sprites

Every building and item is drawn from a PNG in `assets/sprites/`:

- `items/<ItemId>.png` — the icon of every item, buildings included
  (square), shown in the palette, cargo, crafting panels and pickers.
- `buildings/<ItemId>.png` — how each building looks built on the grid,
  plus `buildings/Bridge.png` and `buildings/Hull.png` (one cell of
  hull). Its shape is the building's footprint (2:3
  for the 2x3 ion thruster, 1:1 for a 2x2 collector), drawn facing
  **North**; the game turns belts to face the way they point.

`<ItemId>` is the item's enumerator in `src/sim/game_types.h`, as saves
write it (`IronPlate`, `AsteroidCollector`). Any resolution works as long
as the shape is right; the stand-ins are 64 px per cell.

The PNGs are embedded into the executable, like the logo and the font: to
try new art, replace the file and run `make` again (adding or deleting a
sprite also triggers a rebuild). A sprite that is missing or misshapen is
reported at startup and drawn as a plain stand-in shape, and `make test`
names it.

The current sprites are placeholders made by
`tools/make_placeholder_sprites.py` (Python 3 and Pillow). It only writes
the sprites that are missing, so it never overwrites real art;
`--force` regenerates them all.

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

## Blueprints

Copy part of the ship (Ctrl+C, or Cmd+C on macOS, then drag a box over it)
and paste it elsewhere (click; Ctrl/Cmd+V picks the last copy up again).
A copy holds the buildings in the box with their settings — machine
recipes, collector targets and sorter filters — but never the items in
them, nor the hull (the ship's layout fixes that). A building partly inside
the box comes whole; the bridge and the engines never come. R turns a copy
while pasting.

Pasting is all or nothing: the buildings go down only if every one fits,
on hull, and the bridge has everything it takes; otherwise nothing
changes, the pieces in the way show red, and the message says what's
missing. An identical building already in place counts as done and costs
nothing. A recipe not unlocked yet is left unset.

The Blueprints window (B) keeps copies by name, in a library every game
shares, in the `blueprints/` folder next to `saves/` (see Saves). Each
blueprint can be copied to the system clipboard as text — one line of JSON,
in the save format's style — to share, and "Import text" adds one from the
clipboard. A library file holds exactly that text.

## Controls

- **Escape**: back to the main menu (from the save list or the credits). In
  the game it first puts away the copy / paste tool, then closes what's
  open, one at a time (Send cargo, Build a new ship, the inspector, the keys
  overlay, the star map, the Blueprints window), and then opens the pause
  menu, whose Main menu saves the game.
- **Left click / drag**: build the selected item on the grid. A quick drag
  leaves no gaps, and belts drawn by dragging face the way the drag goes,
  turning at corners.
- **Left click a building**: inspect it (at the top of the right panel);
  pick a machine's recipe, a collector's target or a sorter's item. New
  machines, collectors and sorters start with the settings last picked for
  their kind.
- **Right click / drag**: remove buildings and refund them (not the bridge
  or the engines, which are part of the ship's layout).
- **Shift + right click a building**: copy its settings (recipe, target or
  filter). **Shift + left click / drag**: paste them onto each building
  that can take them.
- **Right click an item in the build palette**: hand-craft one (Shift: 5).
- **Mouse wheel**: zoom the grid (about the cursor).
- **Middle drag, or hold Space and move the mouse**: pan the grid.
- **M**: the star map; click a sector for its details, and to jump the
  ship shown there. Drag to pan, mouse wheel to zoom.
- **[ / ]**: show the previous / next ship of the fleet (or click its tab
  above the grid; double-click a tab to rename the ship).
- **1–9**: pick the item to build. The palette lists every building
  unlocked, in a fixed order (faded when there are none in the bridge), so
  the keys stay put. **R** / **Shift+R**: turn what's being built (or a
  blueprint being pasted) clockwise / back.
- **Q** over a building: pick it to build, facing the same way and with its
  settings for new ones. **Q** anywhere else, or the selected item's key or
  icon again: build nothing, so left clicks only inspect.
- **?** (the / key), or Keys under the grid: every control, in a list. The
  bar under the grid shows the keys that do something right now.
- **Ctrl+C** (Cmd+C on macOS): copy; drag a box over what to copy, and the
  copy is picked up to paste. **Ctrl+V** (Cmd+V): pick up the last copy.
  While pasting: **left click** pastes, **R** turns it, **right click**,
  **Escape** or **Q** puts it away (see Blueprints).
- **B**: the Blueprints window (saved blueprints, import and export).
- **F1**: the debug menu, while playing: pause and single-step the
  simulation, give the bridge items, reveal the map, charge the thrusters,
  send or clear a wave, complete the milestone, and the Dear ImGui demo
  window (widget reference).
- **F2**: the ImPlot demo window.

## Code layout

One directory per layer under `src/`; includes name their layer
(`#include "sim/world.h"`).

- `src/sim/` — pure simulation, no SDL/imgui/OpenGL, so the tests can link
  it headless.
  - `game_types.h` — `Tick` and the tick rate (60 per real second),
    `GridPos`, `GridSize`, `Direction`, and `ItemKind` (what the inventory
    holds) with each item's name and id (`items.cpp`).
  - `building.h/.cpp` — the `Building` class hierarchy (`Bridge`,
    `Collector`, `Conveyor`, `Machine`, ...),
    each kind's footprint (`footprint_of`, e.g. 2x2 collectors), the
    collectors' rates and buffers (`CollectorSpec`), belts (`Conveyor`),
    machines and which recipes they run, the `make_building` factory and
    `Inventory` (the bridge's, and building buffers).
  - `recipes.h/.cpp` — every `Recipe` (inputs, outputs, time, the
    `CraftCategory` of machine that makes it, and whether the bridge can
    hand-craft it); a building's cost is the recipe that makes its item.
  - `ship.h/.cpp` — `Ship`: the build grid (its layout's hull, and
    buildings stored at their top-left cell), placement / removal rules,
    the starting ship, and moving items between neighboring buildings each
    tick; the yields of the sector it's in, which its collectors gather at.
  - `ship_layout.h/.cpp` — the `ShipLayout`s a ship is built in: each one's
    hull, bridge and engines, its cost, and the milestone that unlocks it.
  - `sector.h/.cpp` — the `StarMap` of `Sector`s (links, yields, threat,
    what the player knows of each) and generating it from a seed.
  - `rng.h` — `Rng`: a small random number generator whose whole state is
    one number, so saves keep it.
  - `blueprint.h/.cpp` — `Blueprint`: copying part of a ship (buildings
    and their settings), turning it, and pasting it all or
    nothing (each piece checked by `Ship::can_place` against the ones
    before it, `PlannedChanges`).
  - `milestones.h/.cpp` — the milestones (what to deliver, what each
    unlocks) and `Progress`, the fleet's way through them (every bridge
    shares one), which gates hand-crafting and machine recipes.
  - `fleet.h/.cpp` — `Fleet`: the player's ships (`FleetShip`: an id, a
    name, a `Ship` and the sector it's in), the `Progress` they share,
    building a new ship in a layout, and cargo pods in flight
    between ships (`Transfer`).
  - `combat.h/.cpp` — enemies (`Enemy`, `EnemySpec`), the waves hostile
    sectors send, and the fight on a ship's grid (`Battle`, `combat_tick`)
    between them and its turrets.
  - `world.h/.cpp` — `World`: owns all state (the star map, the fleet and
    the fights' random numbers), `advance_tick()` steps it; jumping between
    sectors, survey scanners, losing ships, and buildings mending.
- `src/persistence/` — save files, also headless (the tests link it). The
  simulation knows nothing about saving beyond a `friend struct SaveAccess`
  in each class with private state to save.
  - `save_file.h/.cpp` — the save format, a JSON document: a header (name,
    when, play time) and the whole `World` (the star map, every ship of the
    fleet and its fight, the shared milestone progress and the cargo in
    flight), with items, directions and
    recipes written by name, and loading that checks everything (a bad
    save is refused with a reason, never trusted).
  - `save_slots.h/.cpp` — the saves folder: listing, writing (atomically,
    via a temporary file), loading, renaming, copying and deleting saves.
  - `blueprint_file.h/.cpp` — blueprints as one-line JSON strings (checked
    like saves) and the blueprint library folder.
  - `json_io.h/.cpp` — the JSON pieces saves and blueprints share: items,
    directions and recipes by name, and checked reading.
  - `files.h/.cpp` — atomic writes, numbered file names and deleting, for
    both folders.
- `src/ui/` — the only code that knows Dear ImGui exists.
  - `ui.h` — the `Screen` enum and the per-screen draw functions
    `main.cpp` calls each frame.
  - `ui_internal.h` — what the UI files share behind `ui.h`: the game
    screen's UI-only state (what's selected, the camera, the clipboard,
    the ship shown, the star map view) and the common helpers and widgets.
  - `ui.cpp` — the game screen (`DrawPlaying`): its panels laid out, the
    hint bar, banners and message toasts, the Keys window, the pause menu
    and the fleet-lost screen.
  - `ui_grid.cpp` — the build grid, with its zoom / pan camera (one per
    ship): building, removing, copying and pasting with the mouse, and the
    enemies.
  - `ui_panels.cpp` — the build palette (left) and the craft panel
    (right): the milestone, the inspector, hand-crafting.
  - `ui_fleet.cpp` — the fleet's tabs, the Send cargo and Build a new ship
    windows, and the star map.
  - `ui_blueprints.cpp` — the Blueprints window.
  - `ui_menus.cpp` — the main menu, save list and credits.
  - `ui_debug.cpp` — the debug menu (F1) and the ImPlot demo window (F2).
  - `ui_common.cpp`, `ui_draw.cpp` — the shared state and widgets, and
    drawing buildings, items and icons (from their sprites, or stand-ins).
  - `ui_theme.h/.cpp` — the dark, industrial look: the palette, ImGui /
    ImPlot styles, the embedded fonts (Roboto, and Cousine for headings and
    numbers) and a few styled widgets (bevelled plates, hazard stripes, key
    caps, collapsible headings). Every color comes from here.
  - `sprites.h/.cpp` — the embedded sprites (see Sprites), packed into
    one atlas texture at startup and looked up by item or building.
- `src/app/` — `main.cpp` (window, GL context, screen switching,
  fixed-timestep loop, the game being played and when it is saved), the
  embedded app icon, and the macOS-only startup tweak.
- `tests/` — the headless tests, one binary per `test_*.cpp` (`make
  test`), linked against `src/sim/` and `src/persistence/` only.
- `assets/` — the app icon (`logo.png`) and the sprites.
- `tools/` — `make_placeholder_sprites.py` (see Sprites).
- `imgui/`, `implot/` — Dear ImGui and ImPlot, as git submodules.
- `third_party/` — vendored single headers: nlohmann::json (saves and
  blueprints) and stb_image (decoding the icon and sprites).
