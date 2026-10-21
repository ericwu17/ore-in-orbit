// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
#pragma once

#include "sim/fleet.h"
#include "sim/game_types.h"
#include "sim/ship.h"

class World {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Tick current_tick = 0;
    Fleet fleet = Fleet::new_game();

public:
    // One fixed simulation step (kTicksPerSecond of them per real second).
    void advance_tick();

    Tick get_tick() const { return current_tick; }

    Fleet& get_fleet() { return fleet; }
    const Fleet& get_fleet() const { return fleet; }
    // The fleet's oldest ship (a new game's only one).
    Ship& flagship() { return fleet.flagship().ship; }
    const Ship& flagship() const { return fleet.flagship().ship; }
};
