// world.h — World: owns all simulation state and drives it tick by tick.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
#pragma once

#include "sim/game_types.h"

class World {
private:
    Tick current_tick = 0;

public:
    // One fixed simulation step (kTicksPerSecond of them per real second).
    void advance_tick();

    Tick get_tick() const { return current_tick; }
};
