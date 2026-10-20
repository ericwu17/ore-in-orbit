// ui.h — the only place that knows Dear ImGui exists.
//
// These functions read the World (and let the player mutate it through its
// public API), once per frame. The simulation layer knows nothing about them.
#pragma once

#include "sim/world.h"

// Forgets the UI state that belongs to one game (the statistics window and
// anything else in ui::GameUiState), so the next game starts clean.
void ResetGameUi();

// Fullscreen dockspace over the main viewport, hosting the dockable windows.
// The central node is passthrough (transparent and click-through) so anything
// drawn into the background draw list stays visible and interactive. On first
// run builds the default layout: the inspector docked into a left split;
// afterwards the user's own arrangement is loaded from imgui.ini untouched.
void DrawDockspace();

// Top-center transport bar: play/pause, slow-down/speed-up and the day/time
// readout, drawn into the background draw list. Laid out from the display
// size each frame, so it survives window resizes.
void DrawTransportBar(World& world);

// Status card pinned to the top-right of the main area: a headline number, a
// few stat rows and the statistics button. Also toggles the ImGui / ImPlot
// demo windows (F1 / F2).
void DrawHUD(const World& world);

// The side panel docked on the left (ui_inspector.cpp). A placeholder: put
// whatever the player inspects or edits here.
void DrawInspector(World& world);

// The statistics window (toggled with L or the HUD button): charts of the
// World's statistics over a chosen time range.
void DrawStatsWindow(const World& world);
