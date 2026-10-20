// ui.cpp — the screens: main menu, credits and the game screen (the ship
// build grid, its build palette and the crafting panel), plus the developer
// demo windows.
#include "ui/ui.h"
#include "ui/ui_theme.h"

#include "sim/ship.h"
#include "sim/world.h"

#include "imgui.h"
#include "implot.h"  // ImPlot::ShowDemoWindow

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace theme = ui::theme;

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
    ItemKind selected = ItemKind::Hull;
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
    bool left_pans = false;  // the current left drag started with Space held: it pans
};
Camera g_camera;

// Mouse-wheel zoom steps multiply the cell size by this.
constexpr float kZoomStep = 1.2f;

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

// Draws one building over its footprint, whose top-left cell is at `min`
// (cells of side `cell`). `kind` is nullopt for the bridge. `pos` is used to
// join pipes to their neighbors; `alpha` fades the placement ghost.
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

    if (!kind) {
        letter_tile(theme::kTileBridge, "B");
        return;
    }
    switch (*kind) {
    case ItemKind::Pipe: {
        // A hub plus a bar toward each neighbor that carries fluid.
        const ImU32 color = theme::U32(theme::kTilePipe, alpha);
        const float half = cell * 0.14f;
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
        break;
    }
    case ItemKind::Pump:
        draw->AddRectFilled(a, b, theme::U32(theme::kTilePipe, alpha), rounding);
        DrawArrow(draw, c, d, cell * 0.28f, glyph);
        break;
    case ItemKind::TransportBelt:
    case ItemKind::Splitter:
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
        conveyor(theme::kTileBelt);
        draw->AddRect(a, b, theme::U32(theme::kWarn, alpha), rounding * 0.5f, 0, std::max(1.0f, cell * 0.08f));
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

// One item, centered on `c`, `r` across: chunks and raw resources as round
// lumps, everything made from them as squares.
void DrawItem(ImDrawList* draw, ImVec2 c, float r, ItemKind kind) {
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

// The left panel: one button per placeable item in the inventory with its
// count, the facing, the cargo (everything else in the inventory) and the
// controls.
void DrawBuildPalette(const Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawShadowedPanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                             ImGui::GetStyle().WindowRounding);

    ImGui::BeginChild("##build_palette", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::SectionHeading("Build");

    const float button_w = ImGui::GetContentRegionAvail().x;
    const std::vector<ItemKind> list = BuildList(ship);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const ItemKind kind = list[i];
        const int count = ship.inventory().count(kind);
        char label[96];
        if (i < 9)
            std::snprintf(label, sizeof(label), "%zu  %s  x%d##item%d", i + 1, item_name(kind), count,
                          static_cast<int>(kind));
        else
            std::snprintf(label, sizeof(label), "    %s  x%d##item%d", item_name(kind), count, static_cast<int>(kind));
        if (count == 0)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kInkDim);
        if (theme::SegmentedButton(label, g_build.selected == kind, ImVec2(button_w, 0.0f)))
            g_build.selected = kind;
        if (count == 0)
            ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (is_rotatable(g_build.selected))
        ImGui::Text("Facing: %s  (R to rotate)", DirectionName(g_build.facing));
    else
        ImGui::TextColored(theme::kInkDim, "Facing: -");

    ImGui::Spacing();
    if (!g_build.message.empty() && ImGui::GetTime() - g_build.message_time < kMessageSeconds)
        ImGui::TextColored(theme::kBad, "%s", g_build.message.c_str());
    else
        ImGui::TextUnformatted("");

    theme::SectionHeading("Cargo");
    bool any = false;
    if (ImGui::BeginTable("##cargo", 2, ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < kItemKindCount; ++i) {
            const ItemKind kind = static_cast<ItemKind>(i);
            const int count = ship.inventory().count(kind);
            if (is_placeable(kind) || count == 0)
                continue;
            any = true;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(item_name(kind));
            ImGui::TableNextColumn();
            ImGui::TextColored(theme::kNavy, "%d", count);
        }
        ImGui::EndTable();
    }
    if (!any)
        ImGui::TextColored(theme::kInkDim, "Empty");

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kInkDim,
                       "Left click / drag: build\nLeft click a building: inspect it\n"
                       "(pick a machine's recipe)\nRight click / drag: remove a building;\n"
                       "again to remove the hull (refunds)\n"
                       "Scroll: zoom\nMiddle drag / Space + drag: pan\n"
                       "1-9: pick item   R: rotate\nEsc: main menu\n\n"
                       "Everything but hull needs a hull tile under it; the largest buildings need "
                       "reinforced hull.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// Ingredient / product lines for a recipe tooltip. `inv` colors inputs by
// whether the bridge has them; null leaves them plain.
void RecipeDetails(const Recipe& recipe, const Inventory* inv) {
    ImGui::TextColored(theme::kInkDim, "%s, %.1f s", craft_category_name(recipe.category),
                       static_cast<double>(recipe.ticks) / static_cast<double>(kTicksPerSecond));
    for (const ItemStack& s : recipe.inputs) {
        if (inv) {
            const int have = inv->count(s.kind);
            ImGui::TextColored(have >= s.count ? theme::kInk : theme::kBad, "  %d x %s  (have %d)", s.count,
                               item_name(s.kind), have);
        } else {
            ImGui::Text("  %d x %s", s.count, item_name(s.kind));
        }
    }
    for (const FluidStack& s : recipe.fluid_inputs)
        ImGui::Text("  %d %s", s.amount, fluid_name(s.kind));
    ImGui::TextColored(theme::kInkDim, "Makes");
    for (const ItemStack& s : recipe.outputs)
        ImGui::TextColored(theme::kGood, "  %d x %s", s.count, item_name(s.kind));
    for (const FluidStack& s : recipe.fluid_outputs)
        ImGui::TextColored(theme::kGood, "  %d %s", s.amount, fluid_name(s.kind));
}

// One button per hand-craftable recipe whose main output is in `category`.
void DrawHandCraftButtons(Bridge& bridge, ItemCategory category) {
    const float button_w = ImGui::GetContentRegionAvail().x;
    const std::vector<Recipe>& recipes = all_recipes();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const Recipe& r = recipes[i];
        if (!r.hand_craftable || item_category(r.outputs.front().kind) != category)
            continue;
        const bool craftable = bridge.can_hand_craft(r);
        char label[96];
        std::snprintf(label, sizeof(label), "%s##craft%zu", recipe_name(r), i);
        ImGui::BeginDisabled(!craftable);
        if (ImGui::Button(label, ImVec2(button_w, 0.0f))) {
            const int n = ImGui::GetIO().KeyShift ? 5 : 1;
            for (int k = 0; k < n && bridge.queue_craft(r); ++k) {
            }
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            RecipeDetails(r, &bridge.get_inventory());
            ImGui::EndTooltip();
        }
    }
}

// What a building is and what it's doing: for the grid's hover tooltip and
// the inspector.
void BuildingDetails(const Building& b) {
    const std::optional<ItemKind> kind = b.item_kind();
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
            ImGui::Text("Recipe: %s  (%.1f / min)", recipe_name(*recipe), machine->crafts_per_minute());
            ImGui::ProgressBar(machine->craft_progress(), ImVec2(bar_w, 0.0f), machine->crafting() ? "" : "Waiting");
            ImGui::TextColored(theme::kInkDim, "Ingredients (per craft)");
            for (const ItemStack& s : recipe->inputs) {
                const int have = machine->get_input().count(s.kind);
                ImGui::TextColored(have >= s.count ? theme::kInk : theme::kInkDim, "  %d / %d  %s", have, s.count,
                                   item_name(s.kind));
            }
            for (const FluidStack& f : recipe->fluid_inputs)
                ImGui::TextColored(theme::kInkDim, "  %d / %d  %s (no pipe flow yet)", machine->fluid_input(f.kind),
                                   f.amount, fluid_name(f.kind));
            ImGui::TextColored(theme::kInkDim, "Waiting to go out");
            bool any = false;
            for (const ItemStack& s : machine->get_output().stacks()) {
                ImGui::TextColored(theme::kGood, "  %d x %s", s.count, item_name(s.kind));
                any = true;
            }
            for (const FluidStack& f : recipe->fluid_outputs)
                if (const int amount = machine->fluid_output(f.kind); amount > 0) {
                    ImGui::TextColored(theme::kGood, "  %d %s", amount, fluid_name(f.kind));
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
            ImGui::Text("  %d x %s", s.count, item_name(s.kind));
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
                ImGui::Text("  %d x %s", s.count, item_name(s.kind));
        }
    }
    if (const auto* hold = dynamic_cast<const CargoHold*>(&b)) {
        ImGui::Text("Stored: %d / %d", hold->get_stored().total(), kCargoCapacity);
        for (const ItemStack& s : hold->get_stored().stacks())
            ImGui::Text("  %d x %s", s.count, item_name(s.kind));
    }
    if (!kind)
        ImGui::TextColored(theme::kInkDim, "Belts deliver into its inventory");
    if (b.rotatable())
        ImGui::TextColored(theme::kInkDim, "Facing %s", DirectionName(b.facing()));
}

// The inspected building (clicked on the grid): its details, and for a
// machine, the recipe picker.
void DrawInspector(Ship& ship) {
    const Building* b = InspectedBuilding(ship);
    if (!b)
        return;
    theme::SectionHeading("Inspect");
    BuildingDetails(*b);
    if (const auto* machine = dynamic_cast<const Machine*>(b)) {
        const Recipe* current = machine->get_recipe();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##recipe", current ? recipe_name(*current) : "Pick a recipe",
                              ImGuiComboFlags_HeightLargest)) {
            if (ImGui::Selectable("None", current == nullptr))
                ship.set_recipe(*g_build.inspected, nullptr);
            const std::vector<const Recipe*> recipes = machine_recipes(*machine->item_kind());
            for (std::size_t i = 0; i < recipes.size(); ++i) {
                const Recipe* r = recipes[i];
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(recipe_name(*r), r == current) && r != current)
                    ship.set_recipe(*g_build.inspected, r);
                ImGui::PopID();
                if (ImGui::BeginItemTooltip()) {
                    RecipeDetails(*r, nullptr);
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextColored(theme::kInkDim, "Changing it returns held ingredients to the bridge.");
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

    theme::SectionHeading("Craft queue");
    const std::deque<CraftJob>& queue = bridge.craft_queue();
    if (queue.empty()) {
        ImGui::TextColored(theme::kInkDim, "Idle");
    } else {
        constexpr std::size_t kShown = 8;
        std::optional<std::size_t> cancel;
        for (std::size_t i = 0; i < std::min(queue.size(), kShown); ++i) {
            const CraftJob& job = queue[i];
            ImGui::PushID(static_cast<int>(i));
            if (i == 0) {
                const float progress = static_cast<float>(job.elapsed) / static_cast<float>(job.recipe->ticks);
                ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), recipe_name(*job.recipe));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    cancel = i;
            } else if (ImGui::Selectable(recipe_name(*job.recipe)) ||
                       ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                cancel = i;
            }
            ImGui::SetItemTooltip("Click (right click on the bar) to cancel and refund");
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
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            const Recipe& r = recipes[i];
            if (r.category != category || r.hand_craftable)
                continue;
            ImGui::BulletText("%s", recipe_name(r));
            if (ImGui::BeginItemTooltip()) {
                RecipeDetails(r, &bridge.get_inventory());
                ImGui::EndTooltip();
            }
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
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
        const float center = (static_cast<float>(n / 2) + 0.5f) * cam.cell;
        cam.offset = ImVec2(canvas_size.x * 0.5f - center, canvas_size.y * 0.5f - center);
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

    // Pan: middle drag, or left drag with Space held (for trackpads).
    const bool space = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        cam.left_pans = space;
    const bool panning = ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                                                   (cam.left_pans && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
    if (panning) {
        cam.offset.x += io.MouseDelta.x;
        cam.offset.y += io.MouseDelta.y;
    }
    if (panning || (space && ImGui::IsItemHovered()))
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // Keep the middle of the view over the grid.
    const float side = cam.cell * static_cast<float>(n);
    cam.offset.x = std::clamp(cam.offset.x, canvas_size.x * 0.5f - side, canvas_size.x * 0.5f);
    cam.offset.y = std::clamp(cam.offset.y, canvas_size.y * 0.5f - side, canvas_size.y * 0.5f);
    return panning || space;
}

// The build grid on a canvas filling `avail`: click / drag to build and
// remove, scroll to zoom, middle drag (or Space + drag) to pan.
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
            const BuildResult r = ship.place(g_build.selected, *hover_cell, g_build.facing);
            if (r != BuildResult::Ok && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // Clicking a building (rather than building on it) inspects it.
                if (const Building* b = ship.building_at(*hover_cell)) {
                    g_build.inspected = ship.anchor_of(*hover_cell);
                    g_build.inspected_building = b;
                } else {
                    SetBuildMessage(r);
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
                draw->AddRectFilled(min, ImVec2(min.x + cell, min.y + cell),
                                    *tile == ItemKind::ReinforcedHull ? reinforced : hull);
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
                if (b == InspectedBuilding(ship))
                    draw->AddRect(min, max, theme::U32(theme::kRed), 0.0f, 0, 2.0f);
            }
    // Items on belts, over every building (they cross onto the next cell).
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x)
            if (const auto* belt = dynamic_cast<const Conveyor*>(ship.building_anchored_at({x, y})))
                DrawBeltItems(draw, *belt, CellMin(origin, cell, {x, y}), cell);

    // Hover preview: a ghost of what would be built (its footprint starting
    // at the hovered cell), outlined in good / bad. Clipped to the grid.
    if (hover_cell) {
        const GridSize size = footprint_of(g_build.selected, g_build.facing);
        const ImVec2 min = CellMin(origin, cell, *hover_cell);
        const ImVec2 max = FootprintMax(min, cell, size);
        const BuildResult r = ship.can_place(g_build.selected, *hover_cell, g_build.facing);
        draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);  // within the canvas clip
        if (r == BuildResult::Ok) {
            if (g_build.selected == ItemKind::Hull)
                draw->AddRectFilled(min, max, theme::U32(theme::kTileHull, 0.6f));
            else if (g_build.selected == ItemKind::ReinforcedHull)
                draw->AddRectFilled(min, max, theme::U32(theme::kTileReinforcedHull, 0.6f));
            else
                DrawBuilding(draw, ship, *hover_cell, min, cell, size, g_build.selected, g_build.facing, 0.45f);
        }
        draw->AddRect(min, max, theme::U32(r == BuildResult::Ok ? theme::kGood : theme::kBad), 0.0f, 0, 2.0f);
        draw->PopClipRect();

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
            if (const Building* b = ship.building_at(*hover_cell)) {
                ImGui::BeginTooltip();
                BuildingDetails(*b);
                ImGui::EndTooltip();
            }
    }
    draw->PopClipRect();
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
        screen = Screen::Playing;
    ImGui::SetCursorPosX(button_x);
    if (ImGui::Button("Credits", button_size))
        screen = Screen::Credits;

    ImGui::End();
}

void DrawCredits(Screen& screen) {
    BeginFullscreenPage("##credits");
    theme::SectionHeading("Credits");
    if (ImGui::Button("Back") || ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;
    ImGui::End();
}

void DrawPlaying(Screen& screen, World& world) {
    if (ShortcutPressed(ImGuiKey_Escape)) {
        screen = Screen::MainMenu;
        g_camera = {};  // the next game starts centred on its bridge
        g_build.inspected.reset();
        g_build.inspected_building = nullptr;
    }

    Ship& ship = world.get_ship();
    const std::vector<ItemKind> list = BuildList(ship);
    for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 9); ++i)
        if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i))))
            g_build.selected = list[i];
    if (ShortcutPressed(ImGuiKey_R))
        g_build.facing = rotated_cw(g_build.facing);

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
    ImGui::End();
}

void DrawDevWindows() {
    static bool show_imgui_demo = false;
    static bool show_implot_demo = false;
    if (ShortcutPressed(ImGuiKey_F1))
        show_imgui_demo = !show_imgui_demo;
    if (ShortcutPressed(ImGuiKey_F2))
        show_implot_demo = !show_implot_demo;
    if (show_imgui_demo)
        ImGui::ShowDemoWindow(&show_imgui_demo);
    if (show_implot_demo)
        ImPlot::ShowDemoWindow(&show_implot_demo);
}
