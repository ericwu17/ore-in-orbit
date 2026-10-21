// world.cpp — the simulation step, and travel between sectors (see
// world.h).
#include "sim/world.h"

#include <algorithm>

int thrusters_needed(int hull_tiles) {
    return std::max(1, (hull_tiles + kHullPerThruster - 1) / kHullPerThruster);
}

const char* describe(JumpResult result) {
    switch (result) {
    case JumpResult::Ok:              return "Ok";
    case JumpResult::NoSuchShip:      return "No such ship";
    case JumpResult::NoSuchSector:    return "No such sector";
    case JumpResult::NotLinked:       return "Only a sector linked to this one is in reach";
    case JumpResult::NoThrusters:     return "Needs ion thrusters";
    case JumpResult::TooFewThrusters: return "Too few ion thrusters for a ship this size";
    case JumpResult::NotCharged:      return "Not every ion thruster is charged with fuel cells";
    }
    return "?";
}

World::World(std::uint64_t map_seed) : map(StarMap::generate(map_seed)), rng{map_seed ^ 0x5EC7012ull} {
    update_yields();
}

void World::update_yields() {
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        s->ship.set_yields(map.at(s->sector).yields);
}

void World::scan() {
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        if (s->ship.has_building(ItemKind::SurveyScanner))
            for (const int n : map.at(s->sector).neighbors)
                map.survey(n);
}

void World::fight() {
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        combat_tick(s->ship, s->battle, map.at(s->sector), rng, current_tick);
    std::vector<int> lost;
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        if (s->ship.bridge_destroyed())
            lost.push_back(s->id);
    for (const int id : lost)
        fleet.remove(id);
}

void World::advance_tick() {
    if (fleet_lost())
        return;
    ++current_tick;
    fleet.tick(current_tick);
    fight();
    if (current_tick % kTicksPerSecond == 0) {
        scan();
        for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
            if (!s->battle.under_attack())
                s->ship.repair_all(kRepairPercent);
    }
}

bool World::send_wave(int ship) {
    FleetShip* s = fleet.find(ship);
    if (!s)
        return false;
    Sector& sector = map.at(s->sector);
    const int threat = std::max(sector.threat, 1);
    spawn_wave(s->ship, s->battle, threat, std::max(waves_for_threat(threat) - sector.waves_left, 0), rng);
    return true;
}

void World::clear_enemies(int ship) {
    if (FleetShip* s = fleet.find(ship))
        s->battle.enemies.clear();
}

void World::reveal_map() {
    for (int i = 0; i < map.size(); ++i)
        map.survey(i);
}

JumpResult World::can_jump(int ship, int sector) const {
    const FleetShip* s = fleet.find(ship);
    if (!s)
        return JumpResult::NoSuchShip;
    if (sector < 0 || sector >= map.size())
        return JumpResult::NoSuchSector;
    if (!map.linked(s->sector, sector))
        return JumpResult::NotLinked;
    const std::vector<const Thruster*> thrusters = s->ship.thrusters();
    if (thrusters.empty())
        return JumpResult::NoThrusters;
    if (static_cast<int>(thrusters.size()) < thrusters_needed(s->ship.hull_count()))
        return JumpResult::TooFewThrusters;
    if (!std::all_of(thrusters.begin(), thrusters.end(), [](const Thruster* t) { return t->charged(); }))
        return JumpResult::NotCharged;
    return JumpResult::Ok;
}

JumpResult World::jump(int ship, int sector) {
    const JumpResult result = can_jump(ship, sector);
    if (result != JumpResult::Ok)
        return result;
    FleetShip& s = *fleet.find(ship);
    s.ship.burn_thrusters();
    s.battle = {};
    s.sector = sector;
    s.ship.set_yields(map.at(sector).yields);
    map.visit(sector);
    return JumpResult::Ok;
}
