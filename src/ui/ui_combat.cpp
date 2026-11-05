// ui_combat.cpp — ship-to-ship fights on screen: the enemy panel beside the
// grid (the enemy ship, its health and shields, picking what to shoot), the
// shots flying and landing on both ships, shields, and weapons' charge (see
// ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace {

// The middle of `cell` on a grid drawn at `origin`, `cell_px` per cell.
ImVec2 CellCenter(ImVec2 origin, float cell_px, GridPos cell) {
    return ImVec2(origin.x + (static_cast<float>(cell.x) + 0.5f) * cell_px,
                  origin.y + (static_cast<float>(cell.y) + 0.5f) * cell_px);
}

// What a shot looks like in flight: its color.
ImVec4 ShotColor(ItemKind weapon, bool from_enemy) {
    switch (weapon_spec(weapon).family) {
    case WeaponFamily::Ion:     return theme::kTeal;
    case WeaponFamily::Missile: return from_enemy ? theme::kBadHover : theme::kAccentHover;
    case WeaponFamily::Beam:    return from_enemy ? theme::kBad : theme::kWarn;
    case WeaponFamily::Laser:
    case WeaponFamily::Flak:    break;
    }
    return from_enemy ? theme::kBad : theme::kAccent;
}

// A beam's line on `ship`, from `cell` along `sweep`, as far as it reaches.
void DrawBeam(ImDrawList* draw, const Ship& ship, GridPos cell, Direction sweep, int length, ImVec2 origin,
              float cell_px, ImU32 color, float width) {
    GridPos end = cell;
    for (int i = 1; i < length; ++i) {
        const GridPos next = neighbor(end, sweep);
        if (!ship.in_bounds(next))
            break;
        end = next;
    }
    draw->AddLine(CellCenter(origin, cell_px, cell), CellCenter(origin, cell_px, end), color, width);
}

}  // namespace

void DrawCombatStatus(ImDrawList* draw, const Building& b, ImVec2 min, ImVec2 max, float cell) {
    if (const auto* w = dynamic_cast<const Weapon*>(&b)) {
        DrawCornerItem(draw, min, max, cell, w->get_spec().ammo);
        DrawBufferGauge(draw, min, max, cell, w->charge_fraction());
        if (w->get_stun() > 0)
            draw->AddRectFilled(min, max, theme::U32(theme::kTeal, 0.3f));
    } else if (const auto* s = dynamic_cast<const ShieldProjector*>(&b)) {
        const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        const float r = (max.x - min.x) * 0.38f;
        if (s->up())
            draw->AddCircle(c, r, theme::U32(theme::kTeal), 0, std::max(1.5f, cell * 0.12f));
        else
            DrawBufferGauge(draw, min, max, cell,
                            1.0f - static_cast<float>(s->get_recharge()) / static_cast<float>(kShieldRecharge));
        if (s->get_stun() > 0)
            draw->AddRectFilled(min, max, theme::U32(theme::kTeal, 0.3f));
    }
}

void DrawCombatOverlay(ImDrawList* draw, const Ship& ship, const Engagement& fight, bool enemy_side, int ship_id,
                       ImVec2 origin, float cell, ImVec2 canvas_min, ImVec2 canvas_max, Tick now) {
    // The shield bubble: brighter with more layers up.
    if (const int layers = shield_layers(ship); layers > 0) {
        const auto [corner, extent] = ship.hull_bounds();
        const ImVec2 c(origin.x + (static_cast<float>(corner.x) + static_cast<float>(extent.w) * 0.5f) * cell,
                       origin.y + (static_cast<float>(corner.y) + static_cast<float>(extent.h) * 0.5f) * cell);
        const ImVec2 radius((static_cast<float>(extent.w) * 0.5f + 2.0f) * cell,
                            (static_cast<float>(extent.h) * 0.5f + 2.0f) * cell);
        draw->AddEllipseFilled(c, radius, theme::U32(theme::kTeal, 0.05f * static_cast<float>(layers)));
        for (int i = 0; i < layers; ++i) {
            const float grow = static_cast<float>(i) * cell * 0.6f;
            draw->AddEllipse(c, ImVec2(radius.x + grow, radius.y + grow), theme::U32(theme::kTeal, 0.6f), 0.0f, 0,
                             2.0f);
        }
    }
    const auto aimed_here = [&](bool at_enemy, int aimed_at) { return enemy_side ? at_enemy : !at_enemy && aimed_at == ship_id; };
    // Shots on their way in: from the side the other ship is on (the enemy
    // to the right of the player's grid, the player to the left of it).
    for (const Shot& shot : fight.shots) {
        if (!aimed_here(shot.at_enemy, shot.ship) || now < shot.fired)
            continue;
        const WeaponSpec& spec = weapon_spec(shot.weapon);
        const ImVec4 color = ShotColor(shot.weapon, !shot.at_enemy);
        const float t = std::clamp(static_cast<float>(now - shot.fired) /
                                       static_cast<float>(std::max<Tick>(1, shot.lands - shot.fired)),
                                   0.0f, 1.0f);
        const ImVec2 to = CellCenter(origin, cell, shot.cell);
        const ImVec2 from(enemy_side ? canvas_min.x : canvas_max.x,
                          to.y + (static_cast<float>(shot.fired % 7) - 3.0f) * cell);
        const ImVec2 at(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float len = std::max(1.0f, std::hypot(dx, dy));
        const ImVec2 d(dx / len, dy / len);
        switch (spec.family) {
        case WeaponFamily::Beam:
            // Charging up: a thin aiming line where it will sweep.
            DrawBeam(draw, ship, shot.cell, shot.sweep, spec.spread, origin, cell, theme::U32(color, 0.35f * t),
                     std::max(1.0f, cell * 0.08f));
            break;
        case WeaponFamily::Missile: {
            const float r = std::max(3.0f, cell * 0.35f);
            const ImVec2 side(-d.y, d.x);
            draw->AddTriangleFilled(ImVec2(at.x + d.x * r * 1.4f, at.y + d.y * r * 1.4f),
                                    ImVec2(at.x - d.x * r + side.x * r * 0.6f, at.y - d.y * r + side.y * r * 0.6f),
                                    ImVec2(at.x - d.x * r - side.x * r * 0.6f, at.y - d.y * r - side.y * r * 0.6f),
                                    theme::U32(color));
            draw->AddLine(at, ImVec2(at.x - d.x * r * 3.0f, at.y - d.y * r * 3.0f), theme::U32(theme::kWarn, 0.6f),
                          std::max(1.0f, r * 0.4f));
            break;
        }
        case WeaponFamily::Ion:
            draw->AddCircleFilled(at, std::max(3.0f, cell * 0.3f), theme::U32(color, 0.9f));
            draw->AddCircle(at, std::max(5.0f, cell * 0.5f), theme::U32(color, 0.5f), 0, 1.5f);
            break;
        case WeaponFamily::Flak:
            draw->AddCircleFilled(at, std::max(1.5f, cell * 0.15f), theme::U32(color));
            break;
        case WeaponFamily::Laser: {
            const float streak = std::max(6.0f, cell * 1.2f);
            draw->AddLine(ImVec2(at.x - d.x * streak, at.y - d.y * streak), at, theme::U32(color),
                          std::max(2.0f, cell * (shot.weapon == ItemKind::HeavyLaser ? 0.3f : 0.18f)));
            break;
        }
        }
    }
    // What just landed: a burst on a hit, a ripple on the shield when it
    // stopped it, "miss" when it was dodged.
    for (const Impact& i : fight.impacts) {
        if (!aimed_here(i.on_enemy, i.ship) || now < i.at)
            continue;
        const float fade = 1.0f - static_cast<float>(now - i.at) / static_cast<float>(kImpactTicks);
        const ImVec2 c = CellCenter(origin, cell, i.cell);
        const WeaponSpec& spec = weapon_spec(i.weapon);
        switch (i.kind) {
        case ImpactKind::Hit:
            if (spec.family == WeaponFamily::Beam) {
                DrawBeam(draw, ship, i.cell, i.sweep, spec.spread, origin, cell,
                         theme::U32(ShotColor(i.weapon, !i.on_enemy), fade), std::max(3.0f, cell * 0.45f));
            } else {
                const float r = cell * (spec.family == WeaponFamily::Missile ? 1.6f : 0.8f) * (1.5f - fade * 0.5f);
                draw->AddCircleFilled(c, std::max(3.0f, r), theme::U32(theme::kWarn, 0.55f * fade));
                draw->AddCircle(c, std::max(4.0f, r), theme::U32(theme::kBad, fade), 0, 2.0f);
            }
            break;
        case ImpactKind::Blocked:
            draw->AddCircle(c, std::max(4.0f, cell * 1.2f * (1.5f - fade * 0.5f)), theme::U32(theme::kTeal, fade), 0,
                            2.5f);
            break;
        case ImpactKind::Missed:
            theme::DrawHaloText(draw, ImVec2(c.x - cell, c.y - cell), theme::U32(theme::kText, fade),
                                theme::U32(theme::kShadow, 0.7f * fade), "miss");
            break;
        }
    }
}

void DrawEnemyPanel(World& world, FleetShip& shown, ImVec2 avail) {
    const Engagement* fight = world.engagement_in(shown.sector);
    if (!fight)
        return;
    const Ship& enemy = fight->ship;
    const EnemyDesign& design = fight->design();
    const Tick now = world.get_tick();
    const float fh = ImGui::GetFrameHeight();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::kPanel);
    ImGui::BeginChild("##enemy", avail, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PushTextWrapPos(0.0f);
    theme::PushMono(theme::kFontTitle);
    ImGui::TextColored(theme::kBad, "ENEMY %s", design.name);
    theme::PopMono();
    ImGui::TextColored(theme::kTextDim, "%s", design.description);

    // Its bridge: beaten at 0.
    const Bridge& bridge = enemy.get_bridge();
    const float hull = static_cast<float>(bridge.get_hp()) / static_cast<float>(bridge.max_hp());
    const std::string hull_text = "Bridge " + std::to_string(bridge.get_hp()) + " / " + std::to_string(bridge.max_hp());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::kBad);
    ImGui::ProgressBar(hull, ImVec2(-1.0f, 0.0f), hull_text.c_str());
    ImGui::PopStyleColor();
    int projectors = 0;
    for (const GridPos p : enemy.placement_order())
        projectors += dynamic_cast<const ShieldProjector*>(enemy.building_anchored_at(p)) ? 1 : 0;
    const int layers = shield_layers(enemy);
    ImGui::TextUnformatted("Shields");
    ImGui::SameLine();
    if (projectors == 0) {
        ImGui::TextColored(theme::kTextDim, "none");
    } else {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float r = fh * 0.22f;
        ImGui::Dummy(ImVec2(static_cast<float>(projectors) * r * 2.8f, fh * 0.6f));
        for (int i = 0; i < projectors; ++i) {
            const ImVec2 c(at.x + r + static_cast<float>(i) * r * 2.8f, at.y + fh * 0.3f);
            if (i < layers)
                ImGui::GetWindowDrawList()->AddCircleFilled(c, r, theme::U32(theme::kTeal));
            else
                ImGui::GetWindowDrawList()->AddCircle(c, r, theme::U32(theme::kTeal, 0.6f), 0, 1.5f);
        }
    }
    ImGui::SameLine(0.0f, fh);
    ImGui::Text("Evasion %d%%", static_cast<int>(std::lround(evasion(enemy) * 100.0f)));

    // What the ship shown fires at.
    const Building* target = shown.target ? enemy.building_anchored_at(*shown.target) : nullptr;
    if (target) {
        ImGui::TextColored(theme::kAccent, "Target: %s", target == &bridge ? "its bridge" : item_name(*target->item_kind()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear") && world.set_target(shown.id, std::nullopt))
            PlayUi(audio::Sound::CombatTargetClear);
    } else {
        ImGui::TextColored(theme::kTextDim, "Click its buildings to pick a target. With none, your weapons fire at "
                                            "its shields, then its weapons, then its bridge.");
    }
    ImGui::PopTextWrapPos();

    // The enemy ship, fitted to the rest of the panel.
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                             std::max(1.0f, ImGui::GetContentRegionAvail().y));
    const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
    ImGui::InvisibleButton("##enemy_ship", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto [corner, extent] = enemy.hull_bounds();
    const float margin = 3.0f;  // cells round the hull, for its shields
    const float cell = std::floor(std::min(canvas_size.x / (static_cast<float>(extent.w) + margin * 2.0f),
                                           canvas_size.y / (static_cast<float>(extent.h) + margin * 2.0f)));
    if (cell >= 2.0f) {
        const ImVec2 origin(
            std::floor(canvas_min.x + (canvas_size.x - static_cast<float>(extent.w) * cell) * 0.5f -
                       static_cast<float>(corner.x) * cell),
            std::floor(canvas_min.y + (canvas_size.y - static_cast<float>(extent.h) * cell) * 0.5f -
                       static_cast<float>(corner.y) * cell));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(canvas_min, canvas_max, true);
        draw->AddRectFilled(canvas_min, canvas_max, theme::U32(theme::kInset));
        const sprites::Sprite* hull_sprite = sprites::Hull();
        for (int y = corner.y; y < corner.y + extent.h; ++y)
            for (int x = corner.x; x < corner.x + extent.w; ++x)
                if (enemy.has_hull({x, y})) {
                    const ImVec2 min = CellMin(origin, cell, {x, y});
                    if (hull_sprite && cell >= 8.0f)
                        DrawSprite(draw, *hull_sprite, min, ImVec2(min.x + cell, min.y + cell), Direction::North, 1.0f);
                    else
                        draw->AddRectFilled(min, ImVec2(min.x + cell, min.y + cell), theme::U32(theme::kTileHull));
                }
        // The cell under the mouse, and the building there.
        const ImVec2 m = ImGui::GetMousePos();
        const GridPos mouse{static_cast<int>(std::floor((m.x - origin.x) / cell)),
                            static_cast<int>(std::floor((m.y - origin.y) / cell))};
        const std::optional<GridPos> hovered =
            ImGui::IsItemHovered() && enemy.in_bounds(mouse) ? enemy.anchor_of(mouse) : std::nullopt;
        for (const GridPos p : enemy.placement_order()) {
            const Building& b = *enemy.building_anchored_at(p);
            const ImVec2 min = CellMin(origin, cell, p);
            const ImVec2 max = FootprintMax(min, cell, b.footprint());
            const auto* thruster = dynamic_cast<const Thruster*>(&b);
            DrawBuilding(draw, min, cell, b.footprint(), b.item_kind(), b.facing(),
                         thruster && thruster->offline() ? 0.4f : 1.0f);
            DrawCombatStatus(draw, b, min, max, cell);
            DrawBuildingDamage(draw, b, min, max, cell, now);
            if (hovered == p)
                draw->AddRect(min, max, theme::U32(theme::kAccent, 0.6f), 0.0f, 0, 2.0f);
            if (shown.target == p) {
                // Crosshairs.
                const ImVec2 c((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                const float r = std::max(max.x - min.x, max.y - min.y) * 0.5f + cell * 0.4f;
                const ImU32 amber = theme::U32(theme::kAccent);
                draw->AddCircle(c, r, amber, 0, 2.0f);
                draw->AddLine(ImVec2(c.x - r - cell * 0.5f, c.y), ImVec2(c.x - r * 0.5f, c.y), amber, 2.0f);
                draw->AddLine(ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x + r + cell * 0.5f, c.y), amber, 2.0f);
                draw->AddLine(ImVec2(c.x, c.y - r - cell * 0.5f), ImVec2(c.x, c.y - r * 0.5f), amber, 2.0f);
                draw->AddLine(ImVec2(c.x, c.y + r * 0.5f), ImVec2(c.x, c.y + r + cell * 0.5f), amber, 2.0f);
            }
        }
        DrawCombatOverlay(draw, enemy, *fight, true, shown.id, origin, cell, canvas_min, canvas_max, now);
        draw->PopClipRect();

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (world.set_target(shown.id, *hovered))
                PlayUi(audio::Sound::CombatTargetSet);
        } else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (shown.target && world.set_target(shown.id, std::nullopt))
                PlayUi(audio::Sound::CombatTargetClear);
        }
        if (hovered) {
            const Building& b = *enemy.building_anchored_at(*hovered);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(&b == &bridge ? "Bridge" : item_name(*b.item_kind()));
            ImGui::TextColored(theme::kTextDim, "Health %d / %d", b.get_hp(), b.max_hp());
            if (const auto* w = dynamic_cast<const Weapon*>(&b))
                ImGui::TextColored(theme::kTextDim, "%d%% charged%s", static_cast<int>(w->charge_fraction() * 100.0f),
                                   w->get_stun() > 0 ? ", stunned" : "");
            if (const auto* s = dynamic_cast<const ShieldProjector*>(&b))
                ImGui::TextColored(theme::kTextDim, "%s", s->up() ? "Shield layer up" : "Recharging");
            ImGui::TextColored(theme::kAccent, "%s", shown.target == hovered ? "Targeted (right click: clear)"
                                                                              : "Click to target it");
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

}  // namespace ui
