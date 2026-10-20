// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
#pragma once

#include "sim/game_types.h"
#include "sim/ship.h"

class World {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    Tick current_tick = 0;
    Ship ship = Ship::new_game();

public:
    // One fixed simulation step (kTicksPerSecond of them per real second).
    void advance_tick();

    Tick get_tick() const { return current_tick; }

    Ship& get_ship() { return ship; }
    const Ship& get_ship() const { return ship; }
};
