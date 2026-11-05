// combat.cpp — enemy ships, weapons firing, shots landing and salvage (see
// combat.h).
#include "sim/combat.h"

#include "sim/building.h"
#include "sim/ship_layout.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace {

// Places one `kind` (added to its inventory) somewhere on `ship`'s hull that
// it fits, trying the hull's cells in an order `rng` shuffles; false if it
// fits nowhere.
bool place_anywhere(Ship& ship, ItemKind kind, Rng& rng) {
    const auto [corner, extent] = ship.hull_bounds();
    std::vector<GridPos> cells;
    for (int y = corner.y; y < corner.y + extent.h; ++y)
        for (int x = corner.x; x < corner.x + extent.w; ++x)
            cells.push_back({x, y});
    for (std::size_t i = cells.size(); i > 1; --i)
        std::swap(cells[i - 1], cells[static_cast<std::size_t>(rng.range(0, static_cast<int>(i) - 1))]);
    ship.inventory().add(kind);
    for (const GridPos p : cells)
        if (ship.place(kind, p) == BuildResult::Ok)
            return true;
    ship.inventory().take(kind);
    return false;
}

// A cell of the building anchored at `p` on `ship`, picked by `rng`.
GridPos cell_of(const Ship& ship, GridPos p, Rng& rng) {
    const GridSize size = ship.building_anchored_at(p)->footprint();
    return {p.x + rng.range(0, size.w - 1), p.y + rng.range(0, size.h - 1)};
}

// The volley of `weapon` (just fired), aimed at the building anchored at
// `target` on the ship `on`: its shots, on their way.
void fire_volley(std::vector<Shot>& shots, const Ship& on, ItemKind weapon, GridPos target, bool at_enemy, int ship,
                 Rng& rng, Tick now) {
    const WeaponSpec& spec = weapon_spec(weapon);
    Shot shot;
    shot.at_enemy = at_enemy;
    shot.ship = ship;
    shot.weapon = weapon;
    shot.fired = now;
    switch (spec.family) {
    case WeaponFamily::Beam: {
        // Across the target: starting half its length before it.
        shot.sweep = static_cast<Direction>(rng.range(0, 3));
        GridPos start = cell_of(on, target, rng);
        const Direction back = rotated_cw(rotated_cw(shot.sweep));
        for (int i = 0; i < spec.spread / 2; ++i) {
            const GridPos next = neighbor(start, back);
            if (!on.in_bounds(next))
                break;
            start = next;
        }
        shot.cell = start;
        shot.lands = now + spec.flight;
        shots.push_back(shot);
        return;
    }
    case WeaponFamily::Flak:
        // Pellets all round the aim.
        for (int i = 0; i < spec.shots; ++i) {
            const GridPos aim = cell_of(on, target, rng);
            shot.cell = {std::clamp(aim.x + rng.range(-spec.spread, spec.spread), 0, on.size() - 1),
                         std::clamp(aim.y + rng.range(-spec.spread, spec.spread), 0, on.size() - 1)};
            shot.lands = now + spec.flight + static_cast<Tick>(rng.range(0, 6));
            shots.push_back(shot);
        }
        return;
    case WeaponFamily::Laser:
    case WeaponFamily::Missile:
    case WeaponFamily::Ion:
        // One after another, at the one building.
        for (int i = 0; i < spec.shots; ++i) {
            shot.cell = cell_of(on, target, rng);
            shot.lands = now + spec.flight + static_cast<Tick>(i) * (kTicksPerSecond / 6);
            shots.push_back(shot);
        }
        return;
    }
}

// The first shield projector up on `ship`, if any.
ShieldProjector* shield_up(Ship& ship) {
    for (const GridPos p : ship.placement_order())
        if (auto* shield = dynamic_cast<ShieldProjector*>(ship.building_anchored_at(p)); shield && shield->up())
            return shield;
    return nullptr;
}

// `damage` to the building anchored at `p` on `ship`: destroyed at 0,
// unless it's part of the layout (the bridge, its ship lost or beaten; an
// engine, knocked offline). A building destroyed or an engine knocked
// offline is reported in `events`, as `what` says (which ship, which side).
void damage_building(Ship& ship, GridPos p, int damage, Tick now, Event what, EventLog& events) {
    Building& b = *ship.building_anchored_at(p);
    b.hit_at = now;
    const bool standing = b.get_hp() > 0;
    if (!b.take_damage(damage) || !standing || &b == &ship.get_bridge())
        return;
    if (b.removable()) {
        ship.destroy_building(p);
        what.kind = EventKind::BuildingDestroyed;
    } else {
        what.kind = EventKind::EngineOffline;
    }
    events.push(what);
}

// `shot` landing on `ship` (its target), in the fight in sector `sector`.
// Anything it destroys or knocks offline goes into `events` (how it landed
// is the caller's to report: see landed()).
Impact land(const Shot& shot, Ship& ship, int sector, Rng& rng, EventLog& events) {
    const Event what{.kind = EventKind::ShotHit,
                     .ship = shot.ship,
                     .sector = sector,
                     .enemy = shot.at_enemy,
                     .item = shot.weapon};
    const WeaponSpec& spec = weapon_spec(shot.weapon);
    Impact impact{.on_enemy = shot.at_enemy,
                  .ship = shot.ship,
                  .cell = shot.cell,
                  .kind = ImpactKind::Hit,
                  .weapon = shot.weapon,
                  .sweep = shot.sweep,
                  .at = shot.lands};
    if (spec.family == WeaponFamily::Beam) {
        // Every building on its line, once each, weakened by the shields.
        const int damage = std::max(0, spec.damage - kBeamShieldReduction * shield_layers(ship));
        if (damage == 0) {
            impact.kind = ImpactKind::Blocked;
            return impact;
        }
        std::vector<GridPos> hit;
        GridPos c = shot.cell;
        for (int i = 0; i < spec.spread && ship.in_bounds(c); ++i, c = neighbor(c, shot.sweep))
            if (const std::optional<GridPos> a = ship.anchor_of(c);
                a && std::find(hit.begin(), hit.end(), *a) == hit.end())
                hit.push_back(*a);
        for (const GridPos a : hit)
            if (ship.building_anchored_at(a))  // not destroyed by an earlier step
                damage_building(ship, a, damage, shot.lands, what, events);
        return impact;
    }
    if (spec.family != WeaponFamily::Missile)
        if (ShieldProjector* shield = shield_up(ship)) {
            shield->knock_down();
            if (spec.family == WeaponFamily::Ion)
                shield->stun_for(spec.stun);
            impact.kind = ImpactKind::Blocked;
            return impact;
        }
    if (rng.chance(evasion(ship))) {
        impact.kind = ImpactKind::Missed;
        return impact;
    }
    const std::optional<GridPos> a = ship.anchor_of(shot.cell);
    if (!a)
        return impact;  // bare hull, or space
    if (spec.family == WeaponFamily::Ion) {
        Building* b = ship.building_anchored_at(*a);
        if (auto* weapon = dynamic_cast<Weapon*>(b))
            weapon->stun_for(spec.stun);
        else if (auto* shield = dynamic_cast<ShieldProjector*>(b))
            shield->stun_for(spec.stun);
    }
    damage_building(ship, *a, spec.damage, shot.lands, what, events);
    return impact;
}

// The event for `impact`, landed in the fight in sector `sector`.
Event landed(const Impact& impact, int sector) {
    Event e{.kind = EventKind::ShotHit,
            .ship = impact.ship,
            .sector = sector,
            .enemy = impact.on_enemy,
            .item = impact.weapon};
    switch (impact.kind) {
    case ImpactKind::Hit:     e.kind = EventKind::ShotHit; break;
    case ImpactKind::Blocked: e.kind = EventKind::ShotBlocked; break;
    case ImpactKind::Missed:  e.kind = EventKind::ShotMissed; break;
    }
    return e;
}

// Reports each side's shields falling (the last layer knocked down) or
// coming back (a layer more up) since the last step of `fight`, and notes
// them for the next.
void report_shields(Engagement& fight, const std::vector<FleetShip*>& ships, EventLog& events) {
    const auto check = [&](int id, const Ship& ship) {
        const int now = shield_layers(ship);
        const auto seen = std::find_if(fight.layers_seen.begin(), fight.layers_seen.end(),
                                       [&](const std::pair<int, int>& s) { return s.first == id; });
        if (seen == fight.layers_seen.end()) {
            fight.layers_seen.emplace_back(id, now);
            return;
        }
        Event e{.kind = EventKind::ShieldUp, .ship = id, .sector = fight.sector, .enemy = id == 0};
        if (now == 0 && seen->second > 0)
            e.kind = EventKind::ShieldsDown;
        if (now > seen->second || e.kind == EventKind::ShieldsDown)
            events.push(e);
        seen->second = now;
    };
    check(0, fight.ship);
    for (const FleetShip* s : ships)
        check(s->id, s->ship);
}

// Scales `n` by 75% to 125%, at least 1.
int about(int n, Rng& rng) {
    return std::max(1, n * (75 + rng.range(0, 50)) / 100);
}

Salvage salvage_of(const Engagement& fight, int threat, const Progress& progress, Rng& rng, int ship) {
    using enum ItemKind;
    const EnemyDesign& design = fight.design();
    const int t = std::max(threat, 1);
    Inventory items;
    items.add(FuelCell, about(10 + 5 * t, rng));
    items.add(Steel, about(8 * t, rng));
    items.add(Circuit, about(10 * t, rng));
    items.add(IronPlate, about(15 * t, rng));
    if (t >= 2) {
        items.add(AdvancedCircuit, about(3 * t, rng));
        items.add(Plastic, about(5 * t, rng));
    }
    if (t >= 3) {
        items.add(IridiumPlate, about(4 * t, rng));
        items.add(Silicon, about(5 * t, rng));
    }
    if (t >= 4)
        items.add(Processor, about(t, rng));
    // Its magazines, for each kind of ammunition it fired.
    std::vector<ItemKind> ammo;
    for (const ItemKind w : design.weapons)
        if (std::find(ammo.begin(), ammo.end(), weapon_spec(w).ammo) == ammo.end())
            ammo.push_back(weapon_spec(w).ammo);
    for (const ItemKind a : ammo)
        items.add(a, about(6 + 2 * t, rng));
    // Some of its weapons still standing.
    for (const GridPos p : fight.ship.placement_order()) {
        const Building& b = *fight.ship.building_anchored_at(p);
        if (b.item_kind() && is_weapon(*b.item_kind()) && rng.chance(kWeaponSalvageChance))
            items.add(*b.item_kind());
    }
    // Maybe a weapon in its hold: one the fleet hasn't found, if it may
    // carry any, with some ammunition for it.
    if (!design.loot.empty() && rng.chance(design.loot_chance)) {
        std::vector<ItemKind> pick;
        for (const ItemKind w : design.loot)
            if (!progress.has_found(w))
                pick.push_back(w);
        if (pick.empty())
            pick = design.loot;
        const ItemKind w = pick[static_cast<std::size_t>(rng.range(0, static_cast<int>(pick.size()) - 1))];
        items.add(w);
        items.add(weapon_spec(w).ammo, about(6 + 2 * t, rng));
    }
    Salvage salvage{.enemy = fight.enemy, .sector = fight.sector, .ship = ship, .items = items.stacks(), .found = {}};
    return salvage;
}

}  // namespace

Engagement make_engagement(int sector, EnemyClass enemy, Rng& rng) {
    const EnemyDesign& design = enemy_design(enemy);
    const ShipLayout* layout = find_layout(design.layout);
    assert(layout && "make_engagement: the design's layout is missing");
    Engagement fight{.sector = sector, .enemy = enemy, .ship = Ship::from_layout(*layout), .shots = {}, .impacts = {}, .layers_seen = {}};
    Ship& ship = fight.ship;
    ship.get_bridge().set_max_hp(design.hull);
    for (const ItemKind w : design.weapons)
        place_anywhere(ship, w, rng);
    for (int i = 0; i < design.shields; ++i)
        place_anywhere(ship, ItemKind::ShieldProjector, rng);
    for (const ItemKind e : design.extras)
        place_anywhere(ship, e, rng);
    // Part charged: they don't all fire at once.
    for (const GridPos p : ship.placement_order())
        if (auto* w = dynamic_cast<Weapon*>(ship.building_anchored_at(p)))
            w->charge_up(true, rng.range(0, static_cast<int>(w->get_spec().charge) * kChargeScale / 2));
    return fight;
}

int shield_layers(const Ship& ship) {
    int layers = 0;
    for (const GridPos p : ship.placement_order())
        if (const auto* shield = dynamic_cast<const ShieldProjector*>(ship.building_anchored_at(p)); shield && shield->up())
            ++layers;
    return layers;
}

float evasion(const Ship& ship) {
    int online = 0;
    for (const Thruster* t : ship.thrusters())
        online += t->offline() ? 0 : 1;
    return std::min(kMaxEvasion, kEvasionPerEngine * static_cast<float>(online));
}

bool in_target_group(const Ship& ship, GridPos p, TargetGroup group) {
    const Building* b = ship.building_anchored_at(p);
    if (!b)
        return false;
    switch (group) {
    case TargetGroup::Weapons:  return dynamic_cast<const Weapon*>(b) != nullptr;
    case TargetGroup::Shields:  return dynamic_cast<const ShieldProjector*>(b) != nullptr;
    case TargetGroup::Engines: {
        const auto* t = dynamic_cast<const Thruster*>(b);
        return t && !t->offline();
    }
    case TargetGroup::Bridge:   return b == &ship.get_bridge();
    case TargetGroup::Industry:
        return dynamic_cast<const Machine*>(b) || dynamic_cast<const Collector*>(b) ||
               dynamic_cast<const CargoHold*>(b);
    }
    return false;
}

std::optional<GridPos> pick_target(const Ship& target, const std::vector<TargetGroup>& priorities, float random,
                                   Rng& rng) {
    const auto pick = [&](const auto& wanted) -> std::optional<GridPos> {
        std::vector<GridPos> found;
        for (const GridPos p : target.placement_order())
            if (target.building_anchored_at(p)->get_hp() > 0 && wanted(p))
                found.push_back(p);
        if (found.empty())
            return std::nullopt;
        return found[static_cast<std::size_t>(rng.range(0, static_cast<int>(found.size()) - 1))];
    };
    if (random > 0.0f && rng.chance(random))
        if (const std::optional<GridPos> any = pick([](GridPos) { return true; }))
            return any;
    for (const TargetGroup group : priorities)
        if (const std::optional<GridPos> p = pick([&](GridPos q) { return in_target_group(target, q, group); }))
            return p;
    // Nothing it was after left: the bridge.
    return pick([&](GridPos q) { return in_target_group(target, q, TargetGroup::Bridge); });
}

const std::vector<TargetGroup>& default_player_priorities() {
    static const std::vector<TargetGroup> priorities = {TargetGroup::Shields, TargetGroup::Weapons,
                                                        TargetGroup::Bridge};
    return priorities;
}

std::optional<Salvage> combat_tick(Engagement& fight, const std::vector<FleetShip*>& ships, int threat,
                                   const Progress& progress, Rng& rng, Tick now, EventLog& events) {
    Ship& enemy = fight.ship;
    const EnemyDesign& design = fight.design();
    enemy.tick();  // shields recharging, stuns wearing off
    std::erase_if(fight.impacts, [&](const Impact& i) { return now >= i.at + kImpactTicks; });

    // The player's weapons.
    if (!enemy.bridge_destroyed())
        for (FleetShip* s : ships) {
            // Shot to pieces, or (an engine) knocked offline: nothing
            // more to hit there, so pick again rather than waste rounds.
            if (const Building* aimed = s->target ? enemy.building_anchored_at(*s->target) : nullptr;
                s->target && (!aimed || aimed->get_hp() == 0))
                s->target.reset();
            for (const GridPos p : s->ship.placement_order()) {
                auto* w = dynamic_cast<Weapon*>(s->ship.building_anchored_at(p));
                if (!w)
                    continue;
                w->charge_up(false);
                if (!w->charged())
                    continue;
                const std::optional<GridPos> target =
                    s->target ? s->target : pick_target(enemy, default_player_priorities(), 0.0f, rng);
                if (!target)
                    continue;
                w->fire(false);
                fire_volley(fight.shots, enemy, *w->item_kind(), *target, true, s->id, rng, now);
                events.push({.kind = EventKind::WeaponFired,
                             .ship = s->id,
                             .sector = fight.sector,
                             .enemy = false,
                             .item = *w->item_kind()});
            }
        }
    // The enemy's, each at one of the player's ships there.
    std::vector<FleetShip*> targets;
    for (FleetShip* s : ships)
        if (!s->ship.bridge_destroyed())
            targets.push_back(s);
    if (!enemy.bridge_destroyed() && !targets.empty())
        for (const GridPos p : enemy.placement_order()) {
            auto* w = dynamic_cast<Weapon*>(enemy.building_anchored_at(p));
            if (!w)
                continue;
            w->charge_up(true, design.pace);
            if (!w->charged())
                continue;
            FleetShip& at = *targets[static_cast<std::size_t>(rng.range(0, static_cast<int>(targets.size()) - 1))];
            const std::optional<GridPos> target = pick_target(at.ship, design.priorities, kRandomTargetChance, rng);
            if (!target)
                continue;
            w->fire(true);
            fire_volley(fight.shots, at.ship, *w->item_kind(), *target, false, at.id, rng, now);
            events.push({.kind = EventKind::WeaponFired,
                         .ship = at.id,
                         .sector = fight.sector,
                         .enemy = true,
                         .item = *w->item_kind()});
        }

    // Shots landing, in the order they were fired.
    int last_hit_by = 0;
    for (const Shot& shot : fight.shots) {
        if (shot.lands > now)
            continue;
        Ship* on = nullptr;
        if (shot.at_enemy) {
            on = &enemy;
        } else {
            for (FleetShip* s : ships)
                if (s->id == shot.ship)
                    on = &s->ship;
        }
        if (!on || on->bridge_destroyed())
            continue;  // gone, or lost already
        fight.impacts.push_back(land(shot, *on, fight.sector, rng, events));
        events.push(landed(fight.impacts.back(), fight.sector));
        if (shot.at_enemy && enemy.bridge_destroyed() && last_hit_by == 0)
            last_hit_by = shot.ship;
    }
    std::erase_if(fight.shots, [&](const Shot& s) { return s.lands <= now; });
    report_shields(fight, ships, events);

    if (!enemy.bridge_destroyed())
        return std::nullopt;
    // Beaten: the salvage goes to the ship that fired the shot that did it
    // (or any one there, if it's gone).
    int to = 0;
    for (FleetShip* s : ships)
        if (s->id == last_hit_by && !s->ship.bridge_destroyed())
            to = s->id;
    if (to == 0)
        for (FleetShip* s : ships)
            if (!s->ship.bridge_destroyed()) {
                to = s->id;
                break;
            }
    return salvage_of(fight, threat, progress, rng, to);
}
