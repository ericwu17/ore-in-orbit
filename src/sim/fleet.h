// fleet.h — Fleet: the player's ships, the milestone progress they share,
// and the cargo pods in flight between them.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// Every ship ticks every tick, whether or not the player is looking at it.
// A new ship is built in a layout the player picks (launch), from another
// ship's bridge inventory: a ship starter pack and the layout's cost. Items
// go from one bridge's inventory to another's in cargo pods (send): one pod,
// used up, per kPodCapacity items, and they land kTransferTicks later. Ships
// trade only with ships in the same sector.
//
// The milestones are the fleet's: every bridge shares one Progress, so a
// delivery to any of them counts, and a recipe unlocked is unlocked on every
// ship. Cargo that arrives by pod goes straight into the inventory and,
// like hand-crafting, doesn't count toward a milestone.
#pragma once

#include "sim/building.h"
#include "sim/combat.h"
#include "sim/game_types.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"

#include <memory>
#include <string>
#include <vector>

struct FleetShip {
    int id = 0;  // stable for the whole game: what the UI and transfers name it by
    std::string name;
    Ship ship;
    int sector = 0;  // where it is: the index of a sector
    Battle battle;   // the enemies on its grid, if any
};

// A cargo pod carries up to this many items, of any kinds.
constexpr int kPodCapacity = 50;
// How long cargo is on its way.
constexpr Tick kTransferTicks = 5 * kTicksPerSecond;

// The pods it takes to send `items` items (0 for none).
int pods_needed(int items);

// Cargo on its way from one ship to another. Its items (and the pods) left
// the sender when it was sent; if the receiver is gone when it lands, they
// are lost.
struct Transfer {
    int from = 0;  // ship ids
    int to = 0;
    std::vector<ItemStack> items;
    Tick arrives = 0;  // the tick it lands on (see Fleet::tick)
};

// Why a launch or a send did (or didn't) happen.
enum class FleetResult {
    Ok,
    NoSuchShip,
    SameShip,       // sending to itself
    OtherSector,    // the ships aren't in the same sector
    LayoutLocked,   // its milestone isn't complete yet
    NoStarterPack,  // launching takes one from the bridge inventory
    NotEnoughForLayout,  // the layout's cost isn't all in the bridge inventory
    NothingToSend,
    NotEnoughItems,
    NotEnoughPods,
};

// A short player-facing explanation ("Not enough cargo pods", ...).
const char* describe(FleetResult result);

class Fleet {
private:
    // Saving and loading (persistence/save_file.cpp) reads and restores
    // the private state.
    friend struct SaveAccess;

    std::vector<std::unique_ptr<FleetShip>> ships;  // oldest first
    int next_id = 1;
    std::shared_ptr<Progress> progress = std::make_shared<Progress>();
    std::vector<Transfer> transfers;  // in flight

    // Adds `ship`, sharing the fleet's progress, under the next id.
    FleetShip& add(std::string name, Ship ship, int sector);

public:
    // No ships yet (loading fills it in). Games start from new_game().
    Fleet() = default;

    // A new game's fleet: one ship, the flagship (Ship::new_game()), in the
    // home sector.
    static Fleet new_game();

    // Every ship, oldest first.
    const std::vector<std::unique_ptr<FleetShip>>& get_ships() const { return ships; }
    // The ship with id `id`, or null.
    FleetShip* find(int id);
    const FleetShip* find(int id) const;
    // The oldest ship: a new game's only one.
    FleetShip& flagship() { return *ships.front(); }
    const FleetShip& flagship() const { return *ships.front(); }

    Progress& get_progress() { return *progress; }
    const Progress& get_progress() const { return *progress; }

    // Whether launch(from, layout) would succeed.
    FleetResult can_launch(int from, const ShipLayout& layout) const;
    // Takes a starter pack and `layout`'s cost from ship `from`'s bridge
    // inventory and launches a new ship in that layout (Ship::from_layout)
    // into the same sector, named "Ship <id>". On success, `launched` (if
    // given) is set to its id.
    FleetResult launch(int from, const ShipLayout& layout, int* launched = nullptr);

    // Whether send(from, to, items) would succeed.
    FleetResult can_send(int from, int to, const std::vector<ItemStack>& items) const;
    // Takes `items` and the pods to carry them from ship `from`'s bridge
    // inventory, to land in ship `to`'s at `now` + kTransferTicks.
    FleetResult send(int from, int to, const std::vector<ItemStack>& items, Tick now);
    const std::vector<Transfer>& in_flight() const { return transfers; }

    // Renames ship `id`; false if there's none.
    bool rename(int id, std::string name);
    // Ship `id` is lost (its bridge destroyed): gone from the fleet, and
    // the cargo on its way to it with it (see Transfer).
    void remove(int id);

    // One simulation step at tick `now`: every ship ticks, then the cargo
    // due by `now` lands.
    void tick(Tick now);
};
