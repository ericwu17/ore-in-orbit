// events.h — what just happened in the simulation, for the presentation
// layer to react to (the UI plays sounds for them, see ui/ui_sound.h).
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
// World records events as they happen, each tick, in its EventLog; the UI
// takes them once a frame (World::take_events). Nothing in the simulation
// reads them back, and they are not saved. A log nobody takes (the headless
// tests) keeps only the newest kCapacity.
#pragma once

#include "sim/game_types.h"

#include <cstddef>
#include <vector>

enum class EventKind {
    // In a fight. `sector` is where; `enemy` says whether it happened on (or
    // was done by) the enemy ship rather than the player's ship `ship`.
    WeaponFired,        // `item`: the weapon; one per volley
    ShotHit,            // a shot hit (a building, or bare hull); `item`: what fired it
    ShotBlocked,        // stopped by a shield layer (or a beam weakened to nothing)
    ShotMissed,         // dodged
    ShieldsDown,        // the last shield layer up was knocked down
    ShieldUp,           // a shield layer came back up
    BuildingDestroyed,  // shot to pieces (not the bridge, see below)
    EngineOffline,      // an engine knocked offline
    EnemyEngaged,       // a fight began: one of the fleet's ships met an enemy
    EnemyBeaten,        // its bridge shot down; `ship` got the salvage
    ShipLost,           // the player's ship `ship` lost, with others left
    FleetLost,          // the last of them: the game is over

    // The fleet.
    MilestoneComplete,
    CargoLanded,        // cargo pods landed at ship `ship`
    ThrustersCharged,   // every engine of ship `ship` charged and online: it can jump
    Jumped,             // ship `ship` jumped, to sector `sector`
    SectorSurveyed,     // sector `sector` surveyed for the first time
    ResourceRunOut,     // the last of raw resource `item` in sector `sector` was gathered
};

struct Event {
    EventKind kind = EventKind::WeaponFired;
    int ship = 0;        // the player's ship it concerns (an id), 0 for none
    int sector = -1;     // where it happened, -1 for nowhere in particular
    bool enemy = false;  // in a fight: on (or by) the enemy ship
    ItemKind item = ItemKind::BurstLaser;  // the weapon, for WeaponFired and shots; what ran out
};

class EventLog {
private:
    std::vector<Event> events;  // oldest first

public:
    // The most it keeps: past that, each new event drops the oldest.
    static constexpr std::size_t kCapacity = 256;

    void push(const Event& e) {
        if (events.size() >= kCapacity)
            events.erase(events.begin());
        events.push_back(e);
    }
    // Everything since the last take, oldest first; the log is left empty.
    std::vector<Event> take() {
        std::vector<Event> taken;
        taken.swap(events);
        return taken;
    }
    // What's in it, without taking it (the tests).
    const std::vector<Event>& peek() const { return events; }
};
