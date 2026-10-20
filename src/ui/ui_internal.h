// ui_internal.h — state and helpers shared between the presentation-layer
// files (ui.cpp, ui_hud.cpp, ui_inspector.cpp and ui_stats.cpp). Not part of
// ui.h's public surface: main.cpp and the simulation never see these.
#pragma once

#include "sim/game_types.h"
#include "imgui.h"

#include <cstddef>

class World;

namespace ui {

// ---------------------------------------------------------------------------
// Per-game UI state
// ---------------------------------------------------------------------------

// Everything the presentation layer remembers about one game, in one place,
// so ResetGameUi() forgets it all with a single assignment. Each ui_*.cpp
// reads and writes its own part of g_game.
struct GameUiState {
    // The statistics window (ui_stats.cpp). Its time range is a viewing
    // preference that carries over between games, so it is not kept here.
    bool stats_open = false;
};

extern GameUiState g_game;  // defined in ui.cpp

// --- ui.cpp -----------------------------------------------------------------

// Tick -> "Day N  HH:MM" / duration -> "HH:MM" (hours may exceed 23).
void FormatDayTime(Tick tick, char* buf, std::size_t buf_len);
void FormatDuration(Tick duration, char* buf, std::size_t buf_len);

// The main area: the dockspace's central node, which excludes the docked
// inspector (the whole display before the dockspace first exists).
void GetMainArea(ImVec2& min, ImVec2& size);

// The left dock node the inspector is pinned into (set by DrawDockspace).
ImGuiID InspectorDockId();

// --- ui_hud.cpp -------------------------------------------------------------

// Whether screen point `p` is over the transport bar (which anything drawn
// in the main area must not treat as a click on itself).
bool OverTransportBar(const World& world, ImVec2 p);

}  // namespace ui
