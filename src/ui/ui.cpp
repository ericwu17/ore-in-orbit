// ui.cpp — the screens: main menu, save list, credits and the game screen
// (the ship build grid, its build palette and the crafting panel), plus the
// debug menu and the ImPlot demo window.
#include "ui/ui.h"
#include "ui/sprites.h"
#include "ui/ui_theme.h"

#include "persistence/save_slots.h"
#include "sim/ship.h"
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


// ---------------------------------------------------------------------------
// Game screen
// ---------------------------------------------------------------------------

// What the player is about to build. UI-only: the simulation never sees it.
struct BuildUiState {
    std::optional<ItemKind> selected = ItemKind::Hull;  // nullopt: nothing, clicks only inspect
    Direction facing = Direction::North;  // for rotatable buildings
    bool erase_hull = false;              // this right-drag removes hull, not buildings
    std::string message;                  // why the last click did nothing
    double message_time = -1.0e9;         // ImGui::GetTime() when it was set
    // The building shown in the inspector (clicked on the grid): its
    // top-left cell, and the building itself, to notice it went away.
    std::optional<GridPos> inspected;
    const Building* inspected_building = nullptr;
};
BuildUiState g_build;

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

// Pick `kind` to build, or put it away if it is already picked.
void ToggleSelected(ItemKind kind) {
    if (g_build.selected == kind)
        g_build.selected.reset();
    else
        g_build.selected = kind;
}

void SetBuildMessage(BuildResult result) {
    g_build.message = describe(result);
    g_build.message_time = ImGui::GetTime();
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

// Pipes visually join up with neighbors that carry fluid.
bool CarriesFluid(const Building* b) {
    if (!b)
        return false;
    const std::optional<ItemKind> kind = b->item_kind();
    return kind && handles_fluid(*kind);
}

// The short label on a building's tile, for the ones drawn as lettered tiles.
const char* TileLabel(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector: return "A";
    case ItemKind::TractorArray:      return "TA";
    case ItemKind::HydrogenCollector: return "H";
    case ItemKind::CargoHold:         return "C";
    case ItemKind::FluidTank:         return "FT";
    case ItemKind::AsteroidCrusher:   return "Cr";
    case ItemKind::SolarFurnace:      return "SF";
    case ItemKind::ArcFurnace:        return "AF";
    case ItemKind::IceMelter:         return "IM";
    case ItemKind::Electrolyzer:      return "El";
    case ItemKind::ChemicalPlant:     return "CP";
    case ItemKind::FabricatorMk1:     return "F1";
    case ItemKind::FabricatorMk2:     return "F2";
    case ItemKind::FabricatorMk3:     return "F3";
    case ItemKind::SolarPanel:        return "SP";
    case ItemKind::SolarArray:        return "SA";
    case ItemKind::FuelCellGenerator: return "FC";
    case ItemKind::BatteryBank:       return "BB";
    case ItemKind::SurveyScanner:     return "SS";
    case ItemKind::IonThruster:       return "IT";
    default:                          return "?";
    }
}

// The tile color of a lettered building, by family.
ImVec4 TileColor(ItemKind kind) {
    switch (kind) {
    case ItemKind::AsteroidCollector:
    case ItemKind::TractorArray:      return theme::kTileRockCollector;
    case ItemKind::HydrogenCollector: return theme::kTileH2Collector;
    case ItemKind::CargoHold:
    case ItemKind::FluidTank:         return theme::kTileStorage;
    case ItemKind::SolarPanel:
    case ItemKind::SolarArray:
    case ItemKind::FuelCellGenerator:
    case ItemKind::BatteryBank:       return theme::kTilePower;
    case ItemKind::SurveyScanner:
    case ItemKind::IonThruster:       return theme::kTileSystem;
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
// is nullopt for the bridge. `pos` is used to join pipes to their neighbors;
// `alpha` fades the placement ghost.
void DrawBuilding(ImDrawList* draw, const Ship& ship, GridPos pos, ImVec2 min, float cell, GridSize size,
                  std::optional<ItemKind> kind, Direction facing, float alpha) {
    const float inset = cell * 0.12f;
    const ImVec2 max = FootprintMax(min, cell, size);
    const ImVec2 a(min.x + inset, min.y + inset);
    const ImVec2 b(max.x - inset, max.y - inset);
    const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float rounding = cell * 0.15f;
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
    if (sprite && kind != ItemKind::Pipe) {
        DrawSprite(draw, *sprite, min, max, kind && is_rotatable(*kind) ? facing : Direction::North, alpha);
        return;
    }
    if (!kind) {
        letter_tile(theme::kTileBridge, "Bridge");
        return;
    }
    switch (*kind) {
    case ItemKind::Pipe: {
        // A hub (its sprite, when it has one) plus a bar toward each neighbor
        // that carries fluid.
        const ImU32 color = theme::U32(theme::kTilePipe, alpha);
        const float half = cell * 0.14f;
        if (!sprite)
            draw->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), color);
        for (const Direction dir : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const ImVec2 v = DirectionVector(dir);
            const GridPos n{pos.x + static_cast<int>(v.x), pos.y + static_cast<int>(v.y)};
            if (!CarriesFluid(ship.building_at(n)))
                continue;
            const ImVec2 edge(c.x + v.x * cell * 0.5f, c.y + v.y * cell * 0.5f);
            draw->AddRectFilled(ImVec2(std::min(c.x, edge.x) - half, std::min(c.y, edge.y) - half),
                                ImVec2(std::max(c.x, edge.x) + half, std::max(c.y, edge.y) + half), color);
        }
        if (sprite)
            DrawSprite(draw, *sprite, min, max, Direction::North, alpha);
        break;
    }
    case ItemKind::Pump:
        draw->AddRectFilled(a, b, theme::U32(theme::kTilePipe, alpha), rounding);
        DrawArrow(draw, c, d, cell * 0.28f, glyph);
        break;
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
        draw->AddRect(a, b, theme::U32(*kind == ItemKind::ReverseSorter ? theme::kRed : theme::kWarn, alpha),
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
    case ItemKind::MetallicChunk:
    case ItemKind::IronOre:
    case ItemKind::IronPlate:
    case ItemKind::Steel:           return theme::kItemIron;
    case ItemKind::CopperOre:
    case ItemKind::CopperPlate:
    case ItemKind::CopperWire:      return theme::kItemCopper;
    case ItemKind::NickelOre:
    case ItemKind::NickelPlate:     return theme::kItemNickel;
    case ItemKind::SilicateChunk:
    case ItemKind::Silica:
    case ItemKind::Glass:
    case ItemKind::Silicon:         return theme::kItemSilica;
    case ItemKind::CarbonaceousChunk:
    case ItemKind::Carbon:          return theme::kItemCarbon;
    case ItemKind::IceChunk:
    case ItemKind::WaterIce:        return theme::kItemIce;
    case ItemKind::ExoticChunk:
    case ItemKind::IridiumOre:
    case ItemKind::IridiumPlate:    return theme::kItemExotic;
    case ItemKind::Plastic:
    case ItemKind::Battery:
    case ItemKind::PropellantCell:  return theme::kItemChemical;
    default:
        return item_category(kind) == ItemCategory::Placeable ? theme::kItemPlaceable : theme::kItemComponent;
    }
}

// One item, centered on `c`, about `r` across: its sprite, or without one,
// chunks and raw resources as round lumps, everything made from them as
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
    if (category == ItemCategory::AsteroidChunk || category == ItemCategory::RawResource) {
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

// What an icon in a panel shows: an item, a fluid, a building as built, or
// nothing ("None" in a picker).
struct Icon {
    enum class Type {
        None,
        Item,
        Fluid,
        Building,
        Bridge,
    };
    Type type = Type::None;
    ItemKind item = ItemKind::Hull;
    FluidKind fluid = FluidKind::Water;
};

Icon ItemIcon(ItemKind kind) {
    Icon icon;
    icon.type = Icon::Type::Item;
    icon.item = kind;
    return icon;
}

Icon FluidIcon(FluidKind kind) {
    Icon icon;
    icon.type = Icon::Type::Fluid;
    icon.fluid = kind;
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
// what they do ("Crush metallic chunk", "Split water"), what goes in.
Icon RecipeIcon(const Recipe& r) {
    if (!r.name && !r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    if (!r.inputs.empty())
        return ItemIcon(r.inputs.front().kind);
    if (!r.fluid_inputs.empty())
        return FluidIcon(r.fluid_inputs.front().kind);
    if (!r.outputs.empty())
        return ItemIcon(r.outputs.front().kind);
    if (!r.fluid_outputs.empty())
        return FluidIcon(r.fluid_outputs.front().kind);
    return {};
}

const char* IconName(const Icon& icon) {
    switch (icon.type) {
    case Icon::Type::None:     return "None";
    case Icon::Type::Item:
    case Icon::Type::Building: return item_name(icon.item);
    case Icon::Type::Fluid:    return fluid_name(icon.fluid);
    case Icon::Type::Bridge:   return "Bridge";
    }
    return "?";
}

const char* CategoryName(ItemCategory category) {
    switch (category) {
    case ItemCategory::Placeable:     return "Buildings";
    case ItemCategory::AsteroidChunk: return "Asteroid chunks";
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
        const ImU32 color = theme::U32(theme::kInkDim, alpha);
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
    case Icon::Type::Fluid:
        if (const sprites::Sprite* s = sprites::Fluid(icon.fluid))
            DrawSprite(draw, *s, min, max, Direction::North, alpha);
        else
            draw->AddCircleFilled(c, r * 0.6f, theme::U32(theme::kItemIce, alpha));
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

// A square tile `size` across at `min`: `bg`, the icon over it (faded by
// `alpha`) and its badges. Disabled widgets fade all of it.
void DrawIconFrame(ImDrawList* draw, const Icon& icon, ImVec2 min, float size, ImVec4 bg, Badges badges,
                   float alpha) {
    const float style_alpha = ImGui::GetStyle().Alpha;
    const ImVec2 max(min.x + size, min.y + size);
    draw->AddRectFilled(min, max, theme::U32(bg, style_alpha), ImGui::GetStyle().FrameRounding);
    const float pad = size * 0.08f;
    DrawIcon(draw, icon, ImVec2(min.x + pad, min.y + pad), ImVec2(max.x - pad, max.y - pad), style_alpha * alpha);
    if (!badges.top_left && !badges.bottom_right)
        return;
    ImGui::PushFont(nullptr, theme::kFontSmall);
    const ImU32 ink = theme::U32(theme::kInk, style_alpha);
    const ImU32 halo = theme::U32(theme::kPaperLight, style_alpha);
    if (badges.top_left)
        theme::DrawHaloText(draw, ImVec2(min.x + 3.0f, min.y + 1.0f), ink, halo, badges.top_left);
    if (badges.bottom_right) {
        const ImVec2 ts = ImGui::CalcTextSize(badges.bottom_right);
        theme::DrawHaloText(draw, ImVec2(max.x - ts.x - 3.0f, max.y - ts.y - 1.0f), ink, halo, badges.bottom_right);
    }
    ImGui::PopFont();
}

// A square button `size` across showing `icon`, filled navy when selected
// (like theme::SegmentedButton), with `badges` and the icon faded by `fade`.
// Returns true when clicked.
bool IconButton(const char* id, const Icon& icon, float size, bool selected, Badges badges = {},
                float fade = 1.0f) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec4 bg = selected                 ? (hovered ? theme::kNavyHover : theme::kNavy)
                      : ImGui::IsItemActive() ? theme::kPaper4
                      : hovered               ? theme::kPaper3
                                              : theme::kPaper2;
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, bg, badges, fade);
    return pressed;
}

// An icon framed like IconButton, only to look at (and hover).
void IconTile(const Icon& icon, float size, Badges badges = {}) {
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    DrawIconFrame(ImGui::GetWindowDrawList(), icon, min, size, theme::kPaper2, badges, 1.0f);
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

// One line about an amount of an item or fluid: its icon, then `text` ("3",
// "2 / 4") and `suffix`, in `color`. Hovering the icon shows its name;
// `named` writes the name out after the text too, for tooltips, where
// nothing can be hovered.
void AmountLine(const Icon& icon, const std::string& text, ImVec4 color, bool named, const char* suffix = "") {
    ImGui::AlignTextToFramePadding();
    IconImage(icon, ImGui::GetFrameHeight());
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextColored(color, "%s%s%s%s", text.c_str(), named ? " " : "", named ? IconName(icon) : "", suffix);
}

// The placeable items in the build list: those in the inventory, plus the
// selected one. The first nine get the number keys.
std::vector<ItemKind> BuildList(const Ship& ship) {
    std::vector<ItemKind> list;
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        if (is_placeable(kind) && (ship.inventory().count(kind) > 0 || kind == g_build.selected))
            list.push_back(kind);
    }
    return list;
}

// The left panel: an icon per placeable item in the inventory with its count,
// what is selected and its facing, the cargo (everything else in the
// inventory) and the controls.
void DrawBuildPalette(const Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawShadowedPanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                             ImGui::GetStyle().WindowRounding);

    ImGui::BeginChild("##build_palette", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::SectionHeading("Build");

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
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted(item_name(kind));
            const GridSize footprint = footprint_of(kind);
            ImGui::TextColored(theme::kInkDim, "%d in the bridge, %dx%d", count, footprint.w, footprint.h);
            if (i < 9)
                ImGui::TextColored(theme::kInkDim, "Key %s", key.c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (g_build.selected)
        ImGui::TextUnformatted(item_name(*g_build.selected));
    else
        ImGui::TextColored(theme::kInkDim, "Nothing selected");
    if (g_build.selected && is_rotatable(*g_build.selected))
        ImGui::Text("Facing: %s  (R to rotate)", DirectionName(g_build.facing));
    else
        ImGui::TextColored(theme::kInkDim, "Facing: -");

    ImGui::Spacing();
    if (!g_build.message.empty() && ImGui::GetTime() - g_build.message_time < kMessageSeconds)
        ImGui::TextColored(theme::kBad, "%s", g_build.message.c_str());
    else
        ImGui::TextUnformatted("");

    theme::SectionHeading("Cargo");
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
        ImGui::TextColored(theme::kInkDim, "Empty");

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kInkDim,
                       "Left click / drag: build\nLeft click a building: inspect it\n"
                       "(pick a machine's recipe or a sorter's filter)\nRight click / drag: remove a building;\n"
                       "again to remove the hull (refunds)\n"
                       "Scroll: zoom\nMiddle drag, or hold Space\nand move the mouse: pan\n"
                       "1-9: pick item   R: rotate\nQ, or the same key again:\nbuild nothing\nEsc: pause menu\n\n"
                       "Everything but hull needs a hull tile under it; the largest buildings need "
                       "reinforced hull.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// What goes into a recipe and what it makes, item by item, for its tooltip.
// `inv` colors inputs by whether the bridge has them; null leaves them plain.
void RecipeDetails(const Recipe& recipe, const Inventory* inv) {
    ImGui::TextColored(theme::kInkDim, "%s, %.1f s", craft_category_name(recipe.category),
                       static_cast<double>(recipe.ticks) / static_cast<double>(kTicksPerSecond));
    for (const ItemStack& s : recipe.inputs) {
        if (inv) {
            const int have = inv->count(s.kind);
            const std::string suffix = "  (have " + std::to_string(have) + ")";
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), have >= s.count ? theme::kInk : theme::kBad, true,
                       suffix.c_str());
        } else {
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kInk, true);
        }
    }
    for (const FluidStack& s : recipe.fluid_inputs)
        AmountLine(FluidIcon(s.kind), std::to_string(s.amount), theme::kInk, true);
    ImGui::TextColored(theme::kInkDim, "Makes");
    for (const ItemStack& s : recipe.outputs)
        AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, true);
    for (const FluidStack& s : recipe.fluid_outputs)
        AmountLine(FluidIcon(s.kind), std::to_string(s.amount), theme::kGood, true);
}

// A recipe's tooltip, over the item just drawn: its name and details.
void RecipeTooltip(const Recipe& recipe, const Inventory* inv, const char* hint = nullptr) {
    if (!ImGui::BeginItemTooltip())
        return;
    ImGui::TextUnformatted(recipe_name(recipe));
    RecipeDetails(recipe, inv);
    if (hint)
        ImGui::TextColored(theme::kInkDim, "%s", hint);
    ImGui::EndTooltip();
}

// One icon button per hand-craftable recipe whose main output is in
// `category`.
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
        flow.Next();
        ImGui::PushID(static_cast<int>(i));
        ImGui::BeginDisabled(!craftable);
        if (IconButton("##craft", RecipeIcon(r), size, false, {nullptr, makes > 1 ? badge.c_str() : nullptr})) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            for (int k = 0; k < n && bridge.queue_craft(r); ++k) {
            }
        }
        ImGui::EndDisabled();
        RecipeTooltip(r, &bridge.get_inventory());
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

    const float bar_w = ImGui::GetFrameHeight() * 8.0f;
    if (const auto* machine = dynamic_cast<const Machine*>(&b)) {
        std::string crafts;
        for (int c = 0; c < kCraftCategoryCount; ++c)
            if (machine->crafts(static_cast<CraftCategory>(c)))
                crafts += std::string(crafts.empty() ? "" : ", ") + craft_category_name(static_cast<CraftCategory>(c));
        ImGui::TextColored(theme::kInkDim, "%s, speed x%d", crafts.c_str(), machine->crafting_speed());
        const Recipe* recipe = machine->get_recipe();
        if (!recipe) {
            ImGui::TextColored(theme::kWarnText, "No recipe: click it to pick one");
        } else {
            char rate[32];
            std::snprintf(rate, sizeof(rate), "  (%.1f / min)", machine->crafts_per_minute());
            AmountLine(RecipeIcon(*recipe), recipe_name(*recipe), theme::kInk, false, rate);
            ImGui::ProgressBar(machine->craft_progress(), ImVec2(bar_w, 0.0f), machine->crafting() ? "" : "Waiting");
            ImGui::TextColored(theme::kInkDim, "Ingredients (per craft)");
            for (const ItemStack& s : recipe->inputs) {
                const int have = machine->get_input().count(s.kind);
                AmountLine(ItemIcon(s.kind), Fraction(have, s.count), have >= s.count ? theme::kInk : theme::kInkDim,
                           in_tooltip);
            }
            for (const FluidStack& f : recipe->fluid_inputs)
                AmountLine(FluidIcon(f.kind), Fraction(machine->fluid_input(f.kind), f.amount), theme::kInkDim,
                           in_tooltip, "  (no pipe flow yet)");
            ImGui::TextColored(theme::kInkDim, "Waiting to go out");
            bool any = false;
            for (const ItemStack& s : machine->get_output().stacks()) {
                AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kGood, in_tooltip);
                any = true;
            }
            for (const FluidStack& f : recipe->fluid_outputs)
                if (const int amount = machine->fluid_output(f.kind); amount > 0) {
                    AmountLine(FluidIcon(f.kind), std::to_string(amount), theme::kGood, in_tooltip);
                    any = true;
                }
            if (!any)
                ImGui::TextColored(theme::kInkDim, "  -");
        }
    }
    if (const auto* collector = dynamic_cast<const Collector*>(&b)) {
        const CollectorSpec& spec = collector->get_spec();
        const double seconds = static_cast<double>(spec.interval) / static_cast<double>(kTicksPerSecond);
        if (spec.fluid)
            ImGui::TextColored(theme::kInkDim, "%d %s every %.1f s  (%.0f / s)", spec.fluid_amount,
                               fluid_name(*spec.fluid), seconds, collector->rate_per_second());
        else
            ImGui::TextColored(theme::kInkDim, "1 chunk every %.1f s  (%.0f / min)", seconds,
                               collector->rate_per_second() * 60.0);
        const bool full = collector->full();
        ImGui::ProgressBar(full ? 1.0f : collector->catch_progress(), ImVec2(bar_w, 0.0f), full ? "Buffer full" : "");
        ImGui::Text("Buffer: %d / %d", collector->stored(), collector->capacity());
        for (const ItemStack& s : collector->items().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kInk, in_tooltip);
    }
    if (const auto* belt = dynamic_cast<const Conveyor*>(&b)) {
        if (!belt->moves_items()) {
            ImGui::TextColored(theme::kInkDim, "Doesn't move items yet");
        } else {
            ImGui::TextColored(theme::kInkDim, "%.0f cells / s",
                               static_cast<double>(belt->speed() * static_cast<int>(kTicksPerSecond)) / kBeltLength);
            Inventory carried;
            for (const BeltItem& item : belt->get_items())
                carried.add(item.kind);
            for (const ItemStack& s : carried.stacks())
                AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kInk, in_tooltip);
        }
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(&b)) {
        const std::optional<ItemKind> filter = sorter->get_filter();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Filter:");
        ImGui::SameLine();
        AmountLine(filter ? ItemIcon(*filter) : Icon{}, filter ? item_name(*filter) : "none", theme::kInk, false);
        if (sorter->inverted())
            ImGui::TextColored(theme::kInkDim, "Matching items turn left or right;\nothers go straight through");
        else
            ImGui::TextColored(theme::kInkDim, "Matching items go straight through;\nothers turn left or right");
    }
    if (const auto* junction = dynamic_cast<const Junction*>(&b)) {
        ImGui::TextColored(theme::kInkDim, "Items cross straight over, out the\nopposite side");
        Inventory crossing;
        for (const Direction travel : {Direction::North, Direction::East, Direction::South, Direction::West})
            for (const JunctionItem& item : junction->lane(travel))
                crossing.add(item.kind);
        for (const ItemStack& s : crossing.stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kInk, in_tooltip);
    }
    if (const auto* router = dynamic_cast<const Router*>(&b)) {
        ImGui::TextColored(theme::kInkDim, "Passes items to its other three sides\nin turn");
        if (const std::optional<ItemKind> held = router->get_held())
            AmountLine(ItemIcon(*held), item_name(*held), theme::kInk, in_tooltip);
    }
    if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
        ImGui::Text("Stored: %d / %d", hold->get_stored().total(), kCargoCapacity);
        for (const ItemStack& s : hold->get_stored().stacks())
            AmountLine(ItemIcon(s.kind), std::to_string(s.count), theme::kInk, in_tooltip);
    }
    if (!kind)
        ImGui::TextColored(theme::kInkDim, "Belts deliver into its inventory");
    if (b.rotatable())
        ImGui::TextColored(theme::kInkDim, "Facing %s", DirectionName(b.facing()));
}

// Whether `text` contains `part`, ignoring case.
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
        ImGui::TextColored(theme::kInkDim, "No matches");
    ImGui::EndChild();
}

// Every item as a search option, by category, each under a heading; the
// one picked, if any.
std::optional<ItemKind> ComboSearchItemOptions(std::optional<ItemKind> current) {
    std::optional<ItemKind> picked;
    for (const ItemCategory category : {ItemCategory::Placeable, ItemCategory::AsteroidChunk,
                                        ItemCategory::RawResource, ItemCategory::Material,
                                        ItemCategory::Component}) {
        bool any = false;
        for (int i = 0; i < kItemKindCount && !any; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            any = item_category(kind) == category && ComboSearchMatches(item_name(kind));
        }
        if (!any)
            continue;
        ComboSearchHeading(CategoryName(category));
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            if (item_category(kind) != category || !ComboSearchShows(item_name(kind)))
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
// machine, the recipe picker; for a sorter, the filter picker.
void DrawInspector(Ship& ship) {
    const Building* b = InspectedBuilding(ship);
    if (!b)
        return;
    theme::SectionHeading("Inspect");
    BuildingDetails(*b, false);
    if (const auto* machine = dynamic_cast<const Machine*>(b)) {
        const Recipe* current = machine->get_recipe();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##recipe", current ? RecipeIcon(*current) : Icon{},
                           current ? recipe_name(*current) : "Pick a recipe")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, current == nullptr))
                    ship.set_recipe(*g_build.inspected, nullptr);
                ImGui::SetItemTooltip("None");
            }
            const std::vector<const Recipe*> recipes = machine_recipes(*machine->item_kind());
            for (std::size_t i = 0; i < recipes.size(); ++i) {
                const Recipe* r = recipes[i];
                if (!ComboSearchShows(recipe_name(*r)))
                    continue;
                ImGui::PushID(static_cast<int>(i));
                if (ComboSearchOption(RecipeIcon(*r), r == current) && r != current)
                    ship.set_recipe(*g_build.inspected, r);
                RecipeTooltip(*r, nullptr);
                ImGui::PopID();
            }
            EndComboSearch();
            ImGui::EndCombo();
        }
        ImGui::TextColored(theme::kInkDim, "Changing it returns held ingredients to the bridge.");
    }
    if (const auto* sorter = dynamic_cast<const Sorter*>(b)) {
        const std::optional<ItemKind> current = sorter->get_filter();
        ImGui::SetNextItemWidth(-1.0f);
        if (BeginIconCombo("##filter", current ? ItemIcon(*current) : Icon{},
                           current ? item_name(*current) : "Pick a filter")) {
            BeginComboSearch();
            if (ComboSearchShows("None")) {
                if (ComboSearchOption(Icon{}, !current))
                    ship.set_sort_filter(*g_build.inspected, std::nullopt);
                ImGui::SetItemTooltip("None");
            }
            if (const std::optional<ItemKind> picked = ComboSearchItemOptions(current))
                ship.set_sort_filter(*g_build.inspected, *picked);
            EndComboSearch();
            ImGui::EndCombo();
        }
    }
    if (ImGui::Button("Close##inspector", ImVec2(-1.0f, 0.0f))) {
        g_build.inspected.reset();
        g_build.inspected_building = nullptr;
    }
}

// The right panel: the inspector, hand-crafting at the bridge, its queue,
// and the recipe book for everything else.
void DrawCraftPanel(Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawShadowedPanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                             ImGui::GetStyle().WindowRounding);

    ImGui::BeginChild("##craft_panel", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    Bridge& bridge = ship.get_bridge();
    ImGui::PushTextWrapPos(0.0f);
    DrawInspector(ship);
    ImGui::PopTextWrapPos();

    const float icon = ImGui::GetFrameHeight() * 1.6f;
    theme::SectionHeading("Craft queue");
    const std::deque<CraftJob>& queue = bridge.craft_queue();
    if (queue.empty()) {
        ImGui::TextColored(theme::kInkDim, "Idle");
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
                    theme::U32(theme::kWarn));
            }
            if ((clicked && i > 0) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
                cancel = i;
            ImGui::SetItemTooltip("%s\n%s", recipe_name(*job.recipe),
                                  i == 0 ? "Crafting: right click to cancel and refund" : "Click to cancel and refund");
            ImGui::PopID();
        }
        if (queue.size() > kShown)
            ImGui::TextColored(theme::kInkDim, "... and %zu more", queue.size() - kShown);
        if (cancel)
            bridge.cancel_craft(*cancel);
    }

    theme::SectionHeading("Hand-craft");
    ImGui::TextColored(theme::kInkDim, "Click: 1   Shift-click: 5");
    ImGui::SeparatorText("Components");
    DrawHandCraftButtons(bridge, ItemCategory::Component);
    ImGui::SeparatorText("Buildings");
    DrawHandCraftButtons(bridge, ItemCategory::Placeable);

    theme::SectionHeading("Recipe book");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kInkDim, "Made in machines: click one on the grid to pick its recipe.");
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
            IconTile(RecipeIcon(r), icon);
            RecipeTooltip(r, &bridge.get_inventory());
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
}

// A hull tile (`kind`: Hull or ReinforcedHull) over `min`..`max`: its sprite,
// or flat `hull` / `reinforced`, also when zoomed out too far for the sprite
// to read.
void DrawHull(ImDrawList* draw, ItemKind kind, ImVec2 min, ImVec2 max, float cell, float alpha, ImU32 hull,
              ImU32 reinforced) {
    const sprites::Sprite* sprite = sprites::Building(kind);
    if (sprite && cell >= 8.0f)
        DrawSprite(draw, *sprite, min, max, Direction::North, alpha);
    else
        draw->AddRectFilled(min, max, kind == ItemKind::ReinforcedHull ? reinforced : hull);
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
        // The bridge sits in the middle of the grid.
        cam.cell = std::clamp(ImGui::GetFrameHeight(), min_cell, max_cell);
        const float bridge_half = static_cast<float>(kBridgeSize) * 0.5f;
        const ImVec2 center((static_cast<float>(kStartBridgePos.x) + bridge_half) * cam.cell,
                            (static_cast<float>(kStartBridgePos.y) + bridge_half) * cam.cell);
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

// The build grid on a canvas filling `avail`: click / drag to build and
// remove, scroll to zoom, middle drag (or hold Space and move) to pan.
void DrawShipGrid(Ship& ship, ImVec2 avail) {
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

    std::optional<GridPos> hover_cell;
    if (ImGui::IsItemHovered() && !panning) {
        const ImVec2 m = ImGui::GetMousePos();
        const GridPos p{static_cast<int>(std::floor((m.x - origin.x) / cell)),
                        static_cast<int>(std::floor((m.y - origin.y) / cell))};
        if (ship.in_bounds(p))
            hover_cell = p;
    }

    // Input: a click reports why it failed; dragging just skips cells that
    // don't work, so painting a row over existing tiles stays quiet.
    // Only while the press started on the grid (the button is active).
    if (hover_cell && ImGui::IsItemActive()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // With nothing selected, nothing gets built: a click only inspects.
            const std::optional<BuildResult> r =
                g_build.selected ? std::optional(ship.place(*g_build.selected, *hover_cell, g_build.facing))
                                 : std::nullopt;
            if (r != BuildResult::Ok && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // Clicking a building (rather than building on it) inspects it.
                if (const Building* b = ship.building_at(*hover_cell)) {
                    g_build.inspected = ship.anchor_of(*hover_cell);
                    g_build.inspected_building = b;
                } else if (r) {
                    SetBuildMessage(*r);
                }
            }
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            // One layer per press, picked where it starts: on a building it
            // only removes buildings, so a second press takes the hull.
            const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (clicked)
                g_build.erase_hull = ship.building_at(*hover_cell) == nullptr;
            const BuildResult r =
                g_build.erase_hull ? ship.remove_hull(*hover_cell) : ship.remove_building(*hover_cell);
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
    const ImU32 hull = theme::U32(theme::kTileHull);
    const ImU32 reinforced = theme::U32(theme::kTileReinforcedHull);
    // The grid as whole lines (one per row / column, not an outline per
    // cell: the grid is large), then the hull filled over it. Zoomed far
    // out, only the grid's border.
    if (cell >= 4.0f) {
        const ImVec2 top_left = CellMin(origin, cell, lo);
        const ImVec2 bottom_right = CellMin(origin, cell, {hi.x + 1, hi.y + 1});
        for (int x = lo.x; x <= hi.x + 1; ++x) {
            const float t = origin.x + static_cast<float>(x) * cell;
            draw->AddLine(ImVec2(t, top_left.y), ImVec2(t, bottom_right.y), grid_line);
        }
        for (int y = lo.y; y <= hi.y + 1; ++y) {
            const float t = origin.y + static_cast<float>(y) * cell;
            draw->AddLine(ImVec2(top_left.x, t), ImVec2(bottom_right.x, t), grid_line);
        }
    } else {
        draw->AddRect(origin, ImVec2(origin.x + side, origin.y + side), grid_line);
    }
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const GridPos p{x, y};
            if (const std::optional<ItemKind> tile = ship.hull_at(p)) {
                const ImVec2 min = CellMin(origin, cell, p);
                DrawHull(draw, *tile, min, ImVec2(min.x + cell, min.y + cell), cell, 1.0f, hull, reinforced);
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
                DrawBuilding(draw, ship, {x, y}, min, cell, b->footprint(), b->item_kind(), b->facing(), 1.0f);
                const ImVec2 max = FootprintMax(min, cell, b->footprint());
                if (const auto* collector = dynamic_cast<const Collector*>(b))
                    DrawBufferGauge(draw, min, max, cell,
                                    static_cast<float>(collector->stored()) /
                                        static_cast<float>(collector->capacity()));
                if (const auto* machine = dynamic_cast<const Machine*>(b))
                    DrawMachineStatus(draw, *machine, min, max, cell);
                if (const auto* sorter = dynamic_cast<const Sorter*>(b))
                    DrawSorterFilter(draw, *sorter, min, cell);
                if (b == InspectedBuilding(ship))
                    draw->AddRect(min, max, theme::U32(theme::kRed), 0.0f, 0, 2.0f);
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

    // Hover preview: a ghost of what would be built (its footprint starting
    // at the hovered cell), outlined in good / bad. Clipped to the grid.
    if (hover_cell) {
        if (const std::optional<ItemKind> selected = g_build.selected) {
            const GridSize size = footprint_of(*selected, g_build.facing);
            const ImVec2 min = CellMin(origin, cell, *hover_cell);
            const ImVec2 max = FootprintMax(min, cell, size);
            const BuildResult r = ship.can_place(*selected, *hover_cell, g_build.facing);
            draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);  // within the canvas clip
            if (r == BuildResult::Ok) {
                if (*selected == ItemKind::Hull || *selected == ItemKind::ReinforcedHull)
                    DrawHull(draw, *selected, min, max, cell, 0.6f, theme::U32(theme::kTileHull, 0.6f),
                             theme::U32(theme::kTileReinforcedHull, 0.6f));
                else
                    DrawBuilding(draw, ship, *hover_cell, min, cell, size, *selected, g_build.facing, 0.45f);
            }
            draw->AddRect(min, max, theme::U32(r == BuildResult::Ok ? theme::kGood : theme::kBad), 0.0f, 0, 2.0f);
            draw->PopClipRect();
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            if (const Building* b = ship.building_at(*hover_cell)) {
                ImGui::BeginTooltip();
                BuildingDetails(*b, true);
                ImGui::EndTooltip();
            }
    }
    draw->PopClipRect();
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

// `s` without whitespace at either end.
std::string Trimmed(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

}  // namespace

void DrawMainMenu(Screen& screen) {
    BeginFullscreenPage("##main_menu");

    const float fh = ImGui::GetFrameHeight();  // follows font size / DPI scaling
    const ImVec2 button_size = {fh * 8.0f, fh * 1.6f};
    const float spacing = ImGui::GetStyle().ItemSpacing.y;

    // Title plus two buttons, as one block centered vertically.
    ImGui::PushFont(nullptr, theme::kFontHuge);
    const float title_h = ImGui::GetTextLineHeight();
    ImGui::PopFont();
    const float block_h = title_h + fh * 1.5f + 2.0f * button_size.y + spacing;
    ImGui::SetCursorPosY((ImGui::GetWindowHeight() - block_h) * 0.5f);

    ImGui::PushFont(nullptr, theme::kFontHuge);
    CenteredText("ORE IN ORBIT", theme::kInk);
    ImGui::PopFont();
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
        ImGui::TextColored(theme::kInkDim, "No saves yet: start a new game.");
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
                ImGui::TextColored(theme::kInkDim, "%s", slot.path.filename().string().c_str());
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
        if (theme::PrimaryButton("Delete")) {
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

void DrawPlaying(Screen& screen, World& world, bool& paused) {
    // Escape opens the pause menu, or closes it again. Not when it closed a
    // dropdown or tooltip popup: ImGui already did that before we got here,
    // so look at whether one was open last frame.
    static bool popup_was_open = false;
    if (ShortcutPressed(ImGuiKey_Escape) && (paused || !popup_was_open))
        paused = !paused;

    Ship& ship = world.get_ship();
    if (!paused) {
        const std::vector<ItemKind> list = BuildList(ship);
        for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 9); ++i)
            if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i))))
                ToggleSelected(list[i]);
        if (ShortcutPressed(ImGuiKey_Q))
            g_build.selected.reset();
        if (ShortcutPressed(ImGuiKey_R))
            g_build.facing = rotated_cw(g_build.facing);
    }

    // The mouse wheel zooms the grid; it never scrolls the page.
    BeginFullscreenPage("##playing", ImGuiWindowFlags_NoScrollWithMouse);
    const float fh = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().WindowPadding.x * 2.0f;
    const float palette_w = fh * 11.0f;
    const float craft_w = fh * 12.0f;
    DrawBuildPalette(ship, palette_w);
    ImGui::SameLine(0.0f, gap);
    const ImVec2 grid_min = ImGui::GetCursorScreenPos();
    ImVec2 grid_avail = ImGui::GetContentRegionAvail();
    grid_avail.x = std::max(0.0f, grid_avail.x - craft_w - gap);
    DrawShipGrid(ship, grid_avail);
    ImGui::SetCursorScreenPos(ImVec2(grid_min.x + grid_avail.x + gap, grid_min.y));
    DrawCraftPanel(ship, craft_w);
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
            screen = Screen::MainMenu;
            g_camera = {};  // the next game starts centred on its bridge
            g_build.inspected.reset();
            g_build.inspected_building = nullptr;
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
    ItemKind kind = ItemKind::Hull;
    int count = 100;
    char search[64] = {};
};
DebugGiveState g_debug_give;

// The debug menu, while a game is on: pause and single-step the simulation,
// and give the bridge any items. It is a developer tool, so it is drawn in
// stock Dear ImGui (dark colours, default sizes), not the game's theme.
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
                if (!ContainsIgnoringCase(item_name(kind), give.search))
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
        Inventory& inventory = world.get_ship().inventory();
        if (ImGui::Button("Give"))
            inventory.add(give.kind, give.count);
        ImGui::SameLine();
        if (ImGui::Button("Give every item"))
            for (int i = 0; i < kItemKindCount; ++i)
                inventory.add(static_cast<ItemKind>(i), give.count);
        ImGui::TextDisabled("In the bridge: %d", inventory.count(give.kind));

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
