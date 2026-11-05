// world.cpp — the simulation step, and travel between sectors (see
// world.h).
#include "sim/world.h"

#include <algorithm>

const char* describe(JumpResult result) {
    switch (result) {
    case JumpResult::Ok:              return "Ok";
    case JumpResult::NoSuchShip:      return "No such ship";
    case JumpResult::NoSuchSector:    return "No such sector";
    case JumpResult::NotLinked:       return "Only a sector linked to this one is in reach";
    case JumpResult::NoThrusters:     return "Needs ion thrusters";
    case JumpResult::EngineOffline:   return "An ion thruster is knocked offline: wait for it to mend";
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

std::vector<FleetShip*> World::ships_in(int sector) {
    std::vector<FleetShip*> here;
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        if (s->sector == sector)
            here.push_back(s.get());
    return here;
}

void World::fight() {
    // A fight wherever a ship meets an enemy.
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships()) {
        const Sector& sector = map.at(s->sector);
        if (sector.enemy && !engagement_in(s->sector))
            engagements.push_back(make_engagement(s->sector, *sector.enemy, rng));
    }
    std::vector<Salvage> beaten;
    std::erase_if(engagements, [&](Engagement& e) {
        const std::vector<FleetShip*> here = ships_in(e.sector);
        if (here.empty())
            return true;  // left behind: the enemy waits as it was found
        const Sector& sector = map.at(e.sector);
        std::optional<Salvage> salvage =
            combat_tick(e, here, sector.threat, fleet.get_progress(), rng, current_tick);
        if (!salvage)
            return false;
        beaten.push_back(std::move(*salvage));
        return true;
    });
    for (Salvage& s : beaten)
        collect(std::move(s));

    std::vector<int> lost;
    for (const std::unique_ptr<FleetShip>& s : fleet.get_ships())
        if (s->ship.bridge_destroyed())
            lost.push_back(s->id);
    for (const int id : lost)
        fleet.remove(id);
}

void World::collect(Salvage salvage) {
    Sector& sector = map.at(salvage.sector);
    sector.enemy.reset();
    sector.beaten = true;
    for (FleetShip* s : ships_in(salvage.sector))
        s->target.reset();
    if (FleetShip* s = fleet.find(salvage.ship))
        s->ship.inventory().add(salvage.items);
    for (const ItemStack& item : salvage.items)
        if (fleet.get_progress().add_found(item.kind))
            salvage.found.push_back(item.kind);
    salvaged.push_back(std::move(salvage));
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
            if (!engagement_in(s->sector))
                s->ship.repair_all(kRepairPercent);
    }
}

const Engagement* World::engagement_in(int sector) const {
    for (const Engagement& e : engagements)
        if (e.sector == sector)
            return &e;
    return nullptr;
}

bool World::set_target(int ship, std::optional<GridPos> cell) {
    FleetShip* s = fleet.find(ship);
    if (!s)
        return false;
    const Engagement* e = engagement_in(s->sector);
    if (!e)
        return false;
    if (!cell) {
        s->target.reset();
        return true;
    }
    const std::optional<GridPos> anchor = e->ship.anchor_of(*cell);
    if (!anchor)
        return false;
    s->target = anchor;
    return true;
}

std::vector<Salvage> World::take_salvage() {
    std::vector<Salvage> taken;
    taken.swap(salvaged);
    return taken;
}

bool World::spawn_enemy(int ship, EnemyClass enemy) {
    FleetShip* s = fleet.find(ship);
    if (!s)
        return false;
    std::erase_if(engagements, [&](const Engagement& e) { return e.sector == s->sector; });
    for (FleetShip* here : ships_in(s->sector))
        here->target.reset();
    Sector& sector = map.at(s->sector);
    sector.enemy = enemy;
    sector.beaten = false;
    return true;
}

bool World::defeat_enemy(int ship) {
    const FleetShip* s = fleet.find(ship);
    if (!s)
        return false;
    for (Engagement& e : engagements)
        if (e.sector == s->sector) {
            Bridge& bridge = e.ship.get_bridge();
            bridge.take_damage(bridge.get_hp());
            return true;
        }
    return false;
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
    if (std::any_of(thrusters.begin(), thrusters.end(), [](const Thruster* t) { return t->offline(); }))
        return JumpResult::EngineOffline;
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
    for (const GridPos p : s.ship.placement_order())
        if (auto* w = dynamic_cast<Weapon*>(s.ship.building_anchored_at(p)))
            w->discharge();
    s.target.reset();
    s.sector = sector;
    s.ship.set_yields(map.at(sector).yields);
    map.visit(sector);
    return JumpResult::Ok;
}
