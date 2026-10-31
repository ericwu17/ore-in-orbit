// ui_fleet.cpp — the fleet: the ship tabs over the grid, the Fleet section
// of the craft panel, the Send cargo and Build a new ship windows, and the
// star map (see ui_internal.h).
#include "ui/ui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ui {

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

void CycleShip(const Fleet& fleet, int step) {
    const auto& ships = fleet.get_ships();
    const auto n = static_cast<int>(ships.size());
    for (int i = 0; i < n; ++i)
        if (ships[static_cast<std::size_t>(i)]->id == g_fleet.current) {
            SwitchToShip(ships[static_cast<std::size_t>((i + step + n) % n)]->id);
            return;
        }
}

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

void DrawSendCargoWindow(World& world, FleetShip& current) {
    FleetUiState& f = g_fleet;
    if (!f.show_send)
        return;
    Fleet& fleet = world.get_fleet();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 18.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Send cargo", &f.show_send, ImGuiWindowFlags_NoCollapse)) {
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

namespace {

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

}  // namespace

void DrawBuildShipWindow(World& world, FleetShip& current) {
    FleetUiState& f = g_fleet;
    if (!f.show_build)
        return;
    Fleet& fleet = world.get_fleet();
    const float fh = ImGui::GetFrameHeight();
    ImGui::SetNextWindowSize(ImVec2(fh * 20.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Build a new ship", &f.show_build, ImGuiWindowFlags_NoCollapse)) {
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

namespace {

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

}  // namespace

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

}  // namespace ui
