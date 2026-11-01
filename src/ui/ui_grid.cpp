// ui_grid.cpp — the ship build grid: its camera, the hull and buildings,
// the enemies, and building, removing, copying and pasting with the mouse
// (see ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

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
        if (b.recipe && !b.recipe->outputs.empty())
            DrawCornerItem(draw, min, max, cell, b.recipe->outputs.front().kind);
        if (b.target)
            DrawCornerItem(draw, min, max, cell, *b.target);
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

}  // namespace

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
    constexpr int kMaxFootprint = kBridgeSize;  // the largest building
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
                    DrawCollectorStatus(draw, *collector, min, max, cell);
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

}  // namespace ui
