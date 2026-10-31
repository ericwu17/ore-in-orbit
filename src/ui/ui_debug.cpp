// ui_debug.cpp — the developer windows: the debug menu (F1) and the ImPlot
// demo window (F2) (see ui.h).
#include "ui/ui.h"
#include "ui/ui_internal.h"

#include "implot.h"  // ImPlot::ShowDemoWindow

#include <algorithm>
#include <cfloat>

namespace ui {

namespace {

// The debug menu's item cheat: what to give, and how many.
struct DebugGiveState {
    ItemKind kind = ItemKind::TransportBelt;
    int count = 100;
    char search[64] = {};
};
DebugGiveState g_debug_give;

// The debug menu, while a game is on: pause and single-step the simulation,
// give the bridge any items, reveal the map, charge the thrusters, send or
// clear a wave, and complete milestones. It is a developer tool, so it is
// drawn in stock Dear ImGui (dark colors, default sizes), not the game's
// theme.
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

}  // namespace ui

// ui.h's screens are global; the helpers they use are in namespace ui.
using namespace ui;

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
