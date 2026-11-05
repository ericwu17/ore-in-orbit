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
// Providers: a ship can be set to provide (off by default). Once every
// kDispatchInterval, every ship in a sector with a provider is topped up to
// its bridge's item requests from the providers there, in cargo pods, with
// no clicking (dispatch). A provider gives only what it holds beyond its own
// requests and its pods can carry, and the one with the most of an item to
// spare gives it first (ties: the oldest), so one without pods leaves it to
// one with them. Cargo already on its way to a ship counts toward its
// requests. Automatic sends use up the provider's pods like manual ones
// (asked for cargo pods themselves, it keeps back the ones to carry them), so
// they go in whole pods: a part-filled pod goes only once it covers every
// shortfall it carries, or once it has waited kPartialPodWait.
//
// The milestones are the fleet's: every bridge shares one Progress, so a
// delivery to any of them counts, and a recipe unlocked is unlocked on every
// ship. Cargo that arrives by pod goes straight into the inventory and,
// like hand-crafting, doesn't count toward a milestone.
#pragma once

#include "sim/building.h"
#include "sim/events.h"
#include "sim/game_types.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct FleetShip {
    int id = 0;  // stable for the whole game: what the UI and transfers name it by
    std::string name;
    Ship ship;
    int sector = 0;  // where it is: the index of a sector
    // What its weapons fire at, on the enemy ship in its sector (see
    // combat.h): a building's top-left cell there; none, they pick.
    std::optional<GridPos> target;
    // Sends what its bridge holds beyond its own requests to ships in the
    // same sector that request it (see the top of this file).
    bool provides = false;
};

// A cargo pod carries up to this many items, of any kinds.
constexpr int kPodCapacity = 50;
// How long cargo is on its way.
constexpr Tick kTransferTicks = 5 * kTicksPerSecond;
// How often providers check the requests around them.
constexpr Tick kDispatchInterval = kTicksPerSecond;
// How long a provider holds back a part-filled pod, waiting for more to
// fill it, before sending it anyway.
constexpr Tick kPartialPodWait = 10 * kTicksPerSecond;

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
    bool automatic = false;  // sent by a provider, not by the player
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

    // Since when each provider-to-requester pair (from, to) has had a
    // part-filled pod waiting (saved, so a reloaded game sends it when the
    // original would).
    std::vector<std::pair<std::pair<int, int>, Tick>> partial_since;
    // Providers that had cargo to send but not the pods to carry it all, at
    // the last dispatch. Only the UI reads it: not saved.
    std::vector<int> out_of_pods;

    // Adds `ship`, sharing the fleet's progress, under the next id.
    FleetShip& add(std::string name, Ship ship, int sector);
    // Tops up every ship's requests from the providers in its sector (see
    // the top of this file).
    void dispatch(Tick now);

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
    // How many of `kind` are on their way to ship `to`.
    int in_flight_to(int to, ItemKind kind) const;

    // Makes ship `id` a provider, or not; false if there's none.
    bool set_provides(int id, bool on);
    // Whether provider `id` had cargo to send at the last dispatch but not
    // the cargo pods to carry it all (whether or not it went yet).
    bool short_of_pods(int id) const;

    // Renames ship `id`; false if there's none.
    bool rename(int id, std::string name);
    // Ship `id` is lost (its bridge destroyed): gone from the fleet, and
    // the cargo on its way to it with it (see Transfer).
    void remove(int id);

    // One simulation step at tick `now`: every ship ticks, then the cargo
    // due by `now` lands (each landing reported in `events`), then (every
    // kDispatchInterval) providers send what is requested.
    // Each ship's collectors gather from its sector's reserves on `map`
    // (null: no end to them).
    void tick(Tick now, EventLog& events, StarMap* map = nullptr);
};
