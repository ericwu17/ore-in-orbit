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
// contents. Pair with ImGui::End().
void BeginFullscreenPage(const char* id) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoDocking);
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
};
BuildUiState g_build;

constexpr double kMessageSeconds = 3.0;

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
                      theme::U32(theme::kTileArm, alpha), cell * 0.12f);
        break;
    }
    case ItemKind::FilterSorter:
        conveyor(theme::kTileBelt);
        draw->AddRect(a, b, theme::U32(theme::kWarn, alpha), rounding * 0.5f, 0, std::max(1.0f, cell * 0.08f));
        break;
    case ItemKind::GrabberArm:
    case ItemKind::FastGrabberArm: {
        // A base at the back and an arm reaching forward.
        const ImU32 color =
            theme::U32(*kind == ItemKind::FastGrabberArm ? theme::kTileExpressBelt : theme::kTileArm, alpha);
        const ImVec2 base(c.x - d.x * cell * 0.22f, c.y - d.y * cell * 0.22f);
        const ImVec2 tip(c.x + d.x * cell * 0.32f, c.y + d.y * cell * 0.32f);
        draw->AddLine(base, tip, color, cell * 0.1f);
        draw->AddCircleFilled(base, cell * 0.16f, color);
        DrawArrow(draw, tip, d, cell * 0.14f, color);
        break;
    }
    default:
        letter_tile(TileColor(*kind), TileLabel(*kind));
        break;
    }
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
                       "Left click / drag: build\nRight click / drag: remove a building;\n"
                       "again to remove the hull (refunds)\n"
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

// The right panel: hand-crafting at the bridge, its queue, and the recipe
// book for everything else.
void DrawCraftPanel(Ship& ship, float width) {
    const ImVec2 size(width, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawShadowedPanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                             ImGui::GetStyle().WindowRounding);

    ImGui::BeginChild("##craft_panel", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    Bridge& bridge = ship.get_bridge();

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
    ImGui::TextColored(theme::kInkDim, "Made in machines; machines don't run yet.");
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

// What a building is, for the grid's hover tooltip.
void BuildingTooltip(const Building& b) {
    const std::optional<ItemKind> kind = b.item_kind();
    ImGui::TextUnformatted(kind ? item_name(*kind) : "Bridge");
    if (const auto* machine = dynamic_cast<const Machine*>(&b)) {
        std::string crafts;
        for (int c = 0; c < kCraftCategoryCount; ++c)
            if (machine->crafts(static_cast<CraftCategory>(c)))
                crafts += std::string(crafts.empty() ? "" : ", ") + craft_category_name(static_cast<CraftCategory>(c));
        ImGui::TextColored(theme::kInkDim, "%s, speed x%d", crafts.c_str(), machine->crafting_speed());
    }
    if (b.rotatable())
        ImGui::TextColored(theme::kInkDim, "Facing %s", DirectionName(b.facing()));
}

// The build grid, fitted to `avail`, with click / drag to build and remove.
void DrawShipGrid(Ship& ship, ImVec2 avail) {
    const int n = ship.size();
    const float cell = std::max(4.0f, std::floor(std::min(avail.x, avail.y) / static_cast<float>(n)));
    const float side = cell * static_cast<float>(n);
    // Center the grid in the space between the panels.
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const ImVec2 origin(std::floor(cursor.x + std::max(0.0f, (avail.x - side) * 0.5f)),
                        std::floor(cursor.y + std::max(0.0f, (avail.y - side) * 0.5f)));
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##ship_grid", ImVec2(side, side),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    std::optional<GridPos> hover_cell;
    if (hovered) {
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
            if (r != BuildResult::Ok && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                SetBuildMessage(r);
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

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 grid_line = theme::U32(theme::kGridLine);
    const ImU32 hull = theme::U32(theme::kTileHull);
    const ImU32 reinforced = theme::U32(theme::kTileReinforcedHull);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const GridPos p{x, y};
            const ImVec2 min = CellMin(origin, cell, p);
            const ImVec2 max(min.x + cell, min.y + cell);
            if (const std::optional<ItemKind> tile = ship.hull_at(p))
                draw->AddRectFilled(min, max, *tile == ItemKind::ReinforcedHull ? reinforced : hull);
            else
                draw->AddRect(min, max, grid_line);
        }
    }
    // Buildings after all the hull, since larger ones overlap later cells.
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (const Building* b = ship.building_anchored_at({x, y}))
                DrawBuilding(draw, ship, {x, y}, CellMin(origin, cell, {x, y}), cell, b->footprint(),
                             b->item_kind(), b->facing(), 1.0f);

    // Hover preview: a ghost of what would be built (its footprint starting
    // at the hovered cell), outlined in good / bad. Clipped to the grid.
    if (hover_cell) {
        const GridSize size = footprint_of(g_build.selected, g_build.facing);
        const ImVec2 min = CellMin(origin, cell, *hover_cell);
        const ImVec2 max = FootprintMax(min, cell, size);
        const BuildResult r = ship.can_place(g_build.selected, *hover_cell, g_build.facing);
        draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);
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
                BuildingTooltip(*b);
                ImGui::EndTooltip();
            }
    }
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
    if (ShortcutPressed(ImGuiKey_Escape))
        screen = Screen::MainMenu;

    Ship& ship = world.get_ship();
    const std::vector<ItemKind> list = BuildList(ship);
    for (std::size_t i = 0; i < std::min<std::size_t>(list.size(), 9); ++i)
        if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i))))
            g_build.selected = list[i];
    if (ShortcutPressed(ImGuiKey_R))
        g_build.facing = rotated_cw(g_build.facing);

    BeginFullscreenPage("##playing");
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
