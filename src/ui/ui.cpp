// ui.cpp — the screens: main menu, credits and the game screen (the ship
// build grid and its build palette), plus the developer demo windows.
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
    return kind == ItemKind::Pipe || kind == ItemKind::HydrogenCollector;
}

// Screen-space top-left corner of a grid cell.
ImVec2 CellMin(ImVec2 origin, float cell, GridPos p) {
    return {origin.x + static_cast<float>(p.x) * cell, origin.y + static_cast<float>(p.y) * cell};
}

// Screen-space bottom-right corner of a footprint whose top-left cell is at `min`.
ImVec2 FootprintMax(ImVec2 min, float cell, GridSize size) {
    return {min.x + static_cast<float>(size.w) * cell, min.y + static_cast<float>(size.h) * cell};
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

    auto letter_tile = [&](ImVec4 color, const char* letter) {
        draw->AddRectFilled(a, b, theme::U32(color, alpha), rounding);
        if (cell >= ImGui::GetFontSize() * 1.2f) {
            const ImVec2 ts = ImGui::CalcTextSize(letter);
            draw->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), glyph, letter);
        }
    };

    if (!kind) {
        letter_tile(theme::kTileBridge, "B");
        return;
    }
    switch (*kind) {
    case ItemKind::HydrogenCollector: letter_tile(theme::kTileH2Collector, "H"); break;
    case ItemKind::AsteroidCollector: letter_tile(theme::kTileRockCollector, "A"); break;
    case ItemKind::Pipe: {
        // A hub plus a bar toward each neighbor that carries fluid.
        const ImU32 color = theme::U32(theme::kTilePipe, alpha);
        const float half = cell * 0.14f;
        draw->AddRectFilled(ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half), color);
        for (const Direction d : {Direction::North, Direction::East, Direction::South, Direction::West}) {
            const ImVec2 v = DirectionVector(d);
            const GridPos n{pos.x + static_cast<int>(v.x), pos.y + static_cast<int>(v.y)};
            if (!CarriesFluid(ship.building_at(n)))
                continue;
            const ImVec2 edge(c.x + v.x * cell * 0.5f, c.y + v.y * cell * 0.5f);
            draw->AddRectFilled(ImVec2(std::min(c.x, edge.x) - half, std::min(c.y, edge.y) - half),
                                ImVec2(std::max(c.x, edge.x) + half, std::max(c.y, edge.y) + half), color);
        }
        break;
    }
    case ItemKind::TransportBelt: {
        draw->AddRectFilled(a, b, theme::U32(theme::kTileBelt, alpha), rounding * 0.5f);
        const ImVec2 d = DirectionVector(facing);
        const ImVec2 p(-d.y, d.x);
        const float h = cell * 0.28f;
        draw->AddTriangleFilled(ImVec2(c.x + d.x * h, c.y + d.y * h),
                                ImVec2(c.x - d.x * h * 0.5f + p.x * h * 0.8f, c.y - d.y * h * 0.5f + p.y * h * 0.8f),
                                ImVec2(c.x - d.x * h * 0.5f - p.x * h * 0.8f, c.y - d.y * h * 0.5f - p.y * h * 0.8f),
                                glyph);
        break;
    }
    case ItemKind::Hull:
    case ItemKind::Count:
        break;
    }
}

// The left panel: one button per placeable item with its inventory count,
// the belt direction, and the controls.
void DrawBuildPalette(const Ship& ship) {
    const float fh = ImGui::GetFrameHeight();
    const ImVec2 size(fh * 11.0f, ImGui::GetContentRegionAvail().y);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    theme::DrawShadowedPanel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + size.x, min.y + size.y),
                             ImGui::GetStyle().WindowRounding);

    ImGui::BeginChild("##build_palette", size, ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::SectionHeading("Bridge inventory");

    const float button_w = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = static_cast<ItemKind>(i);
        const int count = ship.inventory().count(kind);
        char label[64];
        std::snprintf(label, sizeof(label), "%d  %s  x%d##item%d", i + 1, item_name(kind), count, i);
        if (count == 0)
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kInkDim);
        if (theme::SegmentedButton(label, g_build.selected == kind, ImVec2(button_w, 0.0f)))
            g_build.selected = kind;
        if (count == 0)
            ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    if (g_build.selected == ItemKind::TransportBelt)
        ImGui::Text("Facing: %s  (R to rotate)", DirectionName(g_build.facing));
    else
        ImGui::TextColored(theme::kInkDim, "Facing: -");

    ImGui::Spacing();
    if (!g_build.message.empty() && ImGui::GetTime() - g_build.message_time < kMessageSeconds)
        ImGui::TextColored(theme::kBad, "%s", g_build.message.c_str());
    else
        ImGui::TextUnformatted("");

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::kInkDim,
                       "Left click / drag: build\nRight click / drag: remove a building;\n"
                       "again to remove the hull (refunds)\n"
                       "1-5: pick item   R: rotate belt\nEsc: main menu\n\n"
                       "Everything but hull needs a hull tile under it.");
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}

// The build grid, fitted to the remaining space, with click / drag to build
// and remove.
void DrawShipGrid(Ship& ship) {
    const int n = ship.size();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float cell = std::max(4.0f, std::floor(std::min(avail.x, avail.y) / static_cast<float>(n)));
    const float side = cell * static_cast<float>(n);
    // Center the grid in the space left of the palette.
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
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const GridPos p{x, y};
            const ImVec2 min = CellMin(origin, cell, p);
            const ImVec2 max(min.x + cell, min.y + cell);
            if (ship.has_hull(p))
                draw->AddRectFilled(min, max, hull);
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
        const GridSize size = footprint_of(g_build.selected);
        const ImVec2 min = CellMin(origin, cell, *hover_cell);
        const ImVec2 max = FootprintMax(min, cell, size);
        const BuildResult r = ship.can_place(g_build.selected, *hover_cell);
        draw->PushClipRect(origin, ImVec2(origin.x + side, origin.y + side), true);
        if (r == BuildResult::Ok) {
            if (g_build.selected == ItemKind::Hull)
                draw->AddRectFilled(min, max, theme::U32(theme::kTileHull, 0.6f));
            else
                DrawBuilding(draw, ship, *hover_cell, min, cell, size, g_build.selected, g_build.facing, 0.45f);
        }
        draw->AddRect(min, max, theme::U32(r == BuildResult::Ok ? theme::kGood : theme::kBad), 0.0f, 0, 2.0f);
        draw->PopClipRect();
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
    for (int i = 0; i < kItemKindCount; ++i)
        if (ShortcutPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i)))
            g_build.selected = static_cast<ItemKind>(i);
    if (ShortcutPressed(ImGuiKey_R))
        g_build.facing = rotated_cw(g_build.facing);

    BeginFullscreenPage("##playing");
    DrawBuildPalette(ship);
    ImGui::SameLine(0.0f, ImGui::GetStyle().WindowPadding.x * 2.0f);
    DrawShipGrid(ship);
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
