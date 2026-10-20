// ui.h — the only place that knows Dear ImGui exists.
//
// These functions draw one screen each, once per frame. The simulation layer
// knows nothing about them.
#pragma once

// Which screen the app is showing. main.cpp owns it; the screens switch it.
enum class Screen {
    MainMenu,
    Playing,
    Credits,
};

// The title screen: the game's name and the Play / Credits buttons.
void DrawMainMenu(Screen& screen);

// The credits page (empty for now). Back or Escape returns to the menu.
void DrawCredits(Screen& screen);

// The game itself (nothing drawn yet). Escape returns to the menu.
void DrawPlaying(Screen& screen);

// The ImGui / ImPlot demo windows, toggled with F1 / F2 on any screen. Kept
// handy as a widget reference while developing.
void DrawDevWindows();
