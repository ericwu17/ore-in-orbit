// ui_internal.h — what the UI's files share, behind ui.h: the game screen's
// UI-only state, and the helpers and widgets more than one screen draws with.
// Only src/ui/ includes it.
//
// The UI is split by screen area, each file drawing one part:
//   ui.cpp             the game screen (DrawPlaying): the layout of its panels,
//                      the keys, the hint bar, banners and toasts, the pause
//                      menu and the fleet-lost screen
//   ui_common.cpp      the state below, and the small widgets every part uses
//                      (icon buttons, amount lines, searchable combos)
//   ui_draw.cpp        drawing buildings, items and icons into a draw list
//   ui_grid.cpp        the ship build grid: camera, building, copy and paste
//   ui_panels.cpp      the build palette (left) and the craft panel (right),
//                      with the inspector and the milestone
//   ui_fleet.cpp       the fleet's tabs and windows, and the star map
//   ui_blueprints.cpp  the Blueprints window (the library)
//   ui_menus.cpp       the main menu, the save list and the credits
//   ui_debug.cpp       the developer windows (F1 / F2)
#pragma once

#include "ui/sprites.h"
#include "ui/ui_theme.h"

#include "sim/blueprint.h"
#include "sim/building.h"
#include "sim/combat.h"
#include "sim/fleet.h"
#include "sim/ship.h"
#include "sim/world.h"

#include "imgui.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// Pages and keys (ui_common.cpp)
// ---------------------------------------------------------------------------

// A borderless, transparent window covering the whole display, for a screen's
// contents, plus any `extra` window flags. Pair with ImGui::End().
void BeginFullscreenPage(const char* id, ImGuiWindowFlags extra = 0);

// Text centered horizontally in the current window, at the cursor's height.
void CenteredText(const char* text, ImVec4 color);

// A shortcut key went down this frame, unless the player is typing in a text
// field. (Not WantCaptureKeyboard: with keyboard navigation on, that is set
// whenever any ImGui window has focus, and the screens are ImGui windows.)
bool ShortcutPressed(ImGuiKey key);

// ShortcutPressed() for a key with modifiers (ImGuiMod_Ctrl | ImGuiKey_C).
// ImGuiMod_Ctrl is Cmd on macOS.
bool ChordPressed(ImGuiKeyChord chord);

// The key that goes with C and V for copy and paste.
#ifdef __APPLE__
constexpr const char* kCtrlName = "Cmd";
#else
constexpr const char* kCtrlName = "Ctrl";
#endif

// ---------------------------------------------------------------------------
// The game screen's state (ui_common.cpp). UI-only: the simulation never
// sees any of it.
// ---------------------------------------------------------------------------

// What the player is about to build.
struct BuildUiState {
    std::optional<ItemKind> selected;     // nullopt: nothing, clicks only inspect
    Direction facing = Direction::North;  // for rotatable buildings
    std::string message;                  // why the last click did nothing
    ImVec4 message_color = theme::kBad;
    double message_time = -1.0e9;         // ImGui::GetTime() when it was set
    // The building shown in the inspector (clicked on the grid): its
    // top-left cell, and the building itself, to notice it went away.
    std::optional<GridPos> inspected;
    const Building* inspected_building = nullptr;
    // The grid cell under the mouse last frame (Q picks what's there).
    std::optional<GridPos> hover_cell;
    // The current left drag: the last cell it went over, and the cells it
    // built on, which a belt drawn round a corner turns to face the new way.
    std::optional<GridPos> drag_last;
    std::vector<GridPos> drag_placed;
    bool show_keys = false;  // the keys overlay (?)
};
extern BuildUiState g_build;

// How long a message (SetMessage) shows.
constexpr double kMessageSeconds = 3.0;

// A machine's recipe, a collector's target or a sorter's filter: what a
// building of that kind is set to do.
struct BuildingSettings {
    ItemKind kind = ItemKind::TransportBelt;  // the building they came from
    const Recipe* recipe = nullptr;
    std::optional<ItemKind> target;
    std::optional<ItemKind> filter;
    bool has_filter = false;  // a sorter's (whose filter may be none)
};

// The settings last picked for each kind of building, which new ones of that
// kind start with, and the ones Shift+right-click copied (Shift+left-click
// pastes them).
struct SettingsUiState {
    std::unordered_map<int, BuildingSettings> last;  // by ItemKind
    std::optional<BuildingSettings> copied;
};
extern SettingsUiState g_settings;

// The view onto the build grid: how far in it is zoomed and where it has
// been panned to.
struct Camera {
    bool placed = false;  // false: center on the bridge at the default zoom, next frame
    float cell = 0.0f;    // screen pixels per grid cell
    ImVec2 offset;        // the grid's top-left corner, relative to the canvas's
    bool space_pans = false;  // Space went down (or was held) over the canvas: the grid follows the mouse
    bool left_pans = false;   // the current left drag started with Space held: it pans, never builds
};
extern Camera g_camera;

// Mouse-wheel zoom steps multiply the cell size (or the star map's scale) by
// this.
constexpr float kZoomStep = 1.2f;

// What a left click on the grid does: build (the selected item), or copy and
// paste with blueprints.
enum class Tool {
    Build,
    Copy,   // drag a box to copy what's in it
    Paste,  // click to paste the held blueprint
};

// Copying and pasting. The clipboard outlives the game, so copies go from
// one save to another.
struct BlueprintUiState {
    Tool tool = Tool::Build;
    std::optional<GridPos> copy_start;   // where the copy drag started
    std::optional<Blueprint> clipboard;  // the last copy
    std::optional<Blueprint> held;       // what Paste pastes, turned as the player turns it
    std::string paste_status;            // why it can't be pasted where the mouse is; empty if it can
    bool show_library = false;           // the Blueprints window
};
extern BlueprintUiState g_blueprints;

// Which ship the game screen shows, and the views onto the others, kept
// while the player looks at another.
struct FleetUiState {
    int current = 0;  // ship id; one that isn't there (a new or loaded game): the flagship
    std::unordered_map<int, Camera> cameras;  // by ship id, for the ships not shown
    std::optional<int> renaming;               // the ship whose tab is a name field
    char rename[64] = {};
    bool focus_rename = false;
    // The Send cargo window: where to, and what.
    bool show_send = false;
    int send_to = 0;
    std::vector<ItemStack> cargo;
    // The Build a new ship window, and the layout picked in it (an index
    // into all_layouts()).
    bool show_build = false;
    std::size_t build_layout = 0;
};
extern FleetUiState g_fleet;

// The star map view (M), shown instead of the grid: whether it's up, the
// sector picked on it, and the view onto it.
struct MapUiState {
    bool shown = false;
    std::optional<int> selected;  // a sector index
    bool placed = false;          // false: fit the map in, next frame
    float scale = 1.0f;           // screen pixels per map unit
    ImVec2 offset;                // where home is, relative to the canvas
    bool dragged = false;         // the press on the map became a pan, not a click
};
extern MapUiState g_map;

// The building in the inspector, or null (forgetting it if it was removed).
const Building* InspectedBuilding(const Ship& ship);

// Back to building (from copying or pasting).
void LeaveBlueprintTool();
// Picks up `blueprint` to paste.
void HoldBlueprint(const Blueprint& blueprint);
// Pick `kind` to build, or put it away if it is already picked.
void ToggleSelected(ItemKind kind);

// Shows ship `id` (its own camera, nothing inspected).
void SwitchToShip(int id);
// The ship the game screen shows: the one picked, or the flagship.
FleetShip& CurrentShip(Fleet& fleet);

// Shows `message` over the grid for kMessageSeconds.
void SetMessage(std::string message, ImVec4 color = theme::kBad);
// SetMessage() with why a build or removal didn't happen.
void SetBuildMessage(BuildResult result);

// What the building `b` is set to do, if it has settings (a machine, a
// collector or a sorter).
std::optional<BuildingSettings> SettingsOf(const Building& b);
// Remembers `s` as what new buildings of its kind start with.
void RememberSettings(const BuildingSettings& s);
// Sets the building covering `p` to `s` where it can take them; whether
// anything changed.
bool ApplySettings(Ship& ship, GridPos p, const BuildingSettings& s);
bool SameSettings(const BuildingSettings& a, const BuildingSettings& b);
// The name of what settings set a building to ("Iron plate"), or null for
// none.
const char* SettingsName(const BuildingSettings& s);

// "6x4, 5 buildings".
std::string BlueprintSummary(const Blueprint& blueprint);
// Everything pasting it takes from the inventory, on bare space.
Inventory BlueprintCost(const Blueprint& blueprint);

// ---------------------------------------------------------------------------
// Drawing buildings, items and icons (ui_draw.cpp)
// ---------------------------------------------------------------------------

const char* DirectionName(Direction d);

// Screen-space top-left corner of a grid cell.
ImVec2 CellMin(ImVec2 origin, float cell, GridPos p);
// Screen-space bottom-right corner of a footprint whose top-left cell is at `min`.
ImVec2 FootprintMax(ImVec2 min, float cell, GridSize size);

// A sprite stretched over `min`..`max`, turned from North (as drawn) to
// face `facing`, and faded by `alpha`.
void DrawSprite(ImDrawList* draw, const sprites::Sprite& sprite, ImVec2 min, ImVec2 max,
                Direction facing = Direction::North, float alpha = 1.0f);

// Draws one building over its footprint, whose top-left cell is at `min`
// (cells of side `cell`): its sprite, or a stand-in when it has none. `kind`
// is nullopt for the bridge. `alpha` fades the placement ghost. A belt tunnel
// looks like its entrance unless `tunnel_exit`.
void DrawBuilding(ImDrawList* draw, ImVec2 min, float cell, GridSize size,
                  std::optional<ItemKind> kind, Direction facing, float alpha, bool tunnel_exit = false);

// A thin gauge along the bottom of a building's footprint (`min`..`max`),
// filled to `fraction`; warning-colored when full.
void DrawBufferGauge(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, float fraction);

// One item, centered on `c`, about `r` across: its sprite, or without one,
// raw resources as round lumps, everything made from them as squares.
void DrawItem(ImDrawList* draw, ImVec2 c, float r, ItemKind kind);

// The items riding a belt whose cell's top-left corner is `min`: position 0
// is its back edge, kBeltLength its front edge.
void DrawBeltItems(ImDrawList* draw, const Conveyor& belt, ImVec2 min, float cell);
// One item in the top-right corner of a building's footprint: the setting
// the building is working to. Skipped when too far out to read.
void DrawCornerItem(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, ItemKind kind);
// A machine's recipe (its main product, in the top-right corner) and its
// craft progress (a gauge along the bottom).
void DrawMachineStatus(ImDrawList* draw, const Machine& machine, ImVec2 min, ImVec2 max, float cell);
// A collector's target (in the top-right corner) and how full its buffer is
// (a gauge along the bottom).
void DrawCollectorStatus(ImDrawList* draw, const Collector& collector, ImVec2 min, ImVec2 max, float cell);
// The items crossing a junction whose cell's top-left corner is `min`, each
// as far across as it has got.
void DrawJunctionItems(ImDrawList* draw, const Junction& junction, ImVec2 min, float cell);
// The item a router holds, in the middle of its tile.
void DrawRouterItem(ImDrawList* draw, const Router& router, ImVec2 min, float cell);
// A sorter's filter item, in the middle of its tile.
void DrawSorterFilter(ImDrawList* draw, const Sorter& sorter, ImVec2 min, float cell);

// What an icon in a panel shows: an item, a building as built, or
// nothing ("None" in a picker).
struct Icon {
    enum class Type {
        None,
        Item,
        Building,
        Bridge,
    };
    Type type = Type::None;
    ItemKind item = ItemKind::TransportBelt;
};

Icon ItemIcon(ItemKind kind);
// A building as it looks built (the bridge has no item kind).
Icon BuildingIcon(const Building& b);
// What a recipe is shown as: what it makes or, for the recipes named after
// what they do, what goes in.
Icon RecipeIcon(const Recipe& r);
const char* IconName(const Icon& icon);
const char* CategoryName(ItemCategory category);

// An icon filling the square `min`..`max`: its sprite, or a stand-in when
// that is missing.
void DrawIcon(ImDrawList* draw, const Icon& icon, ImVec2 min, ImVec2 max, float alpha = 1.0f);

// ---------------------------------------------------------------------------
// Widgets (ui_common.cpp)
// ---------------------------------------------------------------------------

// Small labels over an icon's corners (null: none).
struct Badges {
    const char* top_left = nullptr;
    const char* bottom_right = nullptr;
};

// A square button `size` across showing `icon`, outlined amber when
// selected, with `badges` and the icon faded by `fade`. Returns true when
// clicked.
bool IconButton(const char* id, const Icon& icon, float size, bool selected, Badges badges = {},
                float fade = 1.0f);
// An icon framed like IconButton, only to look at (and hover).
void IconTile(const Icon& icon, float size, Badges badges = {});
// An unframed icon `size` across; hovering it shows its name.
void IconImage(const Icon& icon, float size);

// Lays icons `size` across out in rows, wrapping at the right edge of the
// space there was when Start() was called. Call Next() before each icon.
struct IconFlow {
    float size = 0.0f;
    float right = 0.0f;       // where rows end, in screen space
    float last_right = 0.0f;  // the previous icon's right edge
    int placed = 0;

    void Start(float icon_size) {
        size = icon_size;
        right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        placed = 0;
    }

    void Next() {
        const float spacing = ImGui::GetStyle().ItemSpacing.y;
        if (placed++ > 0 && last_right + spacing + size <= right)
            ImGui::SameLine(0.0f, spacing);
        last_right = ImGui::GetCursorScreenPos().x + size;
    }
};

// A count short enough for a badge: 950, 12k, 3M.
std::string ShortCount(int n);
// "2 / 4".
std::string Fraction(int have, int need);

// One line about an amount of an item: its icon, then `text` ("3",
// "2 / 4") and `suffix`, in `color`. Hovering the icon shows its name;
// `named` writes the name out after the text too, for tooltips, where
// nothing can be hovered.
void AmountLine(const Icon& icon, const std::string& text, ImVec4 color, bool named, const char* suffix = "");

// The placeable items in the build list: every one unlocked, plus any in
// the inventory and the selected one, in a fixed order, so that the first
// nine, which get the number keys, only move when a milestone unlocks more.
std::vector<ItemKind> BuildList(const Ship& ship);

// The bridge's hand-crafting recipe for `kind`, or null if it's only made
// in machines.
const Recipe* HandRecipeFor(ItemKind kind);

// `s` without whitespace at either end.
std::string Trimmed(const std::string& s);
// Whether `text` contains `part`, ignoring case.
bool ContainsIgnoringCase(std::string_view text, std::string_view part);

// A combo box whose closed frame shows `icon` and `text` as its value. Same
// contract as ImGui::BeginCombo (EndCombo() when it returns true).
bool BeginIconCombo(const char* id, const Icon& icon, const char* text);

// Search for a combo's options: a box above them, emptied and focused as the
// popup opens, that hides the options not containing what's typed (ignoring
// case); once something is typed, Enter picks the first one left. The
// options are icon buttons in rows. Only one combo is open at a time, so they
// all share one search.
//
// Call BeginComboSearch() first thing once BeginIconCombo() returns true,
// and EndComboSearch() before EndCombo(). The options in between scroll
// under the box.
void BeginComboSearch();
// Whether the option `label` matches the search. Each one that does must
// then be drawn with ComboSearchOption().
bool ComboSearchShows(const char* label);
// An option the search shows, as an icon button: true, closing the combo,
// when it is clicked, or when it is the first one and Enter was pressed.
bool ComboSearchOption(const Icon& icon, bool selected);
void EndComboSearch();
// Every item as a search option, by category, each under a heading; the
// one picked, if any.
std::optional<ItemKind> ComboSearchItemOptions(std::optional<ItemKind> current);

// ---------------------------------------------------------------------------
// The game screen's parts, which DrawPlaying (ui.cpp) lays out
// ---------------------------------------------------------------------------

// ui_panels.cpp

// The left panel: an icon per placeable item with its count (click to
// build it, right click to hand-craft one), what is selected, its facing
// and the settings new ones get, copying and pasting, and the cargo
// (everything else in the inventory).
void DrawBuildPalette(Ship& ship, float width);
// The right panel: the inspector, the current milestone, the fleet, the
// bridge's craft queue and hand-crafting, and the recipe book for everything
// else.
void DrawCraftPanel(World& world, FleetShip& current, float width);
// What a building is and what it's doing: for the grid's hover tooltip
// (`in_tooltip`, which writes item names out) and the inspector (where
// hovering an icon shows its name).
void BuildingDetails(const Building& b, bool in_tooltip);

// ui_fleet.cpp

// The tabs over the grid, one per ship: a click shows that ship, a
// double-click renames it.
void DrawFleetStrip(World& world);
// The ship before or after the one shown (`step` -1 or 1), going round.
void CycleShip(const Fleet& fleet, int step);
// The fleet, in the right panel: building a new ship, sending cargo, and
// the cargo on its way to or from the ship shown.
void DrawFleetSection(World& world, const FleetShip& current);
// The Send cargo window: from the ship shown to another in its sector, the
// items picked (from its bridge inventory) and the pods they take.
void DrawSendCargoWindow(World& world, FleetShip& current);
// The Build a new ship window: every layout, the one picked drawn with what
// it costs, and building it from the bridge of the ship shown.
void DrawBuildShipWindow(World& world, FleetShip& current);
// The star map (M), filling `avail` in place of the grid.
void DrawStarMap(World& world, const FleetShip& current, ImVec2 avail);

// ui_grid.cpp

// The ship's build grid, filling `avail`: the hull and buildings, the
// enemies on it, and building, removing, copying and pasting with the
// mouse.
void DrawShipGrid(Ship& ship, const Battle& battle, Tick now, ImVec2 avail);

// ui_blueprints.cpp

// The Blueprints window (B, or Library in the left panel): the blueprints
// saved in `dir`, to paste, rename, delete or copy as text, and saving the
// clipboard into it or importing a blueprint copied as text.
void DrawBlueprintLibrary(const std::filesystem::path& dir);

}  // namespace ui
