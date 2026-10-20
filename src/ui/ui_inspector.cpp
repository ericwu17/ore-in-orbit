// ui_inspector.cpp — the side panel docked on the left of the main area. A
// placeholder showing how a window pins itself into the dockspace's left
// node (see DrawDockspace); replace its contents with your own.
#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/ui_theme.h"

#include "sim/world.h"
#include "imgui.h"
#include "imgui_internal.h"  // ImGuiDockNodeFlags_NoWindowMenuButton / _NoDockingOverMe

void DrawInspector(World& world) {
    // Pinned into the left dock node: re-docked every frame, and its node
    // can't be dragged out, docked over, or undocked via the window menu.
    // The lone tab is hidden too: the panel draws its own header.
    ImGuiWindowClass inspector_class;
    inspector_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoUndocking |
                                               (int)ImGuiDockNodeFlags_NoWindowMenuButton |
                                               (int)ImGuiDockNodeFlags_NoDockingOverMe |
                                               (int)ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&inspector_class);
    ImGui::SetNextWindowDockID(ui::InspectorDockId(), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::Begin("Inspector###Inspector");
    ImGui::PopStyleVar();

    ui::theme::SectionHeading("Inspector");
    ImGui::TextWrapped("A placeholder panel. The example simulation in world.h queues items "
                       "at random and processes them one at a time.");
    ImGui::Spacing();
    ImGui::Text("Items waiting: %lld", static_cast<long long>(world.get_queue()));
    ImGui::SameLine();
    ui::theme::HelpMarker("Charted over time in the statistics window (L).");
    if (ui::theme::PrimaryButton(world.is_paused() ? "Resume" : "Pause"))
        world.set_paused(!world.is_paused());
    ImGui::End();
}
