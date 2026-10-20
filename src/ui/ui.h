// ui.h — the only place that knows Dear ImGui exists.
//
// These functions draw one screen each, once per frame. The simulation layer
// knows nothing about them.
#pragma once

#include <filesystem>
#include <string>

class World;

// Which screen the app is showing. main.cpp owns it; the screens switch it.
enum class Screen {
    MainMenu,
    SaveSelect,
    Playing,
    Credits,
};

// The title screen: the game's name and the Play / Credits buttons. Play
// opens the save list.
void DrawMainMenu(Screen& screen);

// What the save list asks main.cpp to do: it owns the World, so it starts
// the game (and switches to Screen::Playing) itself.
struct SaveSelectResult {
    enum class Action {
        None,
        NewGame,
        Load,
    };
    Action action = Action::None;
    std::filesystem::path path;  // Load: the save to load
};

// The save list: New game, and per save Load (or double-click), Rename,
// Copy and Delete; Back or Escape returns to the menu. Rename, copy and
// delete happen here, in the saves folder `saves_dir`. `message` is shown
// under the buttons (why the last action failed): main.cpp puts load errors
// in it, and it is cleared each time the screen opens.
SaveSelectResult DrawSaveSelect(Screen& screen, const std::filesystem::path& saves_dir, std::string& message);

// The credits page (empty for now). Back or Escape returns to the menu.
void DrawCredits(Screen& screen);

// The game itself: the ship build grid and the build palette. Escape returns
// to the menu.
void DrawPlaying(Screen& screen, World& world);

// The ImGui / ImPlot demo windows, toggled with F1 / F2 on any screen. Kept
// handy as a widget reference while developing.
void DrawDevWindows();
