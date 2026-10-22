// ui.cpp — the screens: main menu, save list, credits and the game screen
// (the ship build grid, its build palette and the crafting panel, copying
// and pasting, and the blueprint library), plus the debug menu and the
// ImPlot demo window.
#include "ui/ui.h"
#include "ui/sprites.h"
#include "ui/ui_theme.h"

#include "persistence/blueprint_file.h"
#include "persistence/save_slots.h"
#include "sim/blueprint.h"
#include "sim/combat.h"
#include "sim/fleet.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include "imgui.h"
#include "implot.h"  // ImPlot::ShowDemoWindow

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace theme = ui::theme;
namespace sprites = ui::sprites;

// A borderless, transparent window covering the whole display, for a screen's
// contents, plus any `extra` window flags. Pair with ImGui::End().
void BeginFullscreenPage(const char* id, ImGuiWindowFlags extra = 0) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking | extra);
}

// Text centered horizontally in the current window, at the cursor's height.
void CenteredText(const char* text, ImVec4 color) {
    const float width = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::TextColored(color, "%s", text);
}

// A shortcut key went down this frame, unless the player is typing in a text
// field. (Not WantCaptureKeyboard: with keyboard navigation on, that is set
// whenever any ImGui window has focus, and the screens are ImGui windows.)
bool ShortcutPressed(ImGuiKey key) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(key, false);
}

// ShortcutPressed() for a key with modifiers (ImGuiMod_Ctrl | ImGuiKey_C).
// ImGuiMod_Ctrl is Cmd on macOS.
bool ChordPressed(ImGuiKeyChord chord) {
    return !ImGui::GetIO().WantTextInput && ImGui::IsKeyChordPressed(chord);
}


// ---------------------------------------------------------------------------
// Game screen
// ---------------------------------------------------------------------------

// What the player is about to build. UI-only: the simulation never sees it.
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
BuildUiState g_build;

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
// pastes them). UI-only.
struct SettingsUiState {
    std::unordered_map<int, BuildingSettings> last;  // by ItemKind
    std::optional<BuildingSettings> copied;
};
SettingsUiState g_settings;

// The building in the inspector, or null (forgetting it if it was removed).
const Building* InspectedBuilding(const Ship& ship) {
    if (g_build.inspected && ship.building_anchored_at(*g_build.inspected) == g_build.inspected_building)
        return g_build.inspected_building;
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
    return nullptr;
}

constexpr double kMessageSeconds = 3.0;

// The view onto the build grid: how far in it is zoomed and where it has
// been panned to. UI-only, like BuildUiState.
struct Camera {
    bool placed = false;  // false: centre on the bridge at the default zoom, next frame
    float cell = 0.0f;    // screen pixels per grid cell
    ImVec2 offset;        // the grid's top-left corner, relative to the canvas's
    bool space_pans = false;  // Space went down (or was held) over the canvas: the grid follows the mouse
    bool left_pans = false;   // the current left drag started with Space held: it pans, never builds
};
Camera g_camera;

// Mouse-wheel zoom steps multiply the cell size by this.
constexpr float kZoomStep = 1.2f;

// What a left click on the grid does: build (the selected item), or copy and
// paste with blueprints.
enum class Tool {
    Build,
    Copy,   // drag a box to copy what's in it
    Paste,  // click to paste the held blueprint
};

// Copying and pasting. UI-only, like BuildUiState; the clipboard outlives
// the game, so copies go from one save to another.
struct BlueprintUiState {
    Tool tool = Tool::Build;
    std::optional<GridPos> copy_start;   // where the copy drag started
    std::optional<Blueprint> clipboard;  // the last copy
    std::optional<Blueprint> held;       // what Paste pastes, turned as the player turns it
    std::string paste_status;            // why it can't be pasted where the mouse is; empty if it can
    bool show_library = false;           // the Blueprints window
};
BlueprintUiState g_blueprints;

// Back to building (from copying or pasting).
void LeaveBlueprintTool() {
    g_blueprints.tool = Tool::Build;
    g_blueprints.copy_start.reset();
    g_blueprints.held.reset();
    g_blueprints.paste_status.clear();
}

// Which ship the game screen shows, and the views onto the others, kept
// while the player looks at another. UI-only, like BuildUiState.
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
FleetUiState g_fleet;

// The star map view (M), shown instead of the grid: whether it's up, the
// sector picked on it, and the view onto it. UI-only.
struct MapUiState {
    bool shown = false;
    std::optional<int> selected;  // a sector index
    bool placed = false;          // false: fit the map in, next frame
    float scale = 1.0f;           // screen pixels per map unit
    ImVec2 offset;                // where home is, relative to the canvas
    bool dragged = false;         // the press on the map became a pan, not a click
};
MapUiState g_map;

// Shows ship `id` (its own camera, nothing inspected).
void SwitchToShip(int id) {
    FleetUiState& f = g_fleet;
    if (id == f.current)
        return;
    f.cameras[f.current] = g_camera;
    g_camera = f.cameras[id];  // a ship not seen yet: centred on its bridge
    f.cameras.erase(id);
    f.current = id;
    f.renaming.reset();
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
}

// The ship the game screen shows: the one picked, or the flagship.
FleetShip& CurrentShip(Fleet& fleet) {
    if (FleetShip* s = fleet.find(g_fleet.current))
        return *s;
    g_fleet.current = fleet.flagship().id;
    return fleet.flagship();
}

// Picks up `blueprint` to paste.
void HoldBlueprint(const Blueprint& blueprint) {
    LeaveBlueprintTool();
    g_blueprints.tool = Tool::Paste;
    g_blueprints.held = blueprint;
}

// Pick `kind` to build, or put it away if it is already picked.
void ToggleSelected(ItemKind kind) {
    LeaveBlueprintTool();
    if (g_build.selected == kind)
        g_build.selected.reset();
    else
        g_build.selected = kind;
}

void SetMessage(std::string message, ImVec4 color = theme::kBad) {
    g_build.message = std::move(message);
    g_build.message_color = color;
    g_build.message_time = ImGui::GetTime();
}

void SetBuildMessage(BuildResult result) {
    SetMessage(describe(result));
}

// What the building `b` is set to do, if it has settings (a machine, a
// collector or a sorter).
std::optional<BuildingSettings> SettingsOf(const Building& b) {
    const std::optional<ItemKind> kind = b.item_kind();
    if (!kind)
        return std::nullopt;
    BuildingSettings s;
    s.kind = *kind;
    if (const auto* machine = dynamic_cast<const Machine*>(&b))
        s.recipe = machine->get_recipe();
    else if (const auto* collector = dynamic_cast<const Collector*>(&b))
        s.target = collector->get_target();
    else if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        s.filter = sorter->get_filter();
        s.has_filter = true;
    } else
        return std::nullopt;
    return s;
}

// Remembers `s` as what new buildings of its kind start with.
void RememberSettings(const BuildingSettings& s) {
    g_settings.last[static_cast<int>(s.kind)] = s;
}

// Sets the building covering `p` to `s` where it can take them; whether
// anything changed.
bool ApplySettings(Ship& ship, GridPos p, const BuildingSettings& s) {
    const Building* b = ship.building_at(p);
    const std::optional<BuildingSettings> now = b ? SettingsOf(*b) : std::nullopt;
    if (!now)
        return false;
    if (dynamic_cast<const Machine*>(b))
        return s.recipe && s.recipe != now->recipe && ship.set_recipe(p, s.recipe);
    if (dynamic_cast<const Collector*>(b))
        return s.target && s.target != now->target && ship.set_collector_target(p, *s.target);
    return s.has_filter && s.filter != now->filter && ship.set_sort_filter(p, s.filter);
}

bool SameSettings(const BuildingSettings& a, const BuildingSettings& b) {
    return a.recipe == b.recipe && a.target == b.target && a.filter == b.filter;
}

// The name of what settings set a building to ("Iron plate"), or null for
// none.
const char* SettingsName(const BuildingSettings& s) {
    if (s.recipe)
        return recipe_name(*s.recipe);
    if (s.target)
        return item_name(*s.target);
    if (s.filter)
        return item_name(*s.filter);
    return nullptr;
}

// The key that goes with C and V for copy and paste.
#ifdef __APPLE__
constexpr const char* kCtrlName = "Cmd";
#else
constexpr const char* kCtrlName = "Ctrl";
#endif

// "6x4, 5 buildings".
std::string BlueprintSummary(const Blueprint& blueprint) {
    const std::size_t n = blueprint.buildings.size();
    return std::to_string(blueprint.size.w) + "x" + std::to_string(blueprint.size.h) + ", " + std::to_string(n) +
           (n == 1 ? " building" : " buildings");
}

// Everything pasting it takes from the inventory, on bare space.
Inventory BlueprintCost(const Blueprint& blueprint) {
    Inventory cost;
    for (const BlueprintBuilding& b : blueprint.buildings)
        cost.add(b.kind);
    return cost;
}

const char* DirectionName(Direction d) {
    switch (d) {
    case Direction::North: return "North";
    case Direction::East:  return "East";
    case Direction::South: return "South";
    case Direction::West:  return "West";
    }
    return "?";
}

// Unit vector for a direction in screen space (y grows down).
ImVec2 DirectionVector(Direction d) {
    switch (d) {
    case Direction::North: return {0.0f, -1.0f};
    case Direction::East:  return {1.0f, 0.0f};
    case Direction::South: return {0.0f, 1.0f};
    case Direction::West:  return {-1.0f, 0.0f};
    }
    return {0.0f, 0.0f};
}

// The short label on a building's tile, for the ones drawn as lettered tiles.
const char* TileLabel(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector: return "A";
    case ItemKind::TractorArray:      return "TA";
    case ItemKind::CargoHold:         return "C";
    case ItemKind::SolarFurnace:      return "SF";
    case ItemKind::Refinery:          return "R";
    case ItemKind::FabricatorMk1:     return "F1";
    case ItemKind::FabricatorMk2:     return "F2";
    case ItemKind::FabricatorMk3:     return "F3";
    case ItemKind::SolarPanel:        return "SP";
    case ItemKind::SolarArray:        return "SA";
    case ItemKind::BatteryBank:       return "BB";
    case ItemKind::SurveyScanner:     return "SS";
    case ItemKind::IonThruster:       return "IT";
    case ItemKind::Autocannon:        return "AC";
    case ItemKind::Railgun:           return "RG";
    default:                          return "?";
    }
}

// The tile color of a lettered building, by family.
ImVec4 TileColor(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector:
    case ItemKind::TractorArray:      return theme::kTileRockCollector;
    case ItemKind::CargoHold:         return theme::kTileStorage;
    case ItemKind::SolarPanel:
    case ItemKind::SolarArray:
    case ItemKind::BatteryBank:       return theme::kTilePower;
    case ItemKind::SurveyScanner:
    case ItemKind::IonThruster:       return theme::kTileSystem;
    case ItemKind::Autocannon:
    case ItemKind::Railgun:           return theme::kTileTurret;
    default:                          return theme::kTileMachine;
    }
}

// Screen-space top-left corner of a grid cell.
ImVec2 CellMin(ImVec2 origin, float cell, GridPos p) {
    return {origin.x + static_cast<float>(p.x) * cell, origin.y + static_cast<float>(p.y) * cell};
}

// Screen-space bottom-right corner of a footprint whose top-left cell is at `min`.
ImVec2 FootprintMax(ImVec2 min, float cell, GridSize size) {
    return {min.x + static_cast<float>(size.w) * cell, min.y + static_cast<float>(size.h) * cell};
}

// A filled arrowhead centered on `c`, pointing along unit vector `d`, of
// half-length `h`.
void DrawArrow(ImDrawList* draw, ImVec2 c, ImVec2 d, float h, ImU32 color) {
    const ImVec2 p(-d.y, d.x);
    draw->AddTriangleFilled(ImVec2(c.x + d.x * h, c.y + d.y * h),
                            ImVec2(c.x - d.x * h * 0.5f + p.x * h * 0.8f, c.y - d.y * h * 0.5f + p.y * h * 0.8f),
                            ImVec2(c.x - d.x * h * 0.5f - p.x * h * 0.8f, c.y - d.y * h * 0.5f - p.y * h * 0.8f),
                            color);
}

// A sprite stretched over `min`..`max`, turned from North (as drawn) to
// face `facing`, and faded by `alpha`.
void DrawSprite(ImDrawList* draw, const sprites::Sprite& sprite, ImVec2 min, ImVec2 max,
                Direction facing = Direction::North, float alpha = 1.0f) {
    // The picture's corners, clockwise from its top left; each quarter turn
    // moves them one screen corner on.
    const ImVec2 uv[4] = {sprite.uv0, ImVec2(sprite.uv1.x, sprite.uv0.y), sprite.uv1, ImVec2(sprite.uv0.x, sprite.uv1.y)};
    const int turns = static_cast<int>(facing);
    const auto at = [&](int corner) { return uv[(corner - turns + 4) % 4]; };
    draw->AddImageQuad(sprite.tex, min, ImVec2(max.x, min.y), max, ImVec2(min.x, max.y), at(0), at(1), at(2), at(3),
                       theme::U32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), alpha));
}

// Draws one building over its footprint, whose top-left cell is at `min`
// (cells of side `cell`): its sprite, or a stand-in when it has none. `kind`
// is nullopt for the bridge. `alpha` fades the placement ghost.
void DrawBuilding(ImDrawList* draw, ImVec2 min, float cell, GridSize size,
                  std::optional<ItemKind> kind, Direction facing, float alpha) {
    const float inset = cell * 0.12f;
    const ImVec2 max = FootprintMax(min, cell, size);
    const ImVec2 a(min.x + inset, min.y + inset);
    const ImVec2 b(max.x - inset, max.y - inset);
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float rounding = cell * 0.04f;
    const ImU32 glyph = theme::U32(theme::kTileGlyph, alpha);
    const ImVec2 d = DirectionVector(facing);

    auto letter_tile = [&](ImVec4 color, const char* letter) {
        draw->AddRectFilled(a, b, theme::U32(color, alpha), rounding);
        const ImVec2 ts = ImGui::CalcTextSize(letter);
        if (ts.x < (b.x - a.x) * 0.9f && ts.y < (b.y - a.y) * 0.9f)
            draw->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), glyph, letter);
    };
    // A belt-like strip with an arrow on every cell of the footprint.
    auto conveyor = [&](ImVec4 color) {
        draw->AddRectFilled(a, b, theme::U32(color, alpha), rounding * 0.5f);
        for (int y = 0; y < size.h; ++y)
            for (int x = 0; x < size.w; ++x) {
                const ImVec2 cc(min.x + (static_cast<float>(x) + 0.5f) * cell,
                                min.y + (static_cast<float>(y) + 0.5f) * cell);
                DrawArrow(draw, cc, d, cell * 0.28f, glyph);
            }
    };

    const sprites::Sprite* sprite = kind ? sprites::Building(*kind) : sprites::Bridge();
    if (sprite) {
        DrawSprite(draw, *sprite, min, max, kind && is_rotatable(*kind) ? facing : Direction::North, alpha);
        return;
    }
    if (!kind) {
        letter_tile(theme::kTileBridge, "Bridge");
        return;
    }
    switch (*kind) {
    case ItemKind::TransportBelt:
        conveyor(theme::kTileBelt);
        break;
    case ItemKind::ExpressBelt:
        conveyor(theme::kTileExpressBelt);
        break;
    case ItemKind::BeltTunnel: {
        conveyor(theme::kTileBelt);
        // The tunnel mouth: a bar across the back.
        const ImVec2 back(c.x - d.x * cell * 0.3f, c.y - d.y * cell * 0.3f);
        const ImVec2 p(-d.y * cell * 0.38f, d.x * cell * 0.38f);
        draw->AddLine(ImVec2(back.x - p.x, back.y - p.y), ImVec2(back.x + p.x, back.y + p.y),
                      theme::U32(theme::kTileTunnelMouth, alpha), cell * 0.12f);
        break;
    }
    case ItemKind::FilterSorter:
    case ItemKind::ReverseSorter:
        // No arrow: it doesn't face a way. Its filter is drawn over it (see
        // DrawSorterFilter).
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding * 0.5f);
        draw->AddRect(a, b, theme::U32(*kind == ItemKind::ReverseSorter ? theme::kAccent : theme::kWarn, alpha),
                      rounding * 0.5f, 0, std::max(1.0f, cell * 0.08f));
        break;
    case ItemKind::Junction:
        // A cross: the two ways over it.
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding * 0.5f);
        draw->AddLine(ImVec2(c.x, a.y), ImVec2(c.x, b.y), glyph, std::max(1.0f, cell * 0.1f));
        draw->AddLine(ImVec2(a.x, c.y), ImVec2(b.x, c.y), glyph, std::max(1.0f, cell * 0.1f));
        break;
    case ItemKind::Router:
        // An arrow out of every side.
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding);
        for (const Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const ImVec2 v = DirectionVector(dir);
            DrawArrow(draw, ImVec2(c.x + v.x * cell * 0.25f, c.y + v.y * cell * 0.25f), v, cell * 0.12f, glyph);
        }
        break;
    default:
        letter_tile(TileColor(*kind), TileLabel(*kind));
        break;
    }
}

// A thin gauge along the bottom of a building's footprint (`min`..`max`),
// filled to `fraction`; warning-colored when full.
void DrawBufferGauge(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, float fraction) {
    if (cell < 8.0f)
        return;  // too far out to read
    const float inset = cell * 0.2f;
    const float h = std::max(2.0f, cell * 0.08f);
    const ImVec2 a(min.x + inset, max.y - inset * 0.5f - h);
    const ImVec2 b(max.x - inset, max.y - inset * 0.5f);
    draw->AddRectFilled(a, b, theme::U32(theme::kTileGlyph, 0.5f));
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        draw->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * f, b.y),
                            theme::U32(f >= 1.0f ? theme::kWarn : theme::kTileGlyph));
}

// The color an item is drawn in, by material family.
ImVec4 ItemColor(ItemKind kind) {
    switch (kind) {
    case ItemKind::IronOre:
    case ItemKind::IronPlate:
    case ItemKind::Steel:           return theme::kItemIron;
    case ItemKind::CopperOre:
    case ItemKind::CopperPlate:     return theme::kItemCopper;
    case ItemKind::Silica:
    case ItemKind::Glass:
    case ItemKind::Silicon:         return theme::kItemSilica;
    case ItemKind::Carbon:          return theme::kItemCarbon;
    case ItemKind::IridiumOre:
    case ItemKind::IridiumPlate:    return theme::kItemExotic;
    case ItemKind::Plastic:         return theme::kItemChemical;
    default:
        return item_category(kind) == ItemCategory::Placeable ? theme::kItemPlaceable : theme::kItemComponent;
    }
}

// One item, centered on `c`, about `r` across: its sprite, or without one,
// raw resources as round lumps, everything made from them as
// squares.
void DrawItem(ImDrawList* draw, ImVec2 c, float r, ItemKind kind) {
    if (const sprites::Sprite* sprite = sprites::Item(kind)) {
        const float h = r * 1.3f;  // icons leave a margin around the item
        DrawSprite(draw, *sprite, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h));
        return;
    }
    const ImU32 fill = theme::U32(ItemColor(kind));
    const ImU32 outline = theme::U32(theme::kItemOutline, 0.7f);
    const ItemCategory category = item_category(kind);
    if (category == ItemCategory::RawResource) {
        draw->AddCircleFilled(c, r, fill);
        draw->AddCircle(c, r, outline);
    } else {
        draw->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fill);
        draw->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), outline);
    }
}

// The items riding a belt whose cell's top-left corner is `min`: position 0
// is its back edge, kBeltLength its front edge.
void DrawBeltItems(ImDrawList* draw, const Conveyor& belt, ImVec2 min, float cell) {
    const ImVec2 d = DirectionVector(belt.facing());
    const ImVec2 back(min.x + cell * 0.5f - d.x * cell * 0.5f, min.y + cell * 0.5f - d.y * cell * 0.5f);
    for (const BeltItem& item : belt.get_items()) {
        const float t = cell * static_cast<float>(item.pos) / static_cast<float>(kBeltLength);
        DrawItem(draw, ImVec2(back.x + d.x * t, back.y + d.y * t), std::max(1.5f, cell * 0.16f), item.kind);
    }
}

// A machine's recipe (its main product, in the top-right corner) and its
// craft progress (a gauge along the bottom).
void DrawMachineStatus(ImDrawList* draw, const Machine& machine, ImVec2 min, ImVec2 max, float cell) {
    const Recipe* recipe = machine.get_recipe();
    if (!recipe || cell < 8.0f)
        return;
    if (!recipe->outputs.empty()) {
        const float r = cell * 0.16f;
        DrawItem(draw, ImVec2(max.x - cell * 0.32f, min.y + cell * 0.32f), r, recipe->outputs.front().kind);
    }
    if (machine.crafting())
        DrawBufferGauge(draw, min, max, cell, machine.craft_progress());
}

// The items crossing a junction whose cell's top-left corner is `min`, each
// as far across as it has got.
void DrawJunctionItems(ImDrawList* draw, const Junction& junction, ImVec2 min, float cell) {
    for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West}) {
        const ImVec2 d = DirectionVector(travel);
        const ImVec2 back(min.x + cell * 0.5f - d.x * cell * 0.5f, min.y + cell * 0.5f - d.y * cell * 0.5f);
        for (const JunctionItem& item : junction.lane(travel)) {
            const float t = cell * static_cast<float>(item.elapsed) / static_cast<float>(kJunctionTicks);
            DrawItem(draw, ImVec2(back.x + d.x * t, back.y + d.y * t), std::max(1.5f, cell * 0.16f), item.kind);
        }
    }
}

// The item a router holds, in the middle of its tile.
void DrawRouterItem(ImDrawList* draw, const Router& router, ImVec2 min, float cell) {
    if (const std::optional<ItemKind> held = router.get_held())
        DrawItem(draw, ImVec2(min.x + cell * 0.5f, min.y + cell * 0.5f), std::max(1.5f, cell * 0.16f), *held);
}

// A sorter's filter item, in the middle of its tile.
void DrawSorterFilter(ImDrawList* draw, const Sorter& sorter, ImVec2 min, float cell) {
    if (const std::optional<ItemKind> filter = sorter.get_filter())
        DrawItem(draw, ImVec2(min.x + cell * 0.5f, min.y + cell * 0.5f), std::max(1.5f, cell * 0.2f), *filter);
}

// ---------------------------------------------------------------------------
// Icons in the panels
// ---------------------------------------------------------------------------

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

Icon ItemIcon(ItemKind kind) {
    Icon icon;
    icon.type = Icon::Type::Item;
    icon.item = kind;
    return icon;
}

// A building as it looks built (the bridge has no item kind).
Icon BuildingIcon(const Building& b) {
    Icon icon;
    icon.type = Icon::Type::Bridge;
    if (const std::optional<ItemKind> kind = b.item_kind()) {
        icon.type = Icon::Type::Building;
        icon.item = *kind;
    }
    return icon;
}

// What a recipe is shown as: what it makes or, for the recipes named after
// what they do, what goes in.
Icon RecipeIcon(const Recipe& r) {
    if (!r.name && !r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    if (!r.inputs.empty())
        return ItemIcon(r.inputs.front().kind);
    if (!r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    return {};
}

const char* IconName(const Icon& icon) {
    switch (icon.type) {
    case Icon::Type::None:     return "None";
    case Icon::Type::Item:
    case Icon::Type::Building: return item_name(icon.item);
    case Icon::Type::Bridge:   return "Bridge";
    }
    return "?";
}

const char* CategoryName(ItemCategory category) {
    switch (category) {
    case ItemCategory::Placeable:     return "Buildings";
    case ItemCategory::RawResource:   return "Raw resources";
    case ItemCategory::Material:      return "Materials";
    case ItemCategory::Component:     return "Components";
    }
    return "?";
}

// `sprite` as large as fits in `min`..`max` without stretching, centered.
void DrawSpriteFitted(ImDrawList* draw, const sprites::Sprite& sprite, ImVec2 min, ImVec2 max, float alpha) {
    const float k = std::min((max.x - min.x) / static_cast<float>(sprite.w),
                             (max.y - min.y) / static_cast<float>(sprite.h));
    const ImVec2 size(static_cast<float>(sprite.w) * k, static_cast<float>(sprite.h) * k);
    const ImVec2 a((min.x + max.x - size.x) * 0.5f, (min.y + max.y - size.y) * 0.5f);
    DrawSprite(draw, sprite, a, ImVec2(a.x + size.x, a.y + size.y), Direction::North, alpha);
}

// An icon filling the square `min`..`max`: its sprite, or a stand-in when
// that is missing.
void DrawIcon(ImDrawList* draw, const Icon& icon, ImVec2 min, ImVec2 max, float alpha = 1.0f) {
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float r = (max.x - min.x) * 0.5f;
    switch (icon.type) {
    case Icon::Type::None: {
        // A crossed-out circle.
        const ImU32 color = theme::U32(theme::kTextDim, alpha);
        const float t = std::max(1.5f, r * 0.12f);
        draw->AddCircle(c, r * 0.6f, color, 0, t);
        draw->AddLine(ImVec2(c.x - r * 0.42f, c.y + r * 0.42f), ImVec2(c.x + r * 0.42f, c.y - r * 0.42f), color, t);
        return;
    }
    case Icon::Type::Item:
        if (const sprites::Sprite* s = sprites::Item(icon.item))
            DrawSprite(draw, *s, min, max, Direction::North, alpha);
        else
            DrawItem(draw, c, r * 0.6f, icon.item);
        return;
    case Icon::Type::Building:
        if (const sprites::Sprite* s = sprites::Building(icon.item))
            DrawSpriteFitted(draw, *s, min, max, alpha);
        else
            DrawIcon(draw, ItemIcon(icon.item), min, max, alpha);
        return;
    case Icon::Type::Bridge:
        if (const sprites::Sprite* s = sprites::Bridge())
            DrawSpriteFitted(draw, *s, min, max, alpha);
        else
            draw->AddRectFilled(min, max, theme::U32(theme::kTileBridge, alpha), r * 0.3f);
        return;
    }
}

// Small labels over an icon's corners (null: none).
struct Badges {
    const char* top_left = nullptr;
    const char* bottom_right = nullptr;
};

// A square tile `size` across at `min`: `bg` with bevelled edges (an amber
// border when `selected`), the icon over it (faded by `alpha`) and its
// badges, in mono. Disabled widgets fade all of it.
void DrawIconFrame(ImDrawList* draw, const Icon& icon, ImVec2 min, float size, ImVec4 bg, Badges badges,
                   float alpha, bool selected = false) {
    const float style_alpha = ImGui::GetStyle().Alpha;
    const ImVec2 max(min.x + size, min.y + size);
    draw->AddRectFilled(min, max, theme::U32(bg, style_alpha));
    draw->AddLine(min, ImVec2(max.x - 1.0f, min.y), theme::U32(theme::kBevelLight, style_alpha));
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), theme::U32(theme::kBevelDark, style_alpha));
    if (selected)
        draw->AddRect(min, max, theme::U32(theme::kAccent, style_alpha), 0.0f, 0, 2.0f);
    const float pad = size * 0.08f;
    DrawIcon(draw, icon, ImVec2(min.x + pad, min.y + pad), ImVec2(max.x - pad, max.y - pad), style_alpha * alpha);
    if (!badges.top_left && !badges.bottom_right)
        return;
    theme::PushMono(theme::kFontSmall);
    const ImU32 ink = theme::U32(theme::kText, style_alpha);
    const ImU32 halo = theme::U32(theme::kBevelDark, style_alpha);
    if (badges.top_left)
        theme::DrawHaloText(draw, ImVec2(min.x + 3.0f, min.y + 1.0f), theme::U32(theme::kAccent, style_alpha), halo,
                            badges.top_left);
    if (badges.bottom_right) {
        const ImVec2 ts = ImGui::CalcTextSize(badges.bottom_right);
        theme::DrawHaloText(draw, ImVec2(max.x - ts.x - 3.0f, max.y - ts.y - 1.0f), ink, halo, badges.bottom_right);
    }
    theme::PopMono();
}

// A square button `size` across showing `icon`, outlined amber when
// selected, with `badges` and the icon faded by `fade`. Returns true when
// clicked.
bool IconButton(const char* id, const Icon& icon, float size, bool selected, Badges badges = {},
                float fade = 1.0f) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec4 bg = ImGui::IsItemActive() ? theme::kFrameActive
                      : hovered || selected ? theme::kFrameHover
                                            : theme::kFrame;
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, bg, badges, fade, selected);
    return pressed;
}

// An icon framed like IconButton, only to look at (and hover).
void IconTile(const Icon& icon, float size, Badges badges = {}) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, theme::kFrame, badges, 1.0f);
}

// An unframed icon `size` across; hovering it shows its name.
void IconImage(const Icon& icon, float size) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIcon(ImGui::GetWindowDrawList(), icon, min, ImVec2(min.x + size, min.y + size), ImGui::GetStyle().Alpha);
    ImGui::SetItemTooltip("%s", IconName(icon));
}

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
std::string ShortCount(int n) {
    if (n < 10'000)
        return std::to_string(n);
    if (n < 10'000'000)
        return std::to_string(n / 1'000) + "k";
    return std::to_string(n / 1'000'000) + "M";
}

std::string Fraction(int have, int need) {
    return std::to_string(have) + " / " + std::to_string(need);
}

// One line about an amount of an item: its icon, then `text` ("3",
// "2 / 4") and `suffix`, in `color`. Hovering the icon shows its name;
// `named` writes the name out after the text too, for tooltips, where
// nothing can be hovered.
void AmountLine(const Icon& icon, const std::string& text, ImVec4 color, bool named, const char* suffix = "") {
    ImGui::AlignTextToFramePadding();
    IconImage(icon, ImGui::GetFrameHeight());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(color, "%s%s%s%s", text.c_str(), named ? " " : "", named ? IconName(icon) : "", suffix);
}

// The placeable items in the build list: every one unlocked, plus any in
// the inventory and the selected one, in a fixed order, so that the first
// nine, which get the number keys, only move when a milestone unlocks more.
std::vector<ItemKind> BuildList(const Ship& ship) {
    const Progress& progress = ship.get_bridge().get_progress();
    std::vector<ItemKind> list;
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_placeable(kind) &&
            (progress.unlocked(kind) || ship.inventory().count(kind) > 0 || kind == g_build.selected))
            list.push_back(kind);
    }
    return list;
}

void RecipeDetails(const Recipe& recipe, const Inventory* inv);

// The bridge's hand-crafting recipe for `kind`, or null if it's only made
// in machines.
const Recipe* HandRecipeFor(ItemKind kind) {
    for (const Recipe& r : all_recipes())
        if (r.hand_craftable && r.outputs.front().kind == kind)
            return &r;
    return nullptr;
}

// Copying and pasting, in the left panel: what's on the clipboard, the
// tools, and while pasting, what the blueprint costs.
void DrawBlueprintSection(const Ship& ship) {
    if (!theme::CollapsingSection("Blueprints"))
        return;
    BlueprintUiState& bp = g_blueprints;
    if (bp.clipboard)
        ImGui::Text("Clipboard: %s", BlueprintSummary(*bp.clipboard).c_str());
    else
        ImGui::TextColored(theme::kTextDim, "Clipboard empty");

    const float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    if (theme::SegmentedButton("Copy", bp.tool == Tool::Copy, ImVec2(third, 0.0f))) {
        if (bp.tool == Tool::Copy) {
            LeaveBlueprintTool();
        } else {
            LeaveBlueprintTool();
            bp.tool = Tool::Copy;
        }
    }
    ImGui::SetItemTooltip("Drag a box over what to copy (%s+C)", kCtrlName);
    ImGui::SameLine();
    ImGui::BeginDisabled(!bp.clipboard && bp.tool != Tool::Paste);
    if (theme::SegmentedButton("Paste", bp.tool == Tool::Paste, ImVec2(third, 0.0f))) {
        if (bp.tool == Tool::Paste)
            LeaveBlueprintTool();
        else
            HoldBlueprint(*bp.clipboard);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Paste the clipboard (%s+V)", kCtrlName);
    ImGui::SameLine();
    if (theme::SegmentedButton("Library", bp.show_library, ImVec2(third, 0.0f)))
        bp.show_library = !bp.show_library;
    ImGui::SetItemTooltip("Saved blueprints, import and export (B)");

    if (bp.tool == Tool::Paste && bp.held) {
        ImGui::TextUnformatted(bp.held->name.empty() ? "Pasting the clipboard" : bp.held->name.c_str());
        // What it costs on bare space, marked where the bridge is short.
        const float icon = ImGui::GetFrameHeight() * 1.4f;
        IconFlow flow;
        flow.Start(icon);
        for (const ItemStack& s : BlueprintCost(*bp.held).stacks()) {
            const int have = ship.inventory().count(s.kind);
            const std::string amount = ShortCount(s.count);
            flow.Next();
            ImGui::PushID(static_cast<int>(s.kind));
            IconTile(ItemIcon(s.kind), icon, {have < s.count ? "!" : nullptr, amount.c_str()});
            ImGui::SetItemTooltip("%s: %d (%d in the bridge)", item_name(s.kind), s.count, have);
            ImGui::PopID();
        }
    }
}

// The left panel: an icon per placeable item with its count (click to
// build it, right click to hand-craft one), what is selected, its facing
// and the settings new ones get, copying and pasting, and the cargo
// (everything else in the inventory).
void DrawBuildPalette(Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawPlatePanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y));

    ImGui::BeginChild("##build_palette", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::SectionHeading("Build");

    Bridge& bridge = ship.get_bridge();
    const float icon = ImGui::GetFrameHeight() * 1.9f;
    const std::vector<ItemKind> list = BuildList(ship);
    IconFlow flow;
    flow.Start(icon);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const ItemKind kind = list[i];
        const int count = ship.inventory().count(kind);
        const std::string key = std::to_string(i + 1);
        const std::string amount = ShortCount(count);
        flow.Next();
        ImGui::PushID(static_cast<int>(kind));
        if (IconButton("##build", ItemIcon(kind), icon, g_build.selected == kind,
                       {i < 9 ? key.c_str() : nullptr, amount.c_str()}, count == 0 ? 0.4f : 1.0f))
            ToggleSelected(kind);
        const Recipe* hand = HandRecipeFor(kind);
        if (hand && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            int queued = 0;
            while (queued < n && bridge.queue_craft(*hand))
                ++queued;
            if (queued == 0)
                SetMessage(std::string("Can't hand-craft ") + item_name(kind) + " now: see its recipe");
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(item_name(kind));
            const GridSize footprint = footprint_of(kind);
            ImGui::TextColored(theme::kTextDim, "%d in the bridge, %dx%d", count, footprint.w, footprint.h);
            if (i < 9)
                ImGui::TextColored(theme::kTextDim, "Key %s", key.c_str());
            if (hand) {
                ImGui::TextColored(theme::kTextDim, "Right click: hand-craft 1 (Shift: 5)");
                RecipeDetails(*hand, &bridge.get_inventory());
            } else {
                ImGui::TextColored(theme::kTextDim, "Made in machines");
            }
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (g_build.selected) {
        ImGui::TextUnformatted(item_name(*g_build.selected));
        if (is_rotatable(*g_build.selected)) {
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "facing %s", DirectionName(g_build.facing));
        }
        const auto last = g_settings.last.find(static_cast<int>(*g_build.selected));
        if (last != g_settings.last.end())
            if (const char* name = SettingsName(last->second)) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "New ones:");
                ImGui::SameLine();
                const BuildingSettings& s = last->second;
                const Icon setting = s.recipe ? RecipeIcon(*s.recipe) : ItemIcon(s.target ? *s.target : *s.filter);
                AmountLine(setting, name, theme::kText, false);
            }
    } else {
        ImGui::TextColored(theme::kTextDim, "Nothing selected: clicks inspect");
    }

    DrawBlueprintSection(ship);

    if (theme::CollapsingSection("Cargo")) {
        const float cargo_icon = ImGui::GetFrameHeight() * 1.6f;
        IconFlow cargo;
        cargo.Start(cargo_icon);
        bool any = false;
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            const int count = ship.inventory().count(kind);
            if (is_placeable(kind) || count == 0)
                continue;
            any = true;
            const std::string amount = ShortCount(count);
            cargo.Next();
            IconTile(ItemIcon(kind), cargo_icon, {nullptr, amount.c_str()});
            ImGui::SetItemTooltip("%s: %d", item_name(kind), count);
        }
        if (!any)
            ImGui::TextColored(theme::kTextDim, "Empty");
    }
    ImGui::EndChild();
}

// What goes into a recipe and what it makes, item by item, for its tooltip.
// `inv` colors inputs by whether the bridge has them; null leaves them plain.
void RecipeDetails(const Recipe& recipe, const Inventory* inv) {
    ImGui::TextColored(theme::kTextDim, "%s, %.1f s", craft_category_name(recipe.category),
                       static_cast<double>(recipe.ticks) / static_cast<double>(kTicksPerSecond));
    for (const ItemStack& s : recipe.inputs) {
        if (inv) {
            const int have = inv->count(s.kind);
            const std::string suffix = "  (have " + std::to_string(have) + ")";
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), have >= s.count ? theme::kText : theme::kBad, true,
                       suffix.c_str());
        } else {
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, true);
        }
    }
    ImGui::TextColored(theme::kTextDim, "Makes");
    for (const ItemStack& s : recipe.outputs)
        AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, true);
}

// A recipe's tooltip, over the item just drawn: its name and details.
void RecipeTooltip(const Recipe& recipe, const Inventory* inv, const char* hint = nullptr) {
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::TextUnformatted(recipe_name(recipe));
    RecipeDetails(recipe, inv);
    if (hint)
        ImGui::TextColored(theme::kTextDim, "%s", hint);
    ImGui::EndTooltip();
}

// Which milestone unlocks a recipe not unlocked yet ("Unlocks with
// milestone 2: Heavy industry"), for its tooltip; empty once it is.
std::string LockedHint(const Progress& progress, const Recipe& recipe) {
    if (progress.unlocked(recipe))
        return {};
    const std::size_t i = *milestone_unlocking(recipe.outputs.front().kind);
    return "Unlocks with milestone " + std::to_string(i + 1) + ": " + all_milestones()[i].name;
}

// The corner badge on a locked recipe's icon ("M2"); empty once unlocked.
std::string LockedBadge(const Progress& progress, const Recipe& recipe) {
    if (progress.unlocked(recipe))
        return {};
    return "M" + std::to_string(*milestone_unlocking(recipe.outputs.front().kind) + 1);
}

// One icon button per hand-craftable recipe whose main output is in
// `category`; the locked ones faded, with the milestone that unlocks them.
void DrawHandCraftButtons(Bridge& bridge, ItemCategory category) {
    const float size = ImGui::GetFrameHeight() * 1.9f;
    IconFlow flow;
    flow.Start(size);
    const std::vector<Recipe>& recipes = all_recipes();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const Recipe& r = recipes[i];
        if (!r.hand_craftable || item_category(r.outputs.front().kind) != category)
            continue;
        const bool craftable = bridge.can_hand_craft(r);
        const int makes = r.outputs.front().count;
        const std::string badge = "x" + std::to_string(makes);
        const std::string locked = LockedBadge(bridge.get_progress(), r);
        flow.Next();
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!craftable);
        if (IconButton("##craft", RecipeIcon(r), size, false,
                       {locked.empty() ? nullptr : locked.c_str(), makes > 1 ? badge.c_str() : nullptr},
                       locked.empty() ? 1.0f : 0.35f)) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            for (int k = 0; k < n && bridge.queue_craft(r); ++k) {
            }
        }
        ImGui::EndDisabled();
        const std::string hint = LockedHint(bridge.get_progress(), r);
        RecipeTooltip(r, &bridge.get_inventory(), hint.empty() ? nullptr : hint.c_str());
        ImGui::PopID();
    }
}

// What a building is and what it's doing: for the grid's hover tooltip
// (`in_tooltip`, which writes item names out) and the inspector (where
// hovering an icon shows its name).
void BuildingDetails(const Building& b, bool in_tooltip) {
    const std::optional<ItemKind> kind = b.item_kind();
    // Its picture, and its name beside it.
    const float picture = ImGui::GetFrameHeight() * 1.6f;
    const ImVec2 picture_min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(picture, picture));
    DrawIcon(ImGui::GetWindowDrawList(), BuildingIcon(b), picture_min,
             ImVec2(picture_min.x + picture, picture_min.y + picture));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (picture - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextUnformatted(kind ? item_name(*kind) : "Bridge");
    ImGui::TextColored(b.damaged() ? theme::kBad : theme::kTextDim, "Health %d / %d", b.get_hp(), b.max_hp());

    const float bar_w = ImGui::GetFrameHeight() * 8.0f;
    if (const auto* machine = dynamic_cast<const Machine*>(&b)) {
        std::string crafts;
        for (int c = 0; c < kCraftCategoryCount; ++c)
            if (machine->crafts(static_cast<CraftCategory>(c)))
                crafts += std::string(crafts.empty() ? "" : ", ") + craft_category_name(static_cast<CraftCategory>(c));
        ImGui::TextColored(theme::kTextDim, "%s, speed x%d", crafts.c_str(), machine->crafting_speed());
        const Recipe* recipe = machine->get_recipe();
        if (!recipe) {
            ImGui::TextColored(theme::kWarnText, "No recipe: click it to pick one");
        } else {
            char rate[32];
            std::snprintf(rate, sizeof(rate), "  (%.1f / min)", machine->crafts_per_minute());
            AmountLine(RecipeIcon(*recipe), recipe_name(*recipe), theme::kText, false, rate);
            ImGui::ProgressBar(machine->craft_progress(), ImVec2(bar_w, 0.0f), machine->crafting() ? "" : "Waiting");
            ImGui::TextColored(theme::kTextDim, "Ingredients (per craft)");
            for (const ItemStack& s : recipe->inputs) {
                const int have = machine->get_input().count(s.kind);
                AmountLine(ItemIcon(s.kind), Fraction(have, s.count), have >= s.count ? theme::kText : theme::kTextDim,
                           in_tooltip);
            }
            ImGui::TextColored(theme::kTextDim, "Waiting to go out");
            bool any = false;
            for (const ItemStack& s : machine->get_output().stacks()) {
                AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, in_tooltip);
                any = true;
            }
            if (!any)
                ImGui::TextColored(theme::kTextDim, "  -");
        }
    }
    if (const auto* collector = dynamic_cast<const Collector*>(&b)) {
        const CollectorSpec& spec = collector->get_spec();
        const double seconds = static_cast<double>(spec.interval) / static_cast<double>(kTicksPerSecond);
        if (collector->get_yield() == 0) {
            ImGui::TextColored(theme::kWarnText, "No %s in this sector: aim it at something else",
                               item_name(collector->get_target()));
        } else {
            ImGui::TextColored(theme::kTextDim, "Gathers %s: %.0f / min (%d%% of home)",
                               item_name(collector->get_target()), collector->rate_per_second() * 60.0,
                               collector->get_yield());
            ImGui::TextColored(theme::kTextDim, "At home: 1 every %.1f s", seconds);
        }
        const bool full = collector->full();
        ImGui::ProgressBar(full ? 1.0f : collector->catch_progress(), ImVec2(bar_w, 0.0f), full ? "Buffer full" : "");
        ImGui::Text("Buffer: %d / %d", collector->stored(), collector->capacity());
        for (const ItemStack& s : collector->items().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* belt = dynamic_cast<const Conveyor*>(&b)) {
        if (!belt->moves_items()) {
            ImGui::TextColored(theme::kTextDim, "Doesn't move items yet");
        } else {
            ImGui::TextColored(theme::kTextDim, "%.0f cells / s",
                               static_cast<double>(belt->speed() * static_cast<int>(kTicksPerSecond)) / kBeltLength);
            Inventory carried;
            for (const BeltItem& item : belt->get_items())
                carried.add(item.kind);
            for (const ItemStack& s : carried.stacks())
                AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
        }
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        const std::optional<ItemKind> filter = sorter->get_filter();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        AmountLine(filter ? ItemIcon(*filter) : Icon{}, filter ? item_name(*filter) : "none", theme::kText, false);
        if (sorter->inverted())
            ImGui::TextColored(theme::kTextDim, "Matching items turn left or right;\nothers go straight through");
        else
            ImGui::TextColored(theme::kTextDim, "Matching items go straight through;\nothers turn left or right");
    }
    if (const auto* junction = dynamic_cast<const Junction*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Items cross straight over, out the\nopposite side");
        Inventory crossing;
        for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
            for (const JunctionItem& item : junction->lane(travel))
                crossing.add(item.kind);
        for (const ItemStack& s : crossing.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (const auto* router = dynamic_cast<const Router*>(&b)) {
        ImGui::TextColored(theme::kTextDim, "Passes items to its other three sides\nin turn");
        if (const std::optional<ItemKind> held = router->get_held())
            AmountLine(ItemIcon(*held), item_name(*held), theme::kText, in_tooltip);
    }
    if (const auto* thruster = dynamic_cast<const Thruster*>(&b)) {
        const std::string fuel = Fraction(thruster->get_fuel(), kThrusterCharge) + " fuel cells";
        ImGui::ProgressBar(static_cast<float>(thruster->get_fuel()) / static_cast<float>(kThrusterCharge),
                           ImVec2(bar_w, 0.0f), fuel.c_str());
        if (thruster->offline())
            ImGui::TextColored(theme::kBad, "Knocked offline: it takes no fuel until it mends");
        else
            ImGui::TextColored(thruster->charged() ? theme::kGood : theme::kTextDim, "%s",
                               thruster->charged() ? "Charged: ready to jump (see the map, M)"
                                                   : "Belt fuel cells into it to charge it");
        ImGui::TextColored(theme::kTextDim, "Part of the ship's layout: it can't be removed");
    }
    if (kind == ItemKind::SurveyScanner)
        ImGui::TextColored(theme::kTextDim, "Surveys the sectors next to the ship's");
    if (const auto* turret = dynamic_cast<const Turret*>(&b)) {
        const TurretSpec& spec = turret->get_spec();
        ImGui::TextColored(theme::kTextDim, "%d damage, %.1f shots / s, range %.0f", spec.damage,
                           static_cast<double>(kTicksPerSecond) / static_cast<double>(spec.interval),
                           static_cast<double>(spec.range));
        const std::string held = Fraction(turret->get_ammo(), kTurretAmmoCapacity);
        AmountLine(ItemIcon(spec.ammo), held, turret->get_ammo() > 0 ? theme::kText : theme::kBad, in_tooltip);
        if (turret->shots_left() == 0)
            ImGui::TextColored(theme::kBad, "Out of ammunition: belt %s into it", item_name(spec.ammo));
        else
            ImGui::TextColored(theme::kTextDim, "%d shots left", turret->shots_left());
    }
    if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
        ImGui::Text("Stored: %d / %d", hold->get_stored().total(), kCargoCapacity);
        for (const ItemStack& s : hold->get_stored().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kText, in_tooltip);
    }
    if (!kind)
        ImGui::TextColored(theme::kTextDim, "Belts deliver into its inventory");
    if (b.rotatable())
        ImGui::TextColored(theme::kTextDim, "Facing %s", DirectionName(b.facing()));
}

// Whether `text` contains `part`, ignoring case.
// `s` without whitespace at either end.
std::string Trimmed(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

bool ContainsIgnoringCase(std::string_view text, std::string_view part) {
    const auto same = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    return part.empty() || std::search(text.begin(), text.end(), part.begin(), part.end(), same) != text.end();
}

// A combo box whose closed frame shows `icon` and `text` as its value. Same
// contract as ImGui::BeginCombo (EndCombo() when it returns true).
bool BeginIconCombo(const char* id, const Icon& icon, const char* text) {
    // The frame is drawn empty, then the value over it, in this window's
    // draw list (once open, the popup is the current window).
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float w = ImGui::CalcItemWidth();
    const bool open = ImGui::BeginCombo(id, "", ImGuiComboFlags_HeightLargest);
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const float s = fh - 4.0f;
    draw->PushClipRect(min, ImVec2(min.x + w - fh, min.y + fh), true);  // short of the arrow
    const ImVec2 icon_min(min.x + 2.0f, min.y + 2.0f);
    DrawIcon(draw, icon, icon_min, ImVec2(icon_min.x + s, icon_min.y + s));
    draw->AddText(ImVec2(icon_min.x + s + ImGui::GetStyle().ItemInnerSpacing.x, min.y + pad.y),
                  ImGui::GetColorU32(ImGuiCol_Text), text);
    draw->PopClipRect();
    return open;
}

// Search for a combo's options: a box above them, emptied and focused as the
// popup opens, that hides the options not containing what's typed (ignoring
// case); once something is typed, Enter picks the first one left. The
// options are icon buttons in rows. Only one combo is open at a time, so they
// all share this.
struct ComboSearch {
    char text[64] = {};
    bool enter = false;  // Enter went down in the box this frame
    int shown = 0;       // options shown so far this frame
    IconFlow flow;       // lays the options out
};
ComboSearch g_combo_search;

// Call first thing once BeginIconCombo() returns true, and EndComboSearch()
// before EndCombo(). The options in between scroll under the box.
void BeginComboSearch() {
    ComboSearch& search = g_combo_search;
    if (ImGui::IsWindowAppearing()) {
        search.text[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    // Enter with nothing typed picks nothing (the first option is "None").
    search.enter = ImGui::InputTextWithHint("##search", "Search", search.text, sizeof(search.text),
                                            ImGuiInputTextFlags_EnterReturnsTrue) &&
                   search.text[0] != '\0';
    search.shown = 0;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                        ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 14.0f));
    ImGui::BeginChild("##options", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY);
    search.flow.Start(ImGui::GetFrameHeight() * 1.6f);
}

// Whether the option `label` matches what's typed.
bool ComboSearchMatches(const char* label) {
    return ContainsIgnoringCase(label, g_combo_search.text);
}

// Whether the option `label` matches the search. Each one that does must
// then be drawn with ComboSearchOption().
bool ComboSearchShows(const char* label) {
    if (!ComboSearchMatches(label))
        return false;
    ++g_combo_search.shown;
    g_combo_search.flow.Next();
    return true;
}

// A heading over the options after it, which start a new row.
void ComboSearchHeading(const char* text) {
    ImGui::SeparatorText(text);
    g_combo_search.flow.Start(g_combo_search.flow.size);
}

// An option the search shows, as an icon button: true, closing the combo,
// when it is clicked, or when it is the first one and Enter was pressed.
bool ComboSearchOption(const Icon& icon, bool selected) {
    const bool picked = IconButton("##option", icon, g_combo_search.flow.size, selected) ||
                        (g_combo_search.enter && g_combo_search.shown == 1);
    // The options' child window isn't the popup, so the button won't close it.
    if (picked)
        ImGui::CloseCurrentPopup();
    return picked;
}

void EndComboSearch() {
    if (g_combo_search.shown == 0)
        ImGui::TextColored(theme::kTextDim, "No matches");
    ImGui::EndChild();
}

// Every item as a search option, by category, each under a heading; the
// one picked, if any.
std::optional<ItemKind> ComboSearchItemOptions(std::optional<ItemKind> current) {
    std::optional<ItemKind> picked;
    for (const ItemCategory category : {ItemCategory::Placeable, ItemCategory::RawResource,
                                        ItemCategory::Material, ItemCategory::Component}) {
        bool any = false;
        for (int i = 0; i < kItemKindCount && !any; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            any = item_category(kind) == category && !is_layout_part(kind) && ComboSearchMatches(item_name(kind));
        }
        if (!any)
            continue;
        ComboSearchHeading(CategoryName(category));
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            if (item_category(kind) != category || is_layout_part(kind) || !ComboSearchShows(item_name(kind)))
                continue;
            ImGui::PushID(i);
            if (ComboSearchOption(ItemIcon(kind), current == kind))
                picked = kind;
            ImGui::SetItemTooltip("%s", item_name(kind));
            ImGui::PopID();
        }
    }
    return picked;
}

// The inspected building (clicked on the grid): its details, and for a
// machine, the recipe picker; for a collector, what it gathers; for a
// sorter, the filter picker.
void DrawInspector(Ship& ship) {
    const Building* b = InspectedBuilding(ship);
    if (!b)
        return;
    // The heading, with a cross at its right end to close it.
    const ImVec2 heading_min = ImGui::GetCursorScreenPos();
    const float heading_w = ImGui::GetContentRegionAvail().x;
    theme::SectionHeading("Inspect");
    const ImVec2 after_heading = ImGui::GetCursorScreenPos();
    const float s = ImGui::GetTextLineHeight();
    ImGui::SetCursorScreenPos(ImVec2(heading_min.x + heading_w - s, heading_min.y + ImGui::GetStyle().ItemSpacing.y +
                                                                        (ImGui::GetFrameHeight() - s) * 0.5f));
    if (theme::CloseButton("##close_inspector")) {
        g_build.inspected.reset();
        g_build.inspected_building = nullptr;
        return;
    }
    ImGui::SetItemTooltip("Close (Esc)");
    ImGui::SetCursorScreenPos(after_heading);
    // Whatever is picked below is what new ones of its kind start with.
    const auto remember = [&] {
        if (const Building* changed = ship.building_at(*g_build.inspected))
            if (const std::optional<BuildingSettings> settings = SettingsOf(*changed))
                RememberSettings(*settings);
    };
    BuildingDetails(*b, false);
    if (const auto* machine = dynamic_cast<const Machine*>(b)) {
        const Progress& progress = ship.get_bridge().get_progress();
        const Recipe* current = machine->get_recipe();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##recipe", current ? RecipeIcon(*current) : Icon{},
                           current ? recipe_name(*current) : "Pick a recipe")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, current == nullptr) && ship.set_recipe(*g_build.inspected, nullptr))
                    remember();
                ImGui::SetItemTooltip("None");
            }
            const std::vector<const Recipe*> recipes = machine_recipes(*machine->item_kind());
            for (std::size_t i = 0; i < recipes.size(); ++i) {
                const Recipe* r = recipes[i];
                if (!ComboSearchShows(recipe_name(*r)))
                    continue;
                ImGui::PushID(static_cast<int>(i));
                const std::string hint = LockedHint(progress, *r);
                ImGui::BeginDisabled(!hint.empty());
                if (ComboSearchOption(RecipeIcon(*r), r == current) && r != current &&
                    ship.set_recipe(*g_build.inspected, r))
                    remember();
                ImGui::EndDisabled();
                RecipeTooltip(*r, nullptr, hint.empty() ? nullptr : hint.c_str());
                ImGui::PopID();
            }
            EndComboSearch();
            ImGui::EndCombo();
        }
        ImGui::TextColored(theme::kTextDim, "Changing it returns held ingredients to the bridge.");
    }
    if (const auto* collector = dynamic_cast<const Collector*>(b)) {
        const ItemKind current = collector->get_target();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##target", ItemIcon(current), item_name(current))) {
            BeginComboSearch();
            for (const ItemKind kind : collector->get_spec().targets) {
                if (!ComboSearchShows(item_name(kind)))
                    continue;
                ImGui::PushID(static_cast<int>(kind));
                if (ComboSearchOption(ItemIcon(kind), kind == current) && kind != current &&
                    ship.set_collector_target(*g_build.inspected, kind))
                    remember();
                ImGui::SetItemTooltip("%s", item_name(kind));
                ImGui::PopID();
            }
            EndComboSearch();
            ImGui::EndCombo();
        }
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(b)) {
        const std::optional<ItemKind> current = sorter->get_filter();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##filter", current ? ItemIcon(*current) : Icon{},
                           current ? item_name(*current) : "Pick a filter")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, !current) && ship.set_sort_filter(*g_build.inspected, std::nullopt))
                    remember();
                ImGui::SetItemTooltip("None");
            }
            if (const std::optional<ItemKind> picked = ComboSearchItemOptions(current))
                if (ship.set_sort_filter(*g_build.inspected, *picked))
                    remember();
            EndComboSearch();
            ImGui::EndCombo();
        }
    }
}

// The milestone being worked on: what to deliver, how much has been, and
// what it unlocks.
void DrawMilestone(const Progress& progress) {
    if (!theme::CollapsingSection("Milestone"))
        return;
    const Milestone* m = progress.current_milestone();
    if (!m) {
        ImGui::TextColored(theme::kGood, "All milestones complete");
        return;
    }
    ImGui::Text("%zu / %zu: %s", progress.current_index() + 1, all_milestones().size(), m->name);
    ImGui::TextColored(theme::kTextDim, "Deliver to the bridge:");
    for (const ItemStack& s : m->deliver) {
        const int have = progress.delivered_count(s.kind);
        const std::string overlay = std::to_string(have) + " / " + std::to_string(s.count);
        ImGui::AlignTextToFramePadding();
        IconImage(ItemIcon(s.kind), ImGui::GetFrameHeight());
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::ProgressBar(static_cast<float>(have) / static_cast<float>(s.count), ImVec2(-1.0f, 0.0f),
                           overlay.c_str());
    }
    ImGui::TextColored(theme::kTextDim, "Unlocks");
    const float icon = ImGui::GetFrameHeight() * 1.4f;
    IconFlow flow;
    flow.Start(icon);
    for (const ItemKind kind : m->unlocks) {
        flow.Next();
        IconTile(ItemIcon(kind), icon);
        ImGui::SetItemTooltip("%s", item_name(kind));
    }
}

// The tabs over the grid, one per ship: a click shows that ship, a
// double-click renames it.
void DrawFleetStrip(World& world) {
    Fleet& fleet = world.get_fleet();
    FleetUiState& f = g_fleet;
    if (theme::SegmentedButton("Map", g_map.shown))
        g_map.shown = !g_map.shown;
    ImGui::SetItemTooltip("The star map (M): sectors, and jumping between them");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    const float fh = ImGui::GetFrameHeight();
    std::optional<int> show;
    bool first = true;
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships()) {
        if (!first)
            ImGui::SameLine();
        first = false;
        const Sector& sector = world.get_map().at(s->sector);
        ImGui::PushID(s->id);
        if (f.renaming == s->id) {
            if (f.focus_rename) {
                ImGui::SetKeyboardFocusHere();
                f.focus_rename = false;
            }
            ImGui::SetNextItemWidth(fh * 8.0f);
            const bool done = ImGui::InputText("##rename", f.rename, sizeof f.rename,
                                               ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (done || ImGui::IsItemDeactivated()) {
                if (!Trimmed(f.rename).empty())
                    fleet.rename(s->id, Trimmed(f.rename));
                f.renaming.reset();
            }
        } else {
            const bool attacked = s->battle.under_attack();
            const std::string label = s->name + "  \xc2\xb7  " + sector.name + (attacked ? "  (!)" : "");
            if (attacked && s->id != f.current)
                ImGui::PushStyleColor(ImGuiCol_Text, theme::kBad);
            if (theme::SegmentedButton(label.c_str(), s->id == f.current))
                show = s->id;
            if (attacked && s->id != f.current)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                f.renaming = s->id;
                std::snprintf(f.rename, sizeof f.rename, "%s", s->name.c_str());
                f.focus_rename = true;
            }
            const ShipLayout* layout = s->ship.get_layout();
            ImGui::SetItemTooltip("%s, a %s, in %s%s\nDouble-click to rename", s->name.c_str(),
                                  layout ? layout->name : "ship", sector.name.c_str(),
                                  attacked ? "\nUnder attack!" : "");
        }
        ImGui::PopID();
    }
    if (fleet.get_ships().size() > 1) {
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "[ ] switch ships");
    }
    if (show)
        SwitchToShip(*show);
}

// The ship before or after the one shown (`step` -1 or 1), going round.
void CycleShip(const Fleet& fleet, int step) {
    const auto& ships = fleet.get_ships();
    const auto n = static_cast<int>(ships.size());
    for (int i = 0; i < n; ++i)
        if (ships[static_cast<std::size_t>(i)]->id == g_fleet.current) {
            SwitchToShip(ships[static_cast<std::size_t>((i + step + n) % n)]->id);
            return;
        }
}

// The fleet, in the right panel: building a new ship, sending cargo, and
// the cargo on its way to or from the ship shown.
void DrawFleetSection(World& world, const FleetShip& current) {
    Fleet& fleet = world.get_fleet();
    if (!theme::CollapsingSection("Fleet"))
        return;
    if (ImGui::Button("Build a new ship...", ImVec2(-1.0f, 0.0f)))
        g_fleet.show_build = true;
    ImGui::SetItemTooltip("Pick a layout; it takes a ship starter pack and the layout's cost from this bridge");
    if (ImGui::Button("Send cargo...", ImVec2(-1.0f, 0.0f)))
        g_fleet.show_send = true;
    ImGui::SetItemTooltip("Send items from this bridge to another ship's, in cargo pods (%d items a pod)",
                          kPodCapacity);

    const Tick now = world.get_tick();
    for (const Transfer& t : fleet.in_flight()) {
        if (t.from != current.id && t.to != current.id)
            continue;
        const FleetShip* other = fleet.find(t.from == current.id ? t.to : t.from);
        int n = 0;
        for (const ItemStack& item : t.items)
            n += item.count;
        const double seconds = static_cast<double>(t.arrives - now) / static_cast<double>(kTicksPerSecond);
        ImGui::TextColored(theme::kTextDim, "%s %s: %d items, %.0f s", t.from == current.id ? "To" : "From",
                           other ? other->name.c_str() : "(lost)", n, std::ceil(seconds));
    }
}

// The Send cargo window: from the ship shown to another in its sector, the
// items picked (from its bridge inventory) and the pods they take.
void DrawSendCargoWindow(World& world, FleetShip& current) {
    FleetUiState& f = g_fleet;
    if (!f.show_send)
        return;
    Fleet& fleet = world.get_fleet();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 18.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Send cargo", &f.show_send, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const Inventory& inv = current.ship.inventory();
    ImGui::Text("From %s", current.name.c_str());

    // Where to: another ship in the same sector.
    const FleetShip* to = fleet.find(f.send_to);
    if (to && (to->id == current.id || to->sector != current.sector))
        to = nullptr;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##send_to", to ? to->name.c_str() : "Pick a ship")) {
        bool any = false;
        for (const std::unique_ptr<FleetShip>& s : fleet.get_ships()) {
            if (s->id == current.id || s->sector != current.sector)
                continue;
            any = true;
            ImGui::PushID(s->id);
            if (ImGui::Selectable(s->name.c_str(), to == s.get()))
                f.send_to = s->id;
            ImGui::PopID();
        }
        if (!any)
            ImGui::TextColored(theme::kTextDim, "No other ship in this sector");
        ImGui::EndCombo();
    }

    // What: one row per item, its count capped at what the bridge holds.
    std::optional<std::size_t> remove;
    int total = 0;
    for (std::size_t i = 0; i < f.cargo.size(); ++i) {
        ItemStack& stack = f.cargo[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        IconImage(ItemIcon(stack.kind), fh);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(fh * 6.0f);
        ImGui::InputInt("##count", &stack.count, 1, 10);
        stack.count = std::clamp(stack.count, 0, inv.count(stack.kind));
        ImGui::SameLine();
        ImGui::TextColored(theme::kTextDim, "/ %d", inv.count(stack.kind));
        ImGui::SameLine();
        if (theme::DangerSmallButton("Remove"))
            remove = i;
        ImGui::PopID();
        total += stack.count;
    }
    if (remove)
        f.cargo.erase(f.cargo.begin() + static_cast<std::ptrdiff_t>(*remove));

    ImGui::SetNextItemWidth(-1.0f);
    if (BeginIconCombo("##add_cargo", Icon{}, "Add an item")) {
        BeginComboSearch();
        for (const ItemStack& held : inv.stacks()) {
            const bool listed = std::any_of(f.cargo.begin(), f.cargo.end(),
                                            [&](const ItemStack& c) { return c.kind == held.kind; });
            if (listed || !ComboSearchShows(item_name(held.kind)))
                continue;
            ImGui::PushID(static_cast<int>(held.kind));
            if (ComboSearchOption(ItemIcon(held.kind), false))
                f.cargo.push_back({held.kind, std::min(held.count, kPodCapacity)});
            ImGui::SetItemTooltip("%s (%d)", item_name(held.kind), held.count);
            ImGui::PopID();
        }
        EndComboSearch();
        ImGui::EndCombo();
    }

    const int pods = pods_needed(total);
    const int have_pods = inv.count(ItemKind::CargoPod);
    AmountLine(ItemIcon(ItemKind::CargoPod), Fraction(have_pods, pods),
               have_pods >= pods ? theme::kText : theme::kBad, false, " cargo pods needed");
    const FleetResult result =
        to ? fleet.can_send(current.id, to->id, f.cargo) : FleetResult::NoSuchShip;
    ImGui::BeginDisabled(result != FleetResult::Ok);
    if (theme::PrimaryButton("Send", ImVec2(-1.0f, 0.0f))) {
        fleet.send(current.id, to->id, f.cargo, world.get_tick());
        SetMessage("Cargo on its way to " + to->name, theme::kGood);
        f.cargo.clear();
    }
    ImGui::EndDisabled();
    if (result != FleetResult::Ok)
        ImGui::TextColored(theme::kTextDim, "%s", to ? describe(result) : "Pick a ship to send to");
    ImGui::End();
}

// A small picture of `layout` filling `size` from the cursor: its hull, its
// bridge and its engines.
void DrawLayoutPreview(const ShipLayout& layout, ImVec2 size) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), theme::U32(theme::kInset));
    const GridSize bounds = layout_bounds(layout);
    const float cell = std::min(size.x / static_cast<float>(bounds.w + 2), size.y / static_cast<float>(bounds.h + 2));
    const ImVec2 origin(min.x + (size.x - cell * static_cast<float>(bounds.w)) * 0.5f,
                        min.y + (size.y - cell * static_cast<float>(bounds.h)) * 0.5f);
    const auto box = [&](GridPos at, GridSize s, ImVec4 color) {
        const ImVec2 a(origin.x + static_cast<float>(at.x) * cell, origin.y + static_cast<float>(at.y) * cell);
        draw->AddRectFilled(a, ImVec2(a.x + static_cast<float>(s.w) * cell, a.y + static_cast<float>(s.h) * cell),
                            theme::U32(color));
    };
    for (const HullRect& r : layout.hull)
        box(r.at, r.size, theme::kTileHull);
    box(layout.bridge, {kBridgeSize, kBridgeSize}, theme::kTileBridge);
    for (const GridPos e : layout.engines)
        box(e, footprint_of(ItemKind::IonThruster), theme::kTileSystem);
}

// The Build a new ship window: every layout, the one picked drawn with what
// it costs, and building it from the bridge of the ship shown.
void DrawBuildShipWindow(World& world, FleetShip& current) {
    FleetUiState& f = g_fleet;
    if (!f.show_build)
        return;
    Fleet& fleet = world.get_fleet();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 20.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Build a new ship", &f.show_build, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    const std::vector<ShipLayout>& layouts = all_layouts();
    const Progress& progress = fleet.get_progress();
    f.build_layout = std::min(f.build_layout, layouts.size() - 1);
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        const ShipLayout& l = layouts[i];
        const bool unlocked = layout_unlocked(l, progress);
        ImGui::PushID(static_cast<int>(i));
        if (!unlocked)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kTextDim);
        if (ImGui::Selectable(l.name, f.build_layout == i))
            f.build_layout = i;
        if (!unlocked) {
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(theme::kTextDim, "(unlocks at %s)", all_milestones()[*l.milestone].name);
        }
        ImGui::PopID();
    }
    ImGui::Separator();

    const ShipLayout& layout = layouts[f.build_layout];
    DrawLayoutPreview(layout, ImVec2(ImGui::GetContentRegionAvail().x, fh * 8.0f));
    const GridSize bounds = layout_bounds(layout);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "%s", layout.description);
    ImGui::PopTextWrapPos();
    ImGui::Text("%dx%d, %d hull tiles, %zu ion thrusters", bounds.w, bounds.h, layout_hull_count(layout),
                layout.engines.size());

    // What it costs, against what this bridge has.
    const Inventory& inv = current.ship.inventory();
    ImGui::TextColored(theme::kTextDim, "Costs (from %s)", current.name.c_str());
    std::vector<ItemStack> cost = layout.cost;
    cost.insert(cost.begin(), {ItemKind::ShipStarterPack, 1});
    for (const ItemStack& s : cost) {
        const int have = inv.count(s.kind);
        AmountLine(ItemIcon(s.kind), Fraction(have, s.count), have >= s.count ? theme::kText : theme::kBad, true);
    }

    const FleetResult result = fleet.can_launch(current.id, layout);
    ImGui::BeginDisabled(result != FleetResult::Ok);
    if (theme::PrimaryButton("Build", ImVec2(-1.0f, 0.0f))) {
        int id = 0;
        if (fleet.launch(current.id, layout, &id) == FleetResult::Ok) {
            SetMessage("Built " + fleet.find(id)->name + ", a " + layout.name + ": its tab is above the grid",
                       theme::kGood);
            f.show_build = false;
        }
    }
    ImGui::EndDisabled();
    if (result != FleetResult::Ok)
        ImGui::TextColored(theme::kTextDim, "%s", describe(result));
    ImGui::End();
}

// The right panel: the current milestone, the inspector, the fleet,
// hand-crafting at the bridge, its queue, and the recipe book for
// everything else.
void DrawCraftPanel(World& world, FleetShip& current, float width) {
    Ship& ship = current.ship;
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawPlatePanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y));

    ImGui::BeginChild("##craft_panel", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    Bridge& bridge = ship.get_bridge();
    // The inspector first, so a click on the grid shows up where it's seen.
    ImGui::PushTextWrapPos(0.0f);
    DrawInspector(ship);
    ImGui::PopTextWrapPos();
    DrawMilestone(bridge.get_progress());
    ImGui::PushTextWrapPos(0.0f);
    DrawFleetSection(world, current);
    ImGui::PopTextWrapPos();

    const float icon = ImGui::GetFrameHeight() * 1.6f;
    const std::deque<CraftJob>& queue = bridge.craft_queue();
    if (!theme::CollapsingSection("Craft queue")) {
    } else if (queue.empty()) {
        ImGui::TextColored(theme::kTextDim, "Idle");
    } else {
        constexpr std::size_t kShown = 8;
        std::optional<std::size_t> cancel;
        IconFlow flow;
        flow.Start(icon);
        for (std::size_t i = 0; i < std::min(queue.size(), kShown); ++i) {
            const CraftJob& job = queue[i];
            flow.Next();
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 job_min = ImGui::GetCursorScreenPos();
            // The first one is being crafted: highlighted, with its progress
            // along the bottom, and only a right click cancels it.
            const bool clicked = IconButton("##job", RecipeIcon(*job.recipe), icon, i == 0);
            if (i == 0) {
                const float progress = static_cast<float>(job.elapsed) / static_cast<float>(job.recipe->ticks);
                const float h = std::max(3.0f, icon * 0.1f);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(job_min.x + 2.0f, job_min.y + icon - h - 2.0f),
                    ImVec2(job_min.x + 2.0f + (icon - 4.0f) * progress, job_min.y + icon - 2.0f),
                    theme::U32(theme::kAccent));
            }
            if ((clicked && i > 0) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                cancel = i;
            ImGui::SetItemTooltip("%s\n%s", recipe_name(*job.recipe),
                                  i == 0 ? "Crafting: right click to cancel and refund" : "Click to cancel and refund");
            ImGui::PopID();
        }
        if (queue.size() > kShown)
            ImGui::TextColored(theme::kTextDim, "... and %zu more", queue.size() - kShown);
        if (cancel)
            bridge.cancel_craft(*cancel);
    }

    if (theme::CollapsingSection("Hand-craft")) {
        ImGui::TextColored(theme::kTextDim, "Click: 1   Shift-click: 5");
        ImGui::SeparatorText("Materials");
        DrawHandCraftButtons(bridge, ItemCategory::Material);
        ImGui::SeparatorText("Components");
        DrawHandCraftButtons(bridge, ItemCategory::Component);
        ImGui::SeparatorText("Buildings");
        DrawHandCraftButtons(bridge, ItemCategory::Placeable);
    }

    if (!theme::CollapsingSection("Recipe book", false)) {
        ImGui::EndChild();
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kTextDim, "Made in machines: click one on the grid to pick its recipe.");
    ImGui::PopTextWrapPos();
    const std::vector<Recipe>& recipes = all_recipes();
    for (int c = 0; c < kCraftCategoryCount; ++c) {
        const CraftCategory category = static_cast<CraftCategory>(c);
        if (!ImGui::TreeNode(craft_category_name(category)))
            continue;
        IconFlow flow;
        flow.Start(icon);
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            const Recipe& r = recipes[i];
            if (r.category != category || r.hand_craftable)
                continue;
            flow.Next();
            const std::string locked = LockedBadge(bridge.get_progress(), r);
            IconTile(RecipeIcon(r), icon, {locked.empty() ? nullptr : locked.c_str()});
            const std::string hint = LockedHint(bridge.get_progress(), r);
            RecipeTooltip(r, &bridge.get_inventory(), hint.empty() ? nullptr : hint.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

// The banner over the grid when a milestone is completed: the fleet
// progress it follows, the milestone it last saw being worked on, and until
// when (ImGui::GetTime()) it shows.
struct MilestoneBanner {
    const Progress* progress = nullptr;
    std::size_t seen = 0;
    double until = 0.0;
};
MilestoneBanner g_banner;

// A banner plate centered across the grid (`grid_min`, `grid_size`): a band
// along its top in `band` (hazard stripes when `hazard`), `title` in mono
// in `color`, and `subtitle` under it. At the top of the grid, or with
// `bottom`, at its bottom. Returns its height.
float DrawBanner(const char* title, const char* subtitle, ImVec4 color, ImVec4 band, bool hazard, ImVec2 grid_min,
                 ImVec2 grid_size, bool bottom) {
    const float fh = ImGui::GetFrameHeight();
    const float pad = fh * 0.45f;
    const float band_h = std::max(4.0f, fh * 0.28f);
    theme::PushMono(theme::kFontTitle);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    theme::PopMono();
    const ImVec2 ss = ImGui::CalcTextSize(subtitle);
    const float w = std::max(ts.x, ss.x) + pad * 2.0f;
    const float h = band_h + ts.y + ss.y + pad * 2.0f;
    const float y = bottom ? grid_min.y + grid_size.y - h - fh * 0.6f : grid_min.y + fh * 0.6f;
    const ImVec2 min(grid_min.x + std::floor((grid_size.x - w) * 0.5f), y);
    const ImVec2 max(min.x + w, min.y + h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    theme::DrawPlatePanel(draw, min, max);
    const ImVec2 band_max(max.x - 2.0f, min.y + band_h);
    if (hazard) {
        draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, theme::U32(theme::kBevelDark));
        theme::DrawHazardStripes(draw, ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, band);
    } else {
        draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 1.0f), band_max, theme::U32(band));
    }
    theme::PushMono(theme::kFontTitle);
    draw->AddText(ImVec2(min.x + (w - ts.x) * 0.5f, min.y + band_h + pad * 0.8f), theme::U32(color), title);
    theme::PopMono();
    draw->AddText(ImVec2(min.x + (w - ss.x) * 0.5f, max.y - pad - ss.y), theme::U32(theme::kTextDim), subtitle);
    return h;
}

// Announces a milestone just completed, for a few seconds, at the top of
// the grid (`grid_min`, `grid_size`).
void DrawMilestoneBanner(const Progress& progress, ImVec2 grid_min, ImVec2 grid_size) {
    const std::size_t current = progress.current_index();
    MilestoneBanner& banner = g_banner;
    if (banner.progress != &progress)
        banner = {&progress, current, 0.0};  // a new or loaded game: nothing to announce
    if (current > banner.seen)
        banner.until = ImGui::GetTime() + 5.0;
    banner.seen = current;
    if (current == 0 || ImGui::GetTime() >= banner.until)
        return;

    const std::string title = std::string("MILESTONE COMPLETE: ") + all_milestones()[current - 1].name;
    const char* subtitle = progress.all_complete() ? "Every milestone done!" : "New recipes unlocked";
    DrawBanner(title.c_str(), subtitle, theme::kGood, theme::kAccent, false, grid_min, grid_size, false);
}

// The fight, along the bottom of the grid: how many enemies are on it, or
// when the next wave comes. Returns its height (0 when there's none).
float DrawBattleBanner(const FleetShip& ship, const Sector& sector, Tick now, ImVec2 grid_min, ImVec2 grid_size) {
    const Battle& battle = ship.battle;
    std::string title;
    std::string subtitle;
    ImVec4 color = theme::kBad;
    if (battle.under_attack()) {
        title = "UNDER ATTACK: " + std::to_string(battle.enemies.size()) + " enemies";
        subtitle = sector.hostile() ? std::to_string(sector.waves_left) + " more waves to come" : "The last wave";
    } else if (battle.next_wave) {
        const Tick left = *battle.next_wave > now ? *battle.next_wave - now : 0;
        title = "NEXT WAVE IN " + std::to_string((left + kTicksPerSecond - 1) / kTicksPerSecond) + " s";
        subtitle = std::to_string(sector.waves_left) + " waves left in " + sector.name;
        color = theme::kWarnText;
    } else {
        return 0.0f;
    }
    return DrawBanner(title.c_str(), subtitle.c_str(), color, battle.under_attack() ? theme::kBad : theme::kAccent,
                      true, grid_min, grid_size, true);
}

// The last message (SetMessage), for a few seconds after it's set: a toast
// centered over the bottom of the grid, `above` pixels higher (over the
// battle banner), fading out at the end.
void DrawToast(ImVec2 grid_min, ImVec2 grid_size, float above) {
    const BuildUiState& build = g_build;
    const double age = ImGui::GetTime() - build.message_time;
    if (build.message.empty() || age >= kMessageSeconds)
        return;
    const float alpha = static_cast<float>(std::min(1.0, (kMessageSeconds - age) / 0.5));
    const float fh = ImGui::GetFrameHeight();
    const float pad = fh * 0.4f;
    const float wrap = std::min(grid_size.x * 0.8f, fh * 24.0f);
    const ImVec2 ts = ImGui::CalcTextSize(build.message.c_str(), nullptr, false, wrap);
    const float stripe = std::max(3.0f, fh * 0.15f);
    const float w = ts.x + pad * 2.0f + stripe;
    const float h = ts.y + pad * 2.0f;
    const ImVec2 min(grid_min.x + std::floor((grid_size.x - w) * 0.5f),
                     grid_min.y + grid_size.y - h - fh * 0.6f - (above > 0.0f ? above + fh * 0.4f : 0.0f));
    const ImVec2 max(min.x + w, min.y + h);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(min.x + 3.0f, min.y + 3.0f), ImVec2(max.x + 3.0f, max.y + 3.0f),
                        theme::U32(theme::kShadow, 0.35f * alpha));
    draw->AddRectFilled(min, max, theme::U32(theme::kPanel, alpha));
    draw->AddRect(min, max, theme::U32(theme::kBevelLight, alpha));
    draw->AddRectFilled(min, ImVec2(min.x + stripe, max.y), theme::U32(build.message_color, alpha));
    draw->AddText(nullptr, 0.0f, ImVec2(min.x + stripe + pad, min.y + pad), theme::U32(build.message_color, alpha),
                  build.message.c_str(), nullptr, wrap);
}

// A hull cell over `min`..`max`: its sprite, or flat `hull`, also when
// zoomed out too far for the sprite to read.
void DrawHull(ImDrawList* draw, ImVec2 min, ImVec2 max, float cell, ImU32 hull) {
    const sprites::Sprite* sprite = sprites::Hull();
    if (sprite && cell >= 8.0f)
        DrawSprite(draw, *sprite, min, max, Direction::North, 1.0f);
    else
        draw->AddRectFilled(min, max, hull);
}

// Amber brackets round the corners of `min`..`max`, marking the building
// being inspected.
void DrawCornerBrackets(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 color, float cell) {
    const float len = std::max(4.0f, std::min(cell * 0.5f, (max.x - min.x) * 0.3f));
    const float t = 2.0f;
    const ImVec2 a(min.x - 2.0f, min.y - 2.0f);
    const ImVec2 b(max.x + 2.0f, max.y + 2.0f);
    draw->AddRect(min, max, theme::U32(theme::kAccent, 0.35f), 0.0f, 0, 1.0f);
    draw->AddLine(a, ImVec2(a.x + len, a.y), color, t);
    draw->AddLine(a, ImVec2(a.x, a.y + len), color, t);
    draw->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - len, a.y), color, t);
    draw->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + len), color, t);
    draw->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + len, b.y), color, t);
    draw->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - len), color, t);
    draw->AddLine(b, ImVec2(b.x - len, b.y), color, t);
    draw->AddLine(b, ImVec2(b.x, b.y - len), color, t);
}

// Pans and zooms the camera from this frame's input on the canvas (the
// InvisibleButton just submitted, `canvas_size` at `canvas_min`), keeping it
// within bounds. Returns whether the canvas is being panned.
bool UpdateCamera(int n, ImVec2 canvas_min, ImVec2 canvas_size) {
    const ImGuiIO& io = ImGui::GetIO();
    Camera& cam = g_camera;
    // Zoomed out, the whole ship fits; zoomed in, a few dozen cells do.
    const float min_cell = std::min(canvas_size.x, canvas_size.y) / static_cast<float>(n);
    const float max_cell = std::max(min_cell, ImGui::GetFrameHeight() * 3.0f);
    if (!cam.placed) {
        // Every layout is centered on the grid.
        cam.cell = std::clamp(ImGui::GetFrameHeight(), min_cell, max_cell);
        const float half = static_cast<float>(n) * 0.5f * cam.cell;
        const ImVec2 center(half, half);
        cam.offset = ImVec2(canvas_size.x * 0.5f - center.x, canvas_size.y * 0.5f - center.y);
        cam.placed = true;
    }

    // Zoom about the mouse: the point under it stays put.
    float new_cell = cam.cell;
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
        new_cell = cam.cell * std::pow(kZoomStep, io.MouseWheel);
    new_cell = std::clamp(new_cell, min_cell, max_cell);  // also after a window resize
    if (new_cell != cam.cell) {
        const ImVec2 m(io.MousePos.x - canvas_min.x, io.MousePos.y - canvas_min.y);
        const float k = new_cell / cam.cell;
        cam.offset = ImVec2(m.x - (m.x - cam.offset.x) * k, m.y - (m.y - cam.offset.y) * k);
        cam.cell = new_cell;
    }

    // Pan: middle drag, or hold Space and move the mouse, with or without a
    // button down (for trackpads). Once started over the canvas, a Space pan
    // carries on over the panels until Space comes up.
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
    if (!space)
        cam.space_pans = false;
    else if (ImGui::IsItemHovered())
        cam.space_pans = true;
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        cam.left_pans = space;
    const bool panning =
        cam.space_pans || (ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                                     (cam.left_pans && ImGui::IsMouseDown(ImGuiMouseButton_Left))));
    if (panning) {
        cam.offset.x += io.MouseDelta.x;
        cam.offset.y += io.MouseDelta.y;
    }
    if (panning)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // Keep the middle of the view over the grid.
    const float side = cam.cell * static_cast<float>(n);
    cam.offset.x = std::clamp(cam.offset.x, canvas_size.x * 0.5f - side, canvas_size.x * 0.5f);
    cam.offset.y = std::clamp(cam.offset.y, canvas_size.y * 0.5f - side, canvas_size.y * 0.5f);
    return panning;
}

// The copy tool's box, from cell `a` to cell `b` (either way round), and
// the buildings it will take, outlined (`origin`, `cell`: the grid on
// screen).
void DrawCopyBox(ImDrawList* draw, const Ship& ship, GridPos a, GridPos b, ImVec2 origin, float cell) {
    const GridPos lo{std::min(a.x, b.x), std::min(a.y, b.y)};
    const GridPos hi{std::max(a.x, b.x), std::max(a.y, b.y)};
    const ImVec2 min = CellMin(origin, cell, lo);
    const ImVec2 max = CellMin(origin, cell, {hi.x + 1, hi.y + 1});
    draw->AddRectFilled(min, max, theme::U32(theme::kAccent, 0.12f));
    for (const GridPos p : ship.placement_order()) {
        const Building* building = ship.building_anchored_at(p);
        const GridSize size = building->footprint();
        if (!building->item_kind() || p.x > hi.x || p.y > hi.y || p.x + size.w <= lo.x || p.y + size.h <= lo.y)
            continue;
        const ImVec2 bmin = CellMin(origin, cell, p);
        draw->AddRect(bmin, FootprintMax(bmin, cell, size), theme::U32(theme::kAccent, 0.7f), 0.0f, 0, 1.5f);
    }
    draw->AddRect(min, max, theme::U32(theme::kAccent), 0.0f, 0, 2.0f);
}

// A ghost of `blueprint` pasted with its top-left cell at `at`: what it would
// add (not what is there already), each piece that can't go down outlined
// (warning-colored when it only lacks the items), and its bounds, good or
// bad. Returns the plan.
PastePlan DrawPasteGhost(ImDrawList* draw, const Ship& ship, const Blueprint& blueprint, GridPos at, ImVec2 origin,
                         float cell) {
    const PastePlan plan = plan_paste(ship, blueprint, at);
    for (const PasteEntry& e : plan.entries) {
        if (e.satisfied)
            continue;
        const ImVec2 min = CellMin(origin, cell, e.at);
        const ImU32 bad = theme::U32(e.result == BuildResult::NoneInInventory ? theme::kWarn : theme::kBad);
        const BlueprintBuilding& b = blueprint.buildings[e.building];
        const GridSize size = footprint_of(b.kind, b.facing);
        const ImVec2 max = FootprintMax(min, cell, size);
        DrawBuilding(draw, min, cell, size, b.kind, b.facing, 0.45f);
        // Its settings, where the built one shows them.
        if (cell >= 8.0f && b.recipe && !b.recipe->outputs.empty())
            DrawItem(draw, ImVec2(max.x - cell * 0.32f, min.y + cell * 0.32f), cell * 0.16f,
                     b.recipe->outputs.front().kind);
        if (b.filter)
            DrawItem(draw, ImVec2(min.x + cell * 0.5f, min.y + cell * 0.5f), std::max(1.5f, cell * 0.2f), *b.filter);
        if (e.result != BuildResult::Ok)
            draw->AddRect(min, max, bad, 0.0f, 0, 2.0f);
    }
    const ImVec2 min = CellMin(origin, cell, at);
    draw->AddRect(min, FootprintMax(min, cell, blueprint.size), theme::U32(plan.ok() ? theme::kGood : theme::kBad),
                  0.0f, 0, 2.0f);
    return plan;
}

// The build grid on a canvas filling `avail`: click / drag to build and
// remove (or with the copy and paste tools, copy and paste), scroll to zoom,
// middle drag (or hold Space and move) to pan.
// A building's health, as a thin bar along its top, once it's damaged; and a
// red flash for a few frames after it's hit (`now`: the World's tick).
void DrawBuildingDamage(ImDrawList* draw, const Building& b, ImVec2 min, ImVec2 max, float cell, Tick now) {
    constexpr Tick kFlashTicks = 8;
    if (b.hit_at > 0 && now - b.hit_at < kFlashTicks) {
        const float fade = 1.0f - static_cast<float>(now - b.hit_at) / static_cast<float>(kFlashTicks);
        draw->AddRectFilled(min, max, theme::U32(theme::kBad, 0.45f * fade));
    }
    if (!b.damaged())
        return;
    const float fraction = static_cast<float>(b.get_hp()) / static_cast<float>(b.max_hp());
    const float h = std::max(2.0f, cell * 0.12f);
    const float inset = std::max(1.0f, cell * 0.08f);
    const ImVec2 bar_min(min.x + inset, min.y + inset);
    const ImVec2 bar_max(max.x - inset, min.y + inset + h);
    draw->AddRectFilled(bar_min, bar_max, theme::U32(theme::kBevelDark, 0.75f));
    const ImVec4 color = fraction > 0.6f ? theme::kGood : fraction > 0.3f ? theme::kWarn : theme::kBad;
    draw->AddRectFilled(bar_min, ImVec2(bar_min.x + (bar_max.x - bar_min.x) * fraction, bar_max.y),
                        theme::U32(color));
}

// The fight on the grid (`origin`, `cell`): turrets' shots, then the
// enemies, as arrowheads pointing where they're headed, with their health
// once hurt and their shots.
void DrawBattle(ImDrawList* draw, const Ship& ship, const Battle& battle, ImVec2 origin, float cell, Tick now) {
    constexpr Tick kTracerTicks = 6;
    const auto screen = [&](float x, float y) { return ImVec2(origin.x + x * cell, origin.y + y * cell); };
    const auto tracer = [&](ImVec2 from, ImVec2 to, Tick fired_at, ImVec4 color, float width) {
        if (fired_at == 0 || now - fired_at >= kTracerTicks)
            return;
        const float fade = 1.0f - static_cast<float>(now - fired_at) / static_cast<float>(kTracerTicks);
        draw->AddLine(from, to, theme::U32(color, fade), std::max(1.5f, width));
    };
    for (const GridPos p : ship.placement_order()) {
        const auto* turret = dynamic_cast<const Turret*>(ship.building_anchored_at(p));
        if (!turret)
            continue;
        const GridSize size = turret->footprint();
        const ImVec2 from = screen(static_cast<float>(p.x) + static_cast<float>(size.w) * 0.5f,
                                   static_cast<float>(p.y) + static_cast<float>(size.h) * 0.5f);
        tracer(from, screen(turret->aim_x, turret->aim_y), turret->fired_at, theme::kWarn,
               cell * (*turret->item_kind() == ItemKind::Railgun ? 0.3f : 0.12f));
    }
    for (const Enemy& e : battle.enemies) {
        const EnemySpec& spec = enemy_spec(e.kind);
        const ImVec2 c = screen(e.x, e.y);
        tracer(c, screen(e.aim_x, e.aim_y), e.fired_at, theme::kBad, cell * 0.1f);
        // Pointing at its target.
        ImVec2 d(0.0f, -1.0f);
        if (e.target)
            if (const Building* b = ship.building_anchored_at(*e.target)) {
                const GridSize size = b->footprint();
                const float dx = static_cast<float>(e.target->x) + static_cast<float>(size.w) * 0.5f - e.x;
                const float dy = static_cast<float>(e.target->y) + static_cast<float>(size.h) * 0.5f - e.y;
                const float len = std::hypot(dx, dy);
                if (len > 0.01f)
                    d = ImVec2(dx / len, dy / len);
            }
        const float r = cell * (e.kind == EnemyKind::Drone ? 0.45f : e.kind == EnemyKind::Gunship ? 0.9f : 1.8f);
        const ImVec2 side(-d.y, d.x);
        const ImVec2 tip(c.x + d.x * r, c.y + d.y * r);
        const ImVec2 left(c.x - d.x * r * 0.7f + side.x * r * 0.7f, c.y - d.y * r * 0.7f + side.y * r * 0.7f);
        const ImVec2 right(c.x - d.x * r * 0.7f - side.x * r * 0.7f, c.y - d.y * r * 0.7f - side.y * r * 0.7f);
        const ImVec4 color = e.kind == EnemyKind::Drone     ? theme::kBadHover
                             : e.kind == EnemyKind::Gunship ? theme::kBad
                                                            : theme::kBadActive;
        draw->AddTriangleFilled(tip, left, right, theme::U32(color));
        draw->AddTriangle(tip, left, right, theme::U32(theme::kBevelDark), 1.5f);
        if (e.hp < spec.hp) {
            const float w = r * 1.6f;
            const float h = std::max(2.0f, cell * 0.1f);
            const ImVec2 bar_min(c.x - w * 0.5f, c.y - r - h - 2.0f);
            draw->AddRectFilled(bar_min, ImVec2(bar_min.x + w, bar_min.y + h), theme::U32(theme::kBevelDark, 0.75f));
            draw->AddRectFilled(bar_min,
                                ImVec2(bar_min.x + w * static_cast<float>(e.hp) / static_cast<float>(spec.hp),
                                       bar_min.y + h),
                                theme::U32(theme::kBad));
        }
    }
}

// Whether a left drag turns `kind` to face the way it goes: belts, so a
// line drawn round a corner carries items along it.
bool DragTurns(ItemKind kind) {
    return kind == ItemKind::TransportBelt || kind == ItemKind::ExpressBelt;
}

// The way from cell `a` to the neighboring cell `b`.
Direction DirectionBetween(GridPos a, GridPos b) {
    if (b.x > a.x)
        return Direction::East;
    if (b.x < a.x)
        return Direction::West;
    return b.y > a.y ? Direction::South : Direction::North;
}

// Gives the building just placed at `p` the settings last picked for its
// kind, if any.
void ApplyLastSettings(Ship& ship, GridPos p, ItemKind kind) {
    const auto last = g_settings.last.find(static_cast<int>(kind));
    if (last != g_settings.last.end())
        ApplySettings(ship, p, last->second);
}

// Turns the belt this drag put at `p` to face `facing` (taking it up and
// putting it down again), so the one before a corner points round it.
void RefaceDragged(Ship& ship, GridPos p, ItemKind kind, Direction facing) {
    const std::vector<GridPos>& placed = g_build.drag_placed;
    const Building* b = ship.building_anchored_at(p);
    if (!b || b->item_kind() != kind || b->facing() == facing ||
        std::find(placed.begin(), placed.end(), p) == placed.end())
        return;
    const Direction before = b->facing();
    if (ship.remove_building(p) == BuildResult::Ok && ship.place(kind, p, facing) != BuildResult::Ok)
        ship.place(kind, p, before);
}

// One frame of a left press or drag that builds, the mouse over `cell`:
// places the selected item on every cell from the last one the drag was over
// to this one, a step at a time, so a quick drag leaves no gaps; belts turn
// to face the way it goes. A press that builds nothing inspects the building
// there, or says why.
void BuildDrag(Ship& ship, GridPos cell) {
    BuildUiState& build = g_build;
    const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (pressed || !build.drag_last) {
        build.drag_last.reset();
        build.drag_placed.clear();
    }
    std::vector<GridPos> path;
    GridPos prev = build.drag_last.value_or(cell);
    for (GridPos p = prev; p.x != cell.x || p.y != cell.y;) {
        if (p.x != cell.x)
            p.x += cell.x > p.x ? 1 : -1;
        else
            p.y += cell.y > p.y ? 1 : -1;
        path.push_back(p);
    }
    if (path.empty())
        path.push_back(cell);  // the press, or the mouse hasn't left the cell
    build.drag_last = cell;

    // With nothing selected, nothing gets built: a click only inspects.
    std::optional<BuildResult> result;
    for (const GridPos p : path) {
        if (!build.selected)
            break;
        const ItemKind kind = *build.selected;
        if (DragTurns(kind) && (p.x != prev.x || p.y != prev.y)) {
            build.facing = DirectionBetween(prev, p);
            RefaceDragged(ship, prev, kind, build.facing);
        }
        result = ship.place(kind, p, build.facing);
        if (result == BuildResult::Ok) {
            build.drag_placed.push_back(p);
            ApplyLastSettings(ship, p, kind);
        }
        prev = p;
    }
    if (!pressed || result == BuildResult::Ok)
        return;
    // Clicking a building (rather than building on it) inspects it.
    if (const Building* b = ship.building_at(cell)) {
        build.inspected = ship.anchor_of(cell);
        build.inspected_building = b;
    } else if (result) {
        SetBuildMessage(*result);
    }
}

// Shift + mouse on the grid, over `cell`: a right click copies the settings
// of the building there, a left click (or drag) pastes them onto each one
// it goes over.
void SettingsDrag(Ship& ship, GridPos cell) {
    const Building* b = ship.building_at(cell);
    const std::optional<BuildingSettings> here = b ? SettingsOf(*b) : std::nullopt;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (!here) {
            SetMessage("Nothing there with settings to copy");
            return;
        }
        g_settings.copied = here;
        const char* name = SettingsName(*here);
        SetMessage(std::string("Settings copied: ") + (name ? name : "none"), theme::kGood);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return;
    const bool pressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (!g_settings.copied) {
        if (pressed)
            SetMessage("Shift+right-click a building first, to copy its settings");
        return;
    }
    if (ApplySettings(ship, cell, *g_settings.copied)) {
        if (const std::optional<BuildingSettings> now = SettingsOf(*ship.building_at(cell)))
            RememberSettings(*now);
    } else if (pressed && here && !SameSettings(*here, *g_settings.copied)) {
        SetMessage("Those settings don't fit this building");
    }
}

// How long the mouse rests on a building before its tooltip shows.
constexpr double kTooltipDelay = 0.35;

void DrawShipGrid(Ship& ship, const Battle& battle, Tick now, ImVec2 avail) {
    const int n = ship.size();
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
    const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
    ImGui::InvisibleButton("##ship_grid", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool panning = UpdateCamera(n, canvas_min, canvas_size);
    const float cell = g_camera.cell;
    const float side = cell * static_cast<float>(n);
    const ImVec2 origin(std::floor(canvas_min.x + g_camera.offset.x), std::floor(canvas_min.y + g_camera.offset.y));

    // The cell under the mouse, and the nearest one on the grid (where a
    // copy box dragged off the edge stops).
    const ImVec2 m = ImGui::GetMousePos();
    const GridPos mouse_cell{static_cast<int>(std::floor((m.x - origin.x) / cell)),
                             static_cast<int>(std::floor((m.y - origin.y) / cell))};
    const GridPos nearest_cell{std::clamp(mouse_cell.x, 0, n - 1), std::clamp(mouse_cell.y, 0, n - 1)};
    std::optional<GridPos> hover_cell;
    if (ImGui::IsItemHovered() && !panning && ship.in_bounds(mouse_cell))
        hover_cell = mouse_cell;
    g_build.hover_cell = hover_cell;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        g_build.drag_last.reset();

    // Pasting: the blueprint centered on the mouse.
    BlueprintUiState& bp = g_blueprints;
    std::optional<GridPos> paste_at;
    if (bp.tool == Tool::Paste && bp.held && hover_cell)
        paste_at = GridPos{hover_cell->x - bp.held->size.w / 2, hover_cell->y - bp.held->size.h / 2};

    // Input: a click reports why it failed; dragging just skips cells that
    // don't work, so painting a row over existing tiles stays quiet.
    // Only while the press started on the grid (the button is active).
    if (bp.tool != Tool::Build) {
        // Right click puts the tool away.
        if (hover_cell && ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            LeaveBlueprintTool();
        } else if (bp.tool == Tool::Copy) {
            if (hover_cell && ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                bp.copy_start = hover_cell;
            if (bp.copy_start && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (const std::optional<Blueprint> copy = copy_region(ship, *bp.copy_start, nearest_cell)) {
                    bp.clipboard = copy;
                    HoldBlueprint(*copy);
                    SetMessage("Copied " + BlueprintSummary(*copy), theme::kGood);
                } else {
                    bp.copy_start.reset();
                    SetMessage("Nothing there to copy");
                }
            }
        } else if (paste_at && ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const PasteResult result = paste(ship, *bp.held, *paste_at);
            if (!result.pasted)
                SetMessage(describe(result.plan));
            else if (result.settings_skipped > 0)
                SetMessage(std::to_string(result.settings_skipped) +
                               " recipe(s) not unlocked yet: those machines have none",
                           theme::kWarnText);
        }
    } else if (hover_cell && ImGui::IsItemActive() && ImGui::GetIO().KeyShift) {
        SettingsDrag(ship, *hover_cell);
    } else if (hover_cell && ImGui::IsItemActive()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            BuildDrag(ship, *hover_cell);
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            const BuildResult r = ship.remove_building(*hover_cell);
            if (r != BuildResult::Ok && clicked)
                SetBuildMessage(r);
        }
    }

    // Only the cells in view get drawn: [lo, hi] on each axis.
    const auto first_visible = [&](float canvas_edge, float grid_edge) {
        return std::clamp(static_cast<int>(std::floor((canvas_edge - grid_edge) / cell)), 0, n - 1);
    };
    const GridPos lo{first_visible(canvas_min.x, origin.x), first_visible(canvas_min.y, origin.y)};
    const GridPos hi{first_visible(canvas_max.x, origin.x), first_visible(canvas_max.y, origin.y)};

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvas_min, canvas_max, true);
    const ImU32 grid_line = theme::U32(theme::kGridLine);
    const ImU32 grid_major = theme::U32(theme::kGridMajor);
    const ImU32 hull = theme::U32(theme::kTileHull);
    // The grid's space a shade lighter than the void around it, then the
    // grid as whole lines (one per row / column, not an outline per cell:
    // the grid is large), a brighter one every kGridMajorEvery cells like a
    // drawing sheet, then the hull filled over it. Zoomed far out, only the
    // grid's border.
    draw->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), theme::U32(theme::kInset));
    if (cell >= 4.0f) {
        const ImVec2 top_left = CellMin(origin, cell, lo);
        const ImVec2 bottom_right = CellMin(origin, cell, {hi.x + 1, hi.y + 1});
        for (int x = lo.x; x <= hi.x + 1; ++x) {
            const float t = origin.x + static_cast<float>(x) * cell;
            draw->AddLine(ImVec2(t, top_left.y), ImVec2(t, bottom_right.y),
                          x % theme::kGridMajorEvery == 0 ? grid_major : grid_line);
        }
        for (int y = lo.y; y <= hi.y + 1; ++y) {
            const float t = origin.y + static_cast<float>(y) * cell;
            draw->AddLine(ImVec2(top_left.x, t), ImVec2(bottom_right.x, t),
                          y % theme::kGridMajorEvery == 0 ? grid_major : grid_line);
        }
    } else {
        draw->AddRect(origin, ImVec2(origin.x + side, origin.y + side), grid_line);
    }
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const GridPos p{x, y};
            if (ship.has_hull(p)) {
                const ImVec2 min = CellMin(origin, cell, p);
                DrawHull(draw, min, ImVec2(min.x + cell, min.y + cell), cell, hull);
            }
        }
    }
    // Buildings after all the hull, since larger ones overlap later cells.
    // Ones anchored up to kMaxFootprint - 1 cells above / left of the view
    // still reach into it.
    constexpr int kMaxFootprint = 3;
    for (int y = std::max(0, lo.y - (kMaxFootprint - 1)); y <= hi.y; ++y)
        for (int x = std::max(0, lo.x - (kMaxFootprint - 1)); x <= hi.x; ++x)
            if (const Building* b = ship.building_anchored_at({x, y})) {
                const ImVec2 min = CellMin(origin, cell, {x, y});
                // An engine knocked offline, dimmed till it mends.
                const auto* thruster = dynamic_cast<const Thruster*>(b);
                DrawBuilding(draw, min, cell, b->footprint(), b->item_kind(), b->facing(),
                             thruster && thruster->offline() ? 0.4f : 1.0f);
                const ImVec2 max = FootprintMax(min, cell, b->footprint());
                if (const auto* collector = dynamic_cast<const Collector*>(b))
                    DrawBufferGauge(draw, min, max, cell,
                                    static_cast<float>(collector->stored()) /
                                        static_cast<float>(collector->capacity()));
                if (const auto* machine = dynamic_cast<const Machine*>(b))
                    DrawMachineStatus(draw, *machine, min, max, cell);
                if (const auto* sorter = dynamic_cast<const Sorter*>(b))
                    DrawSorterFilter(draw, *sorter, min, cell);
                DrawBuildingDamage(draw, *b, min, max, cell, now);
                if (b == InspectedBuilding(ship)) {
                    DrawCornerBrackets(draw, min, max, theme::U32(theme::kAccent), cell);
                    // A turret's reach.
                    if (const auto* turret = dynamic_cast<const Turret*>(b))
                        draw->AddCircle(ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
                                        turret->get_spec().range * cell, theme::U32(theme::kAccent, 0.6f), 0, 1.5f);
                }
            }
    // Items on belts, junctions and routers, over every building (they
    // cross onto the next cell).
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x) {
            const Building* b = ship.building_anchored_at({x, y});
            const ImVec2 min = CellMin(origin, cell, {x, y});
            if (const auto* belt = dynamic_cast<const Conveyor*>(b))
                DrawBeltItems(draw, *belt, min, cell);
            else if (const auto* junction = dynamic_cast<const Junction*>(b))
                DrawJunctionItems(draw, *junction, min, cell);
            else if (const auto* router = dynamic_cast<const Router*>(b))
                DrawRouterItem(draw, *router, min, cell);
        }

    DrawBattle(draw, ship, battle, origin, cell, now);

    // Copying: the box being dragged, or the cell it would start from.
    if (bp.tool == Tool::Copy) {
        if (bp.copy_start)
            DrawCopyBox(draw, ship, *bp.copy_start, nearest_cell, origin, cell);
        else if (hover_cell)
            DrawCopyBox(draw, ship, *hover_cell, *hover_cell, origin, cell);
    }
    // Pasting: a ghost of the blueprint, and why it can't go there if so.
    // Clipped to the grid.
    bp.paste_status.clear();
    if (paste_at) {
        draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);  // within the canvas clip
        const PastePlan plan = DrawPasteGhost(draw, ship, *bp.held, *paste_at, origin, cell);
        draw->PopClipRect();
        if (!plan.ok())
            bp.paste_status = describe(plan);
    }

    // Hover preview: a ghost of what would be built (its footprint starting
    // at the hovered cell), outlined in good / bad. Clipped to the grid.
    if (hover_cell && bp.tool == Tool::Build) {
        if (const std::optional<ItemKind> selected = g_build.selected) {
            const GridSize size = footprint_of(*selected, g_build.facing);
            const ImVec2 min = CellMin(origin, cell, *hover_cell);
            const ImVec2 max = FootprintMax(min, cell, size);
            const BuildResult r = ship.can_place(*selected, *hover_cell, g_build.facing);
            draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);  // within the canvas clip
            if (r == BuildResult::Ok)
                DrawBuilding(draw, min, cell, size, *selected, g_build.facing, 0.45f);
            draw->AddRect(min, max, theme::U32(r == BuildResult::Ok ? theme::kGood : theme::kBad), 0.0f, 0, 2.0f);
            draw->PopClipRect();
        }

    }

    // The tooltip of the building under the mouse, once it has rested there
    // a moment with no button down (not while building past it).
    static const Building* tip_building = nullptr;
    static double tip_since = 0.0;
    const bool buttons = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const Building* under =
        hover_cell && bp.tool == Tool::Build && !buttons ? ship.building_at(*hover_cell) : nullptr;
    if (under != tip_building) {
        tip_building = under;
        tip_since = ImGui::GetTime();
    }
    if (under && ImGui::GetTime() - tip_since >= kTooltipDelay) {
        ImGui::BeginTooltip();
        BuildingDetails(*under, true);
        ImGui::EndTooltip();
    }
    draw->PopClipRect();
}

// ---------------------------------------------------------------------------
// Star map
// ---------------------------------------------------------------------------

// The color of a sector's threat: green when safe, then amber, then red.
ImVec4 ThreatColor(int threat) {
    if (threat == 0)
        return theme::kGood;
    if (threat <= 2)
        return theme::kWarn;
    return theme::kBad;
}

// A threat as a row of kMaxThreat dots, `threat` of them filled, laid out
// like a text item.
void ThreatPips(int threat) {
    const float h = ImGui::GetTextLineHeight();
    const float r = h * 0.28f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(static_cast<float>(kMaxThreat) * r * 2.8f, h));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 color = theme::U32(ThreatColor(threat));
    for (int i = 0; i < kMaxThreat; ++i) {
        const ImVec2 c(min.x + r * 1.4f + static_cast<float>(i) * r * 2.8f, min.y + h * 0.5f);
        if (i < threat)
            draw->AddCircleFilled(c, r, color);
        else
            draw->AddCircle(c, r, theme::U32(theme::kTextDim), 0, 1.0f);
    }
}

// The picked sector's details, over the map's top-left corner: what's
// known of it, the ships there, and for the ship shown, jumping there.
void DrawSectorPanel(World& world, const FleetShip& current, int index, ImVec2 min) {
    const StarMap& map = world.get_map();
    const Sector& s = map.at(index);
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(min);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kPanel);
    ImGui::BeginChild("##sector", ImVec2(fh * 13.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushFont(nullptr, theme::kFontTitle);
    ImGui::TextUnformatted(s.surveyed ? s.name.c_str() : "Unsurveyed sector");
    ImGui::PopFont();
    if (index == 0)
        ImGui::TextColored(theme::kTextDim, "Home");
    if (!s.surveyed) {
        ImGui::TextColored(theme::kTextDim,
                           "Visit it, or bring a ship with a survey scanner next door, to learn what's there.");
    } else {
        ImGui::TextUnformatted("Threat");
        ImGui::SameLine();
        if (s.threat == 0)
            ImGui::TextColored(theme::kGood, "none");
        else
            ThreatPips(s.threat);
        if (s.cleared())
            ImGui::TextColored(theme::kGood, "Cleared: no more enemies come");
        else if (s.hostile())
            ImGui::TextColored(theme::kBad, "%d waves of enemies to fight off", s.waves_left);
        ImGui::TextColored(theme::kTextDim, "Yields (home: 100%%)");
        for (int r = 0; r < kRawResourceCount; ++r) {
            const int y = s.yields[static_cast<std::size_t>(r)];
            AmountLine(ItemIcon(kRawResources[r]), y > 0 ? std::to_string(y) + "%" : "none",
                       y > 100 ? theme::kGood : y > 0 ? theme::kText : theme::kTextDim, false);
        }
    }
    std::string here;
    for (const std::unique_ptr<FleetShip>& ship : world.get_fleet().get_ships())
        if (ship->sector == index)
            here += (here.empty() ? "" : ", ") + ship->name;
    if (!here.empty())
        ImGui::Text("Here: %s", here.c_str());

    // Jumping the ship shown here.
    if (index != current.sector) {
        ImGui::Separator();
        const std::vector<const Thruster*> thrusters = current.ship.thrusters();
        const auto charged = std::count_if(thrusters.begin(), thrusters.end(),
                                           [](const Thruster* t) { return t->charged(); });
        const auto offline = std::count_if(thrusters.begin(), thrusters.end(),
                                           [](const Thruster* t) { return t->offline(); });
        ImGui::TextColored(theme::kTextDim, "%s's ion thrusters: %d of %zu charged", current.name.c_str(),
                           static_cast<int>(charged), thrusters.size());
        if (offline > 0)
            ImGui::TextColored(theme::kBad, "%d knocked offline", static_cast<int>(offline));
        const JumpResult can = world.can_jump(current.id, index);
        ImGui::BeginDisabled(can != JumpResult::Ok);
        if (theme::PrimaryButton("Jump here", ImVec2(-1.0f, 0.0f)) &&
            world.jump(current.id, index) == JumpResult::Ok)
            SetMessage(current.name + " jumped to " + s.name, theme::kGood);
        ImGui::EndDisabled();
        if (can != JumpResult::Ok)
            ImGui::TextColored(theme::kWarnText, "%s", describe(can));
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawStarMap(World& world, const FleetShip& current, ImVec2 avail) {
    const StarMap& map = world.get_map();
    MapUiState& m = g_map;
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
    const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
    ImGui::InvisibleButton("##star_map", canvas_size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const ImGuiIO& io = ImGui::GetIO();

    // The view: fit the whole map at first; the wheel zooms about the mouse,
    // and a drag (left on empty space, or middle) pans.
    float extent = 1.0f;
    for (const Sector& s : map.all())
        extent = std::max({extent, std::abs(s.x), std::abs(s.y)});
    const float fit = std::min(canvas_size.x, canvas_size.y) * 0.5f / (extent * 1.15f);
    if (!m.placed) {
        m.scale = fit;
        m.offset = ImVec2(canvas_size.x * 0.5f, canvas_size.y * 0.5f);
        m.placed = true;
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        const float new_scale = std::clamp(m.scale * std::pow(kZoomStep, io.MouseWheel), fit * 0.5f, fit * 5.0f);
        const ImVec2 p(io.MousePos.x - canvas_min.x, io.MousePos.y - canvas_min.y);
        const float k = new_scale / m.scale;
        m.offset = ImVec2(p.x - (p.x - m.offset.x) * k, p.y - (p.y - m.offset.y) * k);
        m.scale = new_scale;
    }
    const auto screen = [&](const Sector& s) {
        return ImVec2(canvas_min.x + m.offset.x + s.x * m.scale, canvas_min.y + m.offset.y + s.y * m.scale);
    };
    const float fh = ImGui::GetFrameHeight();
    const float r = std::max(fh * 0.35f, 6.0f);

    // The sector under the mouse, among those shown.
    std::optional<int> hovered;
    if (ImGui::IsItemHovered())
        for (int i = 0; i < map.size(); ++i) {
            const ImVec2 c = screen(map.at(i));
            if (map.known(i) && std::hypot(io.MousePos.x - c.x, io.MousePos.y - c.y) <= r * 1.6f)
                hovered = i;
        }
    if (ImGui::IsItemActivated())
        m.dragged = false;
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) ||
                                  ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
        m.dragged = true;
        m.offset.x += io.MouseDelta.x;
        m.offset.y += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (ImGui::IsItemDeactivated() && !m.dragged) {
        m.selected = hovered;  // a click: pick a sector, or nothing
    }
    if (m.selected && (*m.selected >= map.size() || !map.known(*m.selected)))
        m.selected.reset();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(canvas_min, canvas_max, true);
    draw->AddRectFilled(canvas_min, canvas_max, theme::U32(theme::kInset));
    draw->AddRect(canvas_min, canvas_max, theme::U32(theme::kBevelDark));
    // Range rings round home, and cross hairs through it, like a plotting
    // table.
    const ImVec2 home(canvas_min.x + m.offset.x, canvas_min.y + m.offset.y);
    const ImU32 ring = theme::U32(theme::kGridLine);
    for (int k = 1; k <= 6; ++k)
        draw->AddCircle(home, extent * m.scale * static_cast<float>(k) / 3.0f, ring, 96, 1.0f);
    draw->AddLine(ImVec2(canvas_min.x, home.y), ImVec2(canvas_max.x, home.y), ring);
    draw->AddLine(ImVec2(home.x, canvas_min.y), ImVec2(home.x, canvas_max.y), ring);

    // Links between known sectors; those out of the shown ship's sector,
    // where it can jump, stand out.
    for (int i = 0; i < map.size(); ++i)
        for (const int j : map.at(i).neighbors) {
            if (j < i || !map.known(i) || !map.known(j))
                continue;
            const bool reach = i == current.sector || j == current.sector;
            draw->AddLine(screen(map.at(i)), screen(map.at(j)),
                          theme::U32(reach ? theme::kAccent : theme::kFrameActive), reach ? 2.5f : 1.5f);
        }

    // Sectors: filled by threat once surveyed, a "?" before; a ring when
    // visited, an amber one when picked.
    const ImU32 ink = theme::U32(theme::kText);
    const ImU32 halo = theme::U32(theme::kInset);
    for (int i = 0; i < map.size(); ++i) {
        if (!map.known(i))
            continue;
        const Sector& s = map.at(i);
        const ImVec2 c = screen(s);
        if (s.surveyed) {
            draw->AddCircleFilled(c, r, theme::U32(ThreatColor(s.hostile() ? s.threat : 0)));
        } else {
            draw->AddCircleFilled(c, r, theme::U32(theme::kFrame));
            const ImVec2 q = ImGui::CalcTextSize("?");
            draw->AddText(ImVec2(c.x - q.x * 0.5f, c.y - q.y * 0.5f), theme::U32(theme::kTextDim), "?");
        }
        draw->AddCircle(c, r, s.visited ? ink : theme::U32(theme::kTextDim), 0, s.visited ? 2.0f : 1.0f);
        if (m.selected == i)
            draw->AddCircle(c, r + 4.0f, theme::U32(theme::kAccent), 0, 2.5f);
        else if (hovered == i)
            draw->AddCircle(c, r + 4.0f, theme::U32(theme::kAccent, 0.5f), 0, 1.5f);
        const char* label = s.surveyed ? s.name.c_str() : "";
        const ImVec2 ts = ImGui::CalcTextSize(label);
        theme::DrawHaloText(draw, ImVec2(c.x - ts.x * 0.5f, c.y + r + 3.0f), ink, halo, label);
    }

    // Ships: a marker per ship beside its sector, the one shown in red.
    std::vector<int> drawn(static_cast<std::size_t>(map.size()), 0);
    for (const std::unique_ptr<FleetShip>& ship : world.get_fleet().get_ships()) {
        const ImVec2 c = screen(map.at(ship->sector));
        int& k = drawn[static_cast<std::size_t>(ship->sector)];
        const ImVec2 p(c.x + r + 6.0f + static_cast<float>(k) * r * 1.1f, c.y - r);
        ++k;
        const float h = r * 0.9f;
        draw->AddTriangleFilled(ImVec2(p.x, p.y - h * 0.6f), ImVec2(p.x + h * 0.5f, p.y + h * 0.5f),
                                ImVec2(p.x - h * 0.5f, p.y + h * 0.5f),
                                theme::U32(ship->id == current.id ? theme::kAccent : theme::kSteel));
    }
    if (hovered)
        ImGui::SetTooltip("%s", map.at(*hovered).surveyed ? map.at(*hovered).name.c_str() : "Unsurveyed sector");
    draw->PopClipRect();

    const float pad = ImGui::GetStyle().WindowPadding.x;
    ImGui::SetCursorScreenPos(ImVec2(canvas_min.x + pad, canvas_max.y - ImGui::GetTextLineHeight() - pad));
    ImGui::TextColored(theme::kTextDim, "Click a sector for details. M: back to the ship");
    if (m.selected)
        DrawSectorPanel(world, current, *m.selected, ImVec2(canvas_min.x + pad, canvas_min.y + pad));
}

// ---------------------------------------------------------------------------
// Save list
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;

// The save list screen's state. Reset, and the list read from disk, each
// time the screen opens; read again after each change it makes.
struct SaveListUiState {
    std::vector<SaveSlot> slots;
    int drawn_frame = -1;  // ImGui::GetFrameCount() when last drawn
    // The save whose name is being edited, and the edit.
    std::optional<fs::path> renaming;
    char new_name[64] = {};
    bool focus_name = false;
    // The save waiting for the player to confirm deleting it.
    std::optional<fs::path> deleting;
    std::string deleting_name;
};
SaveListUiState g_saves;

constexpr const char* kDeletePopup = "Delete save?";

// "2026-10-19 14:05", in local time.
std::string FormatSavedAt(std::int64_t saved_at) {
    const auto t = static_cast<std::time_t>(saved_at);
    const std::tm* local = std::localtime(&t);
    char text[32] = "?";
    if (local)
        std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", local);
    return text;
}

// "1:02:03": hours, minutes and seconds of play.
std::string FormatPlayed(Tick ticks) {
    const Tick seconds = ticks / kTicksPerSecond;
    char text[32];
    std::snprintf(text, sizeof text, "%llu:%02llu:%02llu", static_cast<unsigned long long>(seconds / 3600),
                  static_cast<unsigned long long>(seconds / 60 % 60), static_cast<unsigned long long>(seconds % 60));
    return text;
}

// ---------------------------------------------------------------------------
// Blueprint library
// ---------------------------------------------------------------------------

// The Blueprints window's state. Reset, and the library read from disk,
// each time it opens; read again after each change it makes.
struct LibraryUiState {
    std::vector<BlueprintSlot> slots;
    bool loaded = false;
    std::string message;  // what the last action did, or why it failed
    ImVec4 message_color = theme::kBad;
    // Saving the clipboard: the name being typed.
    char save_name[64] = {};
    bool focus_save = false;
    // The blueprint whose name is being edited, and the edit.
    std::optional<fs::path> renaming;
    char new_name[64] = {};
    bool focus_name = false;
    // The blueprint waiting for the player to confirm deleting it.
    std::optional<fs::path> deleting;
    std::string deleting_name;
};
LibraryUiState g_library;

constexpr const char* kSaveBlueprintPopup = "Save blueprint";
constexpr const char* kDeleteBlueprintPopup = "Delete blueprint?";

// The Blueprints window (B, or Library in the left panel): the blueprints
// saved in `dir`, to paste, rename, delete or copy as text, and saving the
// clipboard into it or importing a blueprint copied as text.
void DrawBlueprintLibrary(const fs::path& dir) {
    BlueprintUiState& bp = g_blueprints;
    LibraryUiState& ui = g_library;
    if (!bp.show_library) {
        ui.loaded = false;
        return;
    }
    if (!ui.loaded) {
        ui = {};
        ui.slots = list_blueprints(dir);
        ui.loaded = true;
    }
    const auto say = [&](std::string message, ImVec4 color) {
        ui.message = std::move(message);
        ui.message_color = color;
    };

    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 24.0f, fh * 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Blueprints", &bp.show_library, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    bool refresh = false;
    std::optional<fs::path> use;  // the blueprint to pick up, once the list is drawn
    bool commit_rename = false;

    ImGui::BeginDisabled(!bp.clipboard);
    if (theme::PrimaryButton("Save clipboard")) {
        std::snprintf(ui.save_name, sizeof ui.save_name, "%s",
                      bp.clipboard->name.empty() ? ("Blueprint " + std::to_string(ui.slots.size() + 1)).c_str()
                                                 : bp.clipboard->name.c_str());
        ui.focus_save = true;
        ImGui::OpenPopup(kSaveBlueprintPopup);
    }
    ImGui::SetItemTooltip("Keep what was last copied, by name");
    ImGui::SameLine();
    if (ImGui::Button("Copy clipboard as text")) {
        ImGui::SetClipboardText(blueprint_to_string(*bp.clipboard).c_str());
        say("Copied as text: paste it anywhere to share it", theme::kGood);
    }
    ImGui::SetItemTooltip("Put what was last copied on the system clipboard, as a blueprint string");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Import text")) {
        const char* text = ImGui::GetClipboardText();
        std::string error;
        std::optional<Blueprint> imported = blueprint_from_string(text ? text : "", error);
        if (!imported) {
            say("The system clipboard holds no blueprint: " + error, theme::kBad);
        } else {
            if (Trimmed(imported->name).empty())
                imported->name = "Imported blueprint";
            if (add_blueprint(dir, *imported, error))
                say("Imported \"" + imported->name + "\"", theme::kGood);
            else
                say("Couldn't save it: " + error, theme::kBad);
            refresh = true;
        }
    }
    ImGui::SetItemTooltip("Add a blueprint string from the system clipboard");

    if (!ui.message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ui.message_color, "%s", ui.message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    if (ui.slots.empty())
        ImGui::TextColored(theme::kTextDim, "No blueprints yet: copy an area (%s+C), then save it here.", kCtrlName);
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ui.slots.empty() && ImGui::BeginTable("##blueprint_table", 3, table_flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        for (const BlueprintSlot& slot : ui.slots) {
            ImGui::PushID(slot.path.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!slot.blueprint) {
                // Unreadable: it can only be deleted.
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "%s", slot.path.filename().string().c_str());
                ImGui::SameLine();
                theme::HelpMarker(("Can't be read: " + slot.error).c_str());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                if (theme::DangerSmallButton("Delete")) {
                    ui.deleting = slot.path;
                    ui.deleting_name = slot.path.filename().string();
                }
                ImGui::PopID();
                continue;
            }
            const Blueprint& blueprint = *slot.blueprint;
            if (ui.renaming == slot.path) {
                if (ui.focus_name) {
                    ImGui::SetKeyboardFocusHere();
                    ui.focus_name = false;
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##name", ui.new_name, sizeof ui.new_name,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    commit_rename = true;
                else if (ImGui::IsItemDeactivated())
                    ui.renaming.reset();  // Escape, or clicked elsewhere
            } else {
                // The whole row: double-click to pick it up.
                ImGui::Selectable(blueprint.name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, fh));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    use = slot.path;
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(BlueprintSummary(blueprint).c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Use"))
                use = slot.path;
            ImGui::SetItemTooltip("Pick it up to paste");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy text")) {
                ImGui::SetClipboardText(blueprint_to_string(blueprint).c_str());
                say("Copied \"" + blueprint.name + "\" as text", theme::kGood);
            }
            ImGui::SetItemTooltip("Put it on the system clipboard, as a blueprint string to share");
            ImGui::SameLine();
            if (theme::PencilButton("##rename")) {
                ui.renaming = slot.path;
                std::snprintf(ui.new_name, sizeof ui.new_name, "%s", blueprint.name.c_str());
                ui.focus_name = true;
            }
            ImGui::SetItemTooltip("Rename");
            ImGui::SameLine();
            if (theme::DangerSmallButton("Delete")) {
                ui.deleting = slot.path;
                ui.deleting_name = blueprint.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Naming the clipboard, to save it.
    if (ImGui::BeginPopupModal(kSaveBlueprintPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Name:");
        if (ui.focus_save) {
            ImGui::SetKeyboardFocusHere();
            ui.focus_save = false;
        }
        ImGui::SetNextItemWidth(fh * 12.0f);
        const bool entered = ImGui::InputText("##save_name", ui.save_name, sizeof ui.save_name,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        bool close = false;
        if (theme::PrimaryButton("Save") || entered) {
            const std::string name = Trimmed(ui.save_name);
            std::string error;
            if (name.empty()) {
                say("A blueprint needs a name.", theme::kBad);
            } else if (bp.clipboard) {
                Blueprint named = *bp.clipboard;
                named.name = name;
                if (add_blueprint(dir, named, error))
                    say("Saved \"" + name + "\"", theme::kGood);
                else
                    say("Couldn't save it: " + error, theme::kBad);
                refresh = true;
            }
            close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ShortcutPressed(ImGuiKey_Escape))
            close = true;
        if (close)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Opened here, not in the row, so the popup's ID doesn't depend on it.
    if (ui.deleting && !ImGui::IsPopupOpen(kDeleteBlueprintPopup))
        ImGui::OpenPopup(kDeleteBlueprintPopup);
    if (ImGui::BeginPopupModal(kDeleteBlueprintPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? This can't be undone.", ui.deleting_name.c_str());
        ImGui::Spacing();
        bool close = false;
        if (theme::DangerButton("Delete")) {
            std::string error;
            if (ui.deleting && !delete_blueprint(*ui.deleting, error))
                say("Couldn't delete it: " + error, theme::kBad);
            else
                ui.message.clear();
            refresh = close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ShortcutPressed(ImGuiKey_Escape))
            close = true;
        if (close) {
            ui.deleting.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (commit_rename && ui.renaming) {
        const std::string name = Trimmed(ui.new_name);
        std::string error;
        if (name.empty())
            say("A blueprint needs a name.", theme::kBad);
        else if (!rename_blueprint(*ui.renaming, name, error))
            say("Couldn't rename it: " + error, theme::kBad);
        ui.renaming.reset();
        refresh = true;
    }
    if (use)
        for (const BlueprintSlot& slot : ui.slots)
            if (slot.path == *use && slot.blueprint) {
                HoldBlueprint(*slot.blueprint);
                bp.show_library = false;
            }
    if (refresh)
        ui.slots = list_blueprints(dir);
    ImGui::End();
}

}  // namespace

void DrawMainMenu(Screen& screen) {
    BeginFullscreenPage("##main_menu");

    const float fh = ImGui::GetFrameHeight();  // follows font size / DPI scaling
    const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    // Title plus two buttons, as one block centered vertically.
    theme::PushMono(theme::kFontHuge * 1.4f);
    const float title_h = ImGui::GetTextLineHeight();
    const float title_w = ImGui::CalcTextSize("ORE IN ORBIT").x;
    theme::PopMono();
    const float block_h = title_h + fh * 1.5f + 2.0f * button_size.y + spacing;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - block_h) * 0.5f);

    theme::PushMono(theme::kFontHuge * 1.4f);
    CenteredText("ORE IN ORBIT", theme::kText);
    theme::PopMono();
    // A hazard-striped rule under the title.
    const ImVec2 title_max = ImGui::GetItemRectMax();
    const float rule_w = std::max(title_w, button_size.x);
    const ImVec2 rule_min((ImGui::GetWindowWidth() - rule_w) * 0.5f + ImGui::GetWindowPos().x, title_max.y + fh * 0.3f);
    theme::DrawHazardStripes(ImGui::GetWindowDrawList(), rule_min,
                             ImVec2(rule_min.x + rule_w, rule_min.y + fh * 0.35f), theme::kAccent);
    ImGui::Dummy({0.0f, fh * 1.5f - spacing});

    const float button_x = (ImGui::GetWindowWidth() - button_size.x) * 0.5f;
    ImGui::SetCursorPosX(button_x);
    if (theme::PrimaryButton("Play", button_size))
        screen = Screen::SaveSelect;
    ImGui::SetCursorPosX(button_x);
    if (ImGui::Button("Credits", button_size))
        screen = Screen::Credits;

    ImGui::End();
}

SaveSelectResult DrawSaveSelect(Screen& screen, const fs::path& saves_dir, std::string& message) {
    SaveListUiState& ui = g_saves;
    if (ImGui::GetFrameCount() != ui.drawn_frame + 1) {
        ui = {};
        ui.slots = list_saves(saves_dir);
        message.clear();
    }
    ui.drawn_frame = ImGui::GetFrameCount();

    SaveSelectResult result;
    // Changes to the list wait until it has been drawn.
    std::optional<fs::path> copy_from;
    bool commit_rename = false;

    BeginFullscreenPage("##save_select");
    const float fh = ImGui::GetFrameHeight();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fh * 32.0f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - width) * 0.5f);
    ImGui::BeginChild("##saves", ImVec2(width, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);

    theme::SectionHeading("Saves");
    if (theme::PrimaryButton("New game"))
        result.action = SaveSelectResult::Action::NewGame;
    ImGui::SameLine();
    const bool popup_open = ImGui::IsPopupOpen(kDeletePopup);
    if (ImGui::Button("Back") || (!popup_open && ShortcutPressed(ImGuiKey_Escape)))
        screen = Screen::MainMenu;
    if (!message.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::kBad, "%s", message.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();

    if (ui.slots.empty())
        ImGui::TextColored(theme::kTextDim, "No saves yet: start a new game.");
    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit;
    if (!ui.slots.empty() && ImGui::BeginTable("##save_table", 4, table_flags)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Last saved");
        ImGui::TableSetupColumn("Played");
        ImGui::TableSetupColumn("##actions");
        ImGui::TableHeadersRow();
        for (const SaveSlot& slot : ui.slots) {
            ImGui::PushID(slot.path.string().c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!slot.header) {
                // Unreadable: it can only be deleted.
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(theme::kTextDim, "%s", slot.path.filename().string().c_str());
                ImGui::SameLine();
                theme::HelpMarker(("Can't be loaded: " + slot.error).c_str());
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                ImGui::TableNextColumn();
                if (theme::DangerSmallButton("Delete")) {
                    ui.deleting = slot.path;
                    ui.deleting_name = slot.path.filename().string();
                }
                ImGui::PopID();
                continue;
            }
            const SaveHeader& header = *slot.header;

            if (ui.renaming == slot.path) {
                if (ui.focus_name) {
                    ImGui::SetKeyboardFocusHere();
                    ui.focus_name = false;
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##name", ui.new_name, sizeof ui.new_name,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    commit_rename = true;
                else if (ImGui::IsItemDeactivated())
                    ui.renaming.reset();  // Escape, or clicked elsewhere
            } else {
                // The whole row: double-click to load.
                ImGui::Selectable(header.name.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                  ImVec2(0.0f, fh));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    result = {SaveSelectResult::Action::Load, slot.path};
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(FormatSavedAt(header.saved_at).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(FormatPlayed(header.played).c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Load"))
                result = {SaveSelectResult::Action::Load, slot.path};
            ImGui::SameLine();
            if (theme::PencilButton("##rename")) {
                ui.renaming = slot.path;
                std::snprintf(ui.new_name, sizeof ui.new_name, "%s", header.name.c_str());
                ui.focus_name = true;
            }
            ImGui::SetItemTooltip("Rename");
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy"))
                copy_from = slot.path;
            ImGui::SameLine();
            if (theme::DangerSmallButton("Delete")) {
                ui.deleting = slot.path;
                ui.deleting_name = header.name;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Opened here, not in the row, so the popup's ID doesn't depend on it.
    if (ui.deleting && !popup_open)
        ImGui::OpenPopup(kDeletePopup);
    bool refresh = false;
    if (ImGui::BeginPopupModal(kDeletePopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"? This can't be undone.", ui.deleting_name.c_str());
        ImGui::Spacing();
        bool close = false;
        if (theme::DangerButton("Delete")) {
            std::string error;
            message = !ui.deleting || delete_save(*ui.deleting, error) ? "" : "Couldn't delete it: " + error;
            refresh = close = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ShortcutPressed(ImGuiKey_Escape))
            close = true;
        if (close) {
            ui.deleting.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (commit_rename && ui.renaming) {
        const std::string name = Trimmed(ui.new_name);
        std::string error;
        if (name.empty())
            message = "A save needs a name.";
        else
            message = rename_save(*ui.renaming, name, error) ? "" : "Couldn't rename it: " + error;
        ui.renaming.reset();
        refresh = true;
    }
    if (copy_from) {
        std::string error;
        message = copy_save(saves_dir, *copy_from, error) ? "" : "Couldn't copy it: " + error;
    }
    if (refresh || copy_from)
        ui.slots = list_saves(saves_dir);

    ImGui::EndChild();
    ImGui::End();
    return result;
}

void DrawCredits(Screen& screen) {
    BeginFullscreenPage("##credits");
    theme::SectionHeading("Credits");
    if (ImGui::Button("Back") || ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;
    ImGui::End();
}

namespace {

// Back to the main menu from a game: the game screen's state is the next
// game's to start over with.
void LeaveGame(Screen& screen) {
    screen = Screen::MainMenu;
    g_camera = {};  // the next game starts centred on its bridge
    g_fleet = {};
    g_map = {};
    g_banner = {};
    g_build.inspected.reset();
    g_build.inspected_building = nullptr;
    LeaveBlueprintTool();  // the clipboard stays, to paste into another game
    g_blueprints.show_library = false;
    g_build.show_keys = false;
    g_settings = {};
}

// One hint in the bar under the grid: a key cap, then what it does.
void Hint(const char* key, const char* does) {
    theme::KeyCap(key);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(theme::kTextDim, "%s", does);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f);
}

// The bar under the grid (`min`, `width` across): the keys that do
// something now, for the tool in hand, and the "Keys" button that opens
// the overlay of all of them.
void DrawHintBar(ImVec2 min, float width) {
    const float fh = ImGui::GetFrameHeight();
    const BlueprintUiState& bp = g_blueprints;
    ImGui::SetCursorScreenPos(min);
    ImGui::BeginChild("##hints", ImVec2(width, fh), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const char* keys_label = "? Keys";
    const float keys_w = ImGui::CalcTextSize(keys_label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::PushClipRect(min, ImVec2(min.x + width - keys_w - ImGui::GetStyle().ItemSpacing.x, min.y + fh), true);
    ImGui::SetCursorPosY((fh - ImGui::GetTextLineHeight()) * 0.5f);
    if (g_map.shown) {
        Hint("LMB", "sector details");
        Hint("Drag", "pan");
        Hint("Wheel", "zoom");
        Hint("M", "back to the ship");
    } else if (bp.tool == Tool::Copy) {
        Hint("Drag", "box over what to copy");
        Hint("RMB", "stop");
        Hint("Esc", "stop");
    } else if (bp.tool == Tool::Paste) {
        Hint("LMB", "paste");
        Hint("R", "turn");
        Hint("RMB", "stop");
        if (!bp.paste_status.empty())
            ImGui::TextColored(theme::kBad, "%s", bp.paste_status.c_str());
    } else if (ImGui::GetIO().KeyShift) {
        Hint("Shift+RMB", "copy a building's settings");
        Hint("Shift+LMB", "paste them (drag for several)");
        if (g_settings.copied)
            if (const char* name = SettingsName(*g_settings.copied))
                ImGui::TextColored(theme::kAccent, "Holding: %s", name);
    } else if (g_build.selected) {
        Hint("LMB", "build");
        Hint("RMB", "remove");
        if (is_rotatable(*g_build.selected)) {
            std::string facing = std::string("turn (now ") + DirectionName(g_build.facing) + ")";
            Hint("R / Shift+R", facing.c_str());
        }
        Hint("Q", "pick / build nothing");
        Hint("Shift", "settings");
    } else {
        Hint("LMB", "inspect");
        Hint("RMB", "remove");
        Hint("Q", "pick the building under the mouse");
        Hint("1-9", "build");
        Hint("Shift", "settings");
    }
    ImGui::PopClipRect();
    ImGui::SetCursorPos(ImVec2(width - keys_w, 0.0f));
    if (theme::SegmentedButton(keys_label, g_build.show_keys, ImVec2(keys_w, fh)))
        g_build.show_keys = !g_build.show_keys;
    ImGui::SetItemTooltip("Every key and mouse control (?)");
    ImGui::EndChild();
}

// The keys overlay (? or the Keys button): every control, in a table.
void DrawKeysWindow() {
    if (!g_build.show_keys)
        return;
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 22.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Keys", &g_build.show_keys,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const std::string copy = std::string(kCtrlName) + "+C";
    const std::string paste = std::string(kCtrlName) + "+V";
    const struct {
        const char* key;
        const char* does;
    } rows[] = {
        {"LMB / drag", "Build the selected item; belts face the way you drag"},
        {"LMB a building", "Inspect it: a machine's recipe, a collector's target, a sorter's filter"},
        {"RMB / drag", "Remove buildings (refunded)"},
        {"Shift+RMB", "Copy a building's settings"},
        {"Shift+LMB / drag", "Paste them onto buildings that can take them"},
        {"Wheel", "Zoom"},
        {"MMB drag, Space", "Pan"},
        {"1-9", "Pick the item to build (again: build nothing)"},
        {"RMB a palette icon", "Hand-craft one (Shift: 5)"},
        {"R / Shift+R", "Turn what you're building, or the blueprint"},
        {"Q", "Pick the building under the mouse, or build nothing"},
        {copy.c_str(), "Copy an area (drag a box)"},
        {paste.c_str(), "Pick the last copy up to paste"},
        {"B", "Blueprint library"},
        {"M", "Star map"},
        {"[ / ]", "Previous / next ship"},
        {"?", "This list"},
        {"Esc", "Put the tool away, close what's open, then pause"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            theme::KeyCap(row.key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row.does);
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(fh * 20.0f);
    ImGui::TextColored(theme::kTextDim,
                       "New machines, collectors and sorters start with the settings last picked for their kind. "
                       "Buildings go on the ship's hull, which its layout fixes, with its bridge and engines.");
    ImGui::PopTextWrapPos();
    ImGui::End();
}

// The end of a game whose fleet is lost: the news, and the way out.
void DrawFleetLost(Screen& screen) {
    BeginFullscreenPage("##fleet_lost");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorPosY(viewport->WorkSize.y * 0.35f);
    // A band of red hazard stripes right across the screen, behind the news.
    const ImVec2 band_min(viewport->WorkPos.x, ImGui::GetCursorScreenPos().y - fh * 0.5f);
    theme::DrawHazardStripes(ImGui::GetWindowDrawList(), band_min,
                             ImVec2(band_min.x + viewport->WorkSize.x, band_min.y + fh * 0.4f), theme::kBad, 0.8f);
    ImGui::Dummy(ImVec2(0.0f, fh * 0.2f));
    theme::PushMono(theme::kFontHuge);
    CenteredText("FLEET LOST", theme::kBad);
    theme::PopMono();
    CenteredText("Every ship's bridge has been destroyed.", theme::kText);
    CenteredText("Load the last save (from a minute ago, at most) to try again.", theme::kTextDim);
    ImGui::Dummy(ImVec2(0.0f, fh));
    const ImVec2 button(fh * 8.0f, fh * 1.6f);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button.x) * 0.5f);
    if (theme::PrimaryButton("Main menu", button) || ShortcutPressed(ImGuiKey_Escape))
        LeaveGame(screen);
    ImGui::End();
}

}  // namespace

void DrawPlaying(Screen& screen, World& world, bool& paused, const std::filesystem::path& blueprints_dir) {
    if (world.fleet_lost()) {
        paused = false;
        DrawFleetLost(screen);
        return;
    }
    // Escape opens the pause menu, or closes it again; first, though, it puts
    // the copy / paste tool away, then closes what's open, one at a time:
    // the Send cargo window, the inspector, the keys overlay, the star map,
    // the Blueprints window. Not when it closed a dropdown or tooltip popup:
    // ImGui already did that before we got here, so look at whether one was
    // open last frame.
    static bool popup_was_open = false;
    BlueprintUiState& bp = g_blueprints;
    if (ShortcutPressed(ImGuiKey_Escape) && (paused || !popup_was_open)) {
        if (paused)
            paused = false;
        else if (bp.tool != Tool::Build)
            LeaveBlueprintTool();
        else if (g_fleet.show_send)
            g_fleet.show_send = false;
        else if (g_fleet.show_build)
            g_fleet.show_build = false;
        else if (g_build.inspected) {
            g_build.inspected.reset();
            g_build.inspected_building = nullptr;
        } else if (g_build.show_keys)
            g_build.show_keys = false;
        else if (g_map.shown)
            g_map.shown = false;
        else if (bp.show_library)
            bp.show_library = false;
        else
            paused = true;
    }

    FleetShip& current = CurrentShip(world.get_fleet());
    Ship& ship = current.ship;
    if (!paused) {
        if (ShortcutPressed(ImGuiKey_LeftBracket))
            CycleShip(world.get_fleet(), -1);
        if (ShortcutPressed(ImGuiKey_RightBracket))
            CycleShip(world.get_fleet(), 1);
        if (ShortcutPressed(ImGuiKey_M))
            g_map.shown = !g_map.shown;
        const std::vector<ItemKind> list = BuildList(ship);
        for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 9); ++i)
            if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i))))
                ToggleSelected(list[i]);
        if (ShortcutPressed(ImGuiKey_Q)) {
            // Over a building: pick it, as it faces and with its settings for
            // new ones. Anywhere else: build nothing.
            LeaveBlueprintTool();
            const Building* b =
                g_build.hover_cell && !g_map.shown ? ship.building_at(*g_build.hover_cell) : nullptr;
            const std::optional<ItemKind> kind = b ? b->item_kind() : std::nullopt;
            if (kind && is_placeable(*kind)) {
                g_build.selected = kind;
                if (is_rotatable(*kind))
                    g_build.facing = b->facing();
                if (const std::optional<BuildingSettings> settings = SettingsOf(*b))
                    RememberSettings(*settings);
            } else {
                g_build.selected.reset();
            }
        }
        if (ShortcutPressed(ImGuiKey_Slash))
            g_build.show_keys = !g_build.show_keys;
        if (ShortcutPressed(ImGuiKey_R)) {
            // Shift turns it back the other way: three quarter turns, for a
            // blueprint.
            const bool back = ImGui::GetIO().KeyShift;
            if (bp.tool != Tool::Paste || !bp.held) {
                g_build.facing = back ? rotated_ccw(g_build.facing) : rotated_cw(g_build.facing);
            } else {
                for (int turns = back ? 3 : 1; turns > 0; --turns)
                    bp.held = rotated_cw(*bp.held);
            }
        }
        if (ChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            LeaveBlueprintTool();
            bp.tool = Tool::Copy;
        }
        if (ChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            if (bp.clipboard)
                HoldBlueprint(*bp.clipboard);
            else
                SetMessage(std::string("Nothing copied yet: ") + kCtrlName + "+C copies an area");
        }
        if (ShortcutPressed(ImGuiKey_B))
            bp.show_library = !bp.show_library;
    }

    // The mouse wheel zooms the grid; it never scrolls the page.
    BeginFullscreenPage("##playing", ImGuiWindowFlags_NoScrollWithMouse);
    const float fh = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().WindowPadding.x * 2.0f;
    const float palette_w = fh * 11.0f;
    const float craft_w = fh * 12.0f;
    DrawBuildPalette(ship, palette_w);
    ImGui::SameLine(0.0f, gap);
    const ImVec2 column_min = ImGui::GetCursorScreenPos();
    ImVec2 column_avail = ImGui::GetContentRegionAvail();
    column_avail.x = std::max(0.0f, column_avail.x - craft_w - gap);
    // The middle column: the fleet's tabs, the grid under them, and the
    // hint bar under that.
    DrawFleetStrip(world);
    const float strip_h = fh + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 grid_min(column_min.x, column_min.y + strip_h);
    const ImVec2 grid_avail(column_avail.x, std::max(0.0f, column_avail.y - strip_h * 2.0f));
    ImGui::SetCursorScreenPos(grid_min);
    // The strip may have switched ships, or renamed this one.
    FleetShip& shown = CurrentShip(world.get_fleet());
    if (g_map.shown) {
        g_build.hover_cell.reset();
        DrawStarMap(world, shown, grid_avail);
    } else {
        DrawShipGrid(shown.ship, shown.battle, world.get_tick(), grid_avail);
    }
    DrawMilestoneBanner(world.get_fleet().get_progress(), grid_min, grid_avail);
    const float battle_h =
        g_map.shown ? 0.0f
                    : DrawBattleBanner(shown, world.get_map().at(shown.sector), world.get_tick(), grid_min, grid_avail);
    DrawToast(grid_min, grid_avail, battle_h);
    DrawHintBar(ImVec2(grid_min.x, grid_min.y + grid_avail.y + ImGui::GetStyle().ItemSpacing.y), grid_avail.x);
    ImGui::SetCursorScreenPos(ImVec2(column_min.x + column_avail.x + gap, column_min.y));
    DrawCraftPanel(world, shown, craft_w);
    if (!paused) {
        DrawBlueprintLibrary(blueprints_dir);
        DrawSendCargoWindow(world, shown);
        DrawBuildShipWindow(world, shown);
        DrawKeysWindow();
    }
    popup_was_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

    // The pause menu: a modal over the game, which dims it and takes the
    // mouse while it is open.
    constexpr const char* kPausePopup = "Paused";
    if (paused && !ImGui::IsPopupOpen(kPausePopup))
        ImGui::OpenPopup(kPausePopup);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(kPausePopup, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
        if (theme::PrimaryButton("Resume", button_size))
            paused = false;
        if (ImGui::Button("Main menu", button_size)) {
            paused = false;
            LeaveGame(screen);
        }
        if (!paused)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

namespace {

// The debug menu's item cheat: what to give, and how many.
struct DebugGiveState {
    ItemKind kind = ItemKind::TransportBelt;
    int count = 100;
    char search[64] = {};
};
DebugGiveState g_debug_give;

// The debug menu, while a game is on: pause and single-step the simulation,
// give the bridge any items, and complete milestones. It is a developer
// tool, so it is drawn in stock Dear ImGui (dark colours, default sizes),
// not the game's theme.
void DrawDebugWindow(World& world, DebugControls& debug, bool* open) {
    // Swap the stock style in for this window (and the popups it opens),
    // keeping the theme's font size and DPI scale so the text stays legible.
    ImGuiStyle& style = ImGui::GetStyle();
    const ImGuiStyle themed = style;
    style = ImGuiStyle();
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(themed.FontScaleDpi);
    style.FontSizeBase = themed.FontSizeBase;
    style.FontScaleDpi = themed.FontScaleDpi;

    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 14.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Debug", open)) {
        ImGui::SeparatorText("Simulation");
        const Tick tick = world.get_tick();
        ImGui::Text("Tick %llu (%.1f s)", static_cast<unsigned long long>(tick),
                    static_cast<double>(tick) / kTicksPerSecond);
        if (ImGui::Button(debug.sim_paused ? "Resume" : "Pause", ImVec2(fh * 4.0f, 0.0f)))
            debug.sim_paused = !debug.sim_paused;
        ImGui::SameLine();
        // Held down, the step buttons repeat.
        ImGui::BeginDisabled(!debug.sim_paused);
        ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
        if (ImGui::Button("Step"))
            debug.ticks_to_step += 1;
        ImGui::SameLine();
        if (ImGui::Button("Step 1 s"))
            debug.ticks_to_step += static_cast<int>(kTicksPerSecond);
        ImGui::PopItemFlag();
        ImGui::EndDisabled();
        if (debug.sim_paused)
            ImGui::TextDisabled("Simulation paused");

        ImGui::SeparatorText("Give items");
        DebugGiveState& give = g_debug_give;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##give_item", item_name(give.kind), ImGuiComboFlags_HeightLarge)) {
            // A filter box, emptied and focused as it opens; Enter picks the
            // first item left.
            if (ImGui::IsWindowAppearing()) {
                give.search[0] = '\0';
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool enter = ImGui::InputTextWithHint("##search", "Search", give.search, sizeof(give.search),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
            bool first = true;
            for (int i = 0; i < kItemKindCount; ++i) {
                const ItemKind kind = static_cast<ItemKind>(i);
                if (is_layout_part(kind) || !ContainsIgnoringCase(item_name(kind), give.search))
                    continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(item_name(kind), kind == give.kind) || (enter && first)) {
                    give.kind = kind;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
                first = false;
            }
            if (first)
                ImGui::TextDisabled("No matches");
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(fh * 6.0f);
        if (ImGui::InputInt("Count", &give.count, 1, 100))
            give.count = std::max(give.count, 1);
        if (world.fleet_lost()) {
            ImGui::TextDisabled("The fleet is lost");
            ImGui::End();
            style = themed;
            return;
        }
        FleetShip& current = CurrentShip(world.get_fleet());
        Inventory& inventory = current.ship.inventory();
        if (ImGui::Button("Give"))
            inventory.add(give.kind, give.count);
        ImGui::SameLine();
        if (ImGui::Button("Give every item"))
            for (int i = 0; i < kItemKindCount; ++i)
                if (!is_layout_part(static_cast<ItemKind>(i)))
                    inventory.add(static_cast<ItemKind>(i), give.count);
        ImGui::TextDisabled("In %s's bridge: %d", current.name.c_str(), inventory.count(give.kind));

        ImGui::SeparatorText("Travel");
        if (ImGui::Button("Reveal the map"))
            world.reveal_map();
        ImGui::SameLine();
        if (ImGui::Button("Charge thrusters")) {
            // Fed from the cell above, as from a belt.
            for (const GridPos p : current.ship.placement_order())
                if (dynamic_cast<const Thruster*>(current.ship.building_anchored_at(p)))
                    while (current.ship.insert_item(p, {p.x, p.y - 1}, ItemKind::FuelCell)) {
                    }
        }
        ImGui::SetItemTooltip("Fills every ion thruster on %s with fuel cells", current.name.c_str());

        ImGui::SeparatorText("Combat");
        if (ImGui::Button("Send a wave"))
            world.send_wave(current.id);
        ImGui::SetItemTooltip("Sends %s's sector's next wave (as if hostile) at %s now",
                              world.get_map().at(current.sector).name.c_str(), current.name.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Clear enemies"))
            world.clear_enemies(current.id);
        ImGui::Text("Enemies on %s: %zu", current.name.c_str(), current.battle.enemies.size());

        ImGui::SeparatorText("Milestones");
        Progress& progress = world.get_fleet().get_progress();
        ImGui::Text("Done: %zu / %zu", progress.current_index(), all_milestones().size());
        ImGui::BeginDisabled(progress.all_complete());
        if (ImGui::Button("Complete milestone"))
            progress.complete_current();
        ImGui::EndDisabled();

        ImGui::SeparatorText("Widgets");
        static bool show_imgui_demo = false;
        ImGui::Checkbox("ImGui demo window", &show_imgui_demo);
        if (show_imgui_demo)
            ImGui::ShowDemoWindow(&show_imgui_demo);
    }
    ImGui::End();
    style = themed;
}

}  // namespace

void DrawDevWindows(Screen screen, World& world, DebugControls& debug) {
    static bool show_debug = false;
    static bool show_implot_demo = false;
    if (ShortcutPressed(ImGuiKey_F1))
        show_debug = !show_debug;
    if (ShortcutPressed(ImGuiKey_F2))
        show_implot_demo = !show_implot_demo;
    if (show_debug && screen == Screen::Playing)
        DrawDebugWindow(world, debug, &show_debug);
    if (show_implot_demo)
        ImPlot::ShowDemoWindow(&show_implot_demo);
}
