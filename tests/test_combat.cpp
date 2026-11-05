// test_combat.cpp — headless tests for ship-to-ship combat: weapons fed
// fuel cells and ammunition (and silent without), charging and firing
// volleys, shields stopping lasers but not missiles, ion stuns, beams,
// evasion, the enemy's target priorities, beating an enemy for its salvage
// and the weapons it carries, leaving a fight, losing the fleet, and the same seed
// fighting the same fight.
#include "sim/blueprint.h"
#include "sim/building.h"
#include "sim/combat.h"
#include "sim/enemies.h"
#include "sim/fleet.h"
#include "sim/milestones.h"
#include "sim/recipes.h"
#include "sim/sector.h"
#include "sim/ship.h"
#include "sim/ship_layout.h"
#include "sim/world.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>

namespace {

// East of the bridge, on the starting hull: room for a 2x2 or 3x3 building
// at each.
constexpr GridPos kGun{72, 64};
constexpr GridPos kGun2{72, 68};
constexpr GridPos kGun3{76, 64};
// West of the bridge.
constexpr GridPos kShield{52, 64};

// Feeds the building at `p` up to `n` of `kind`, from the cell above it as
// from a belt.
void feed(Ship& ship, GridPos p, ItemKind kind, int n) {
    for (int i = 0; i < n; ++i)
        if (!ship.insert_item(p, {p.x, p.y - 1}, kind))
            return;
}

// A `kind` weapon placed at `at` on `ship`, fed `fuel` fuel cells and
// `ammo` rounds.
Weapon& place_weapon(Ship& ship, ItemKind kind, GridPos at, int fuel, int ammo) {
    ship.inventory().add(kind);
    const BuildResult placed = ship.place(kind, at);
    assert(placed == BuildResult::Ok);
    (void)placed;
    feed(ship, at, ItemKind::FuelCell, fuel);
    feed(ship, at, weapon_spec(kind).ammo, ammo);
    return *dynamic_cast<Weapon*>(ship.building_anchored_at(at));
}

// Keeps every weapon on `ship` full.
void top_up(Ship& ship) {
    for (const GridPos p : ship.placement_order())
        if (const auto* w = dynamic_cast<const Weapon*>(ship.building_anchored_at(p))) {
            feed(ship, p, ItemKind::FuelCell, kWeaponFuelCapacity);
            feed(ship, p, w->get_spec().ammo, kWeaponAmmoCapacity);
        }
}

// Knocks every engine on `ship` offline: nothing it's fired at misses.
void ground(Ship& ship) {
    for (const GridPos p : ship.placement_order())
        if (auto* t = dynamic_cast<Thruster*>(ship.building_anchored_at(p)))
            t->take_damage(t->get_hp());
}

// Destroys every weapon on `ship`.
void disarm(Ship& ship) {
    std::vector<GridPos> weapons;
    for (const GridPos p : ship.placement_order())
        if (dynamic_cast<const Weapon*>(ship.building_anchored_at(p)))
            weapons.push_back(p);
    for (const GridPos p : weapons)
        ship.destroy_building(p);
}

// Where the bridge of `ship` is (its top-left cell).
GridPos bridge_of(const Ship& ship) {
    for (const GridPos p : ship.placement_order())
        if (ship.building_anchored_at(p) == &ship.get_bridge())
            return p;
    assert(false);
    return {};
}

FleetShip new_flagship() {
    return FleetShip{1, "Flagship", Ship::new_game(), 0, std::nullopt};
}

// Steps the fight `ticks` times: the player's ship, then the fight. Returns
// the salvage, if the enemy was beaten.
std::optional<Salvage> fight(Engagement& e, FleetShip& player, Rng& rng, Tick& now, Tick ticks) {
    const std::vector<FleetShip*> ships{&player};
    const Progress progress;
    for (Tick t = 0; t < ticks; ++t) {
        ++now;
        player.ship.tick();
        if (std::optional<Salvage> s = combat_tick(e, ships, 1, progress, rng, now))
            return s;
    }
    return std::nullopt;
}

int count_weapons(const Ship& ship) {
    int n = 0;
    for (const GridPos p : ship.placement_order())
        n += dynamic_cast<const Weapon*>(ship.building_anchored_at(p)) ? 1 : 0;
    return n;
}

int count_shields(const Ship& ship) {
    int n = 0;
    for (const GridPos p : ship.placement_order())
        n += dynamic_cast<const ShieldProjector*>(ship.building_anchored_at(p)) ? 1 : 0;
    return n;
}

void test_ids() {
    for (int i = 0; i < kEnemyClassCount; ++i) {
        const EnemyClass c = static_cast<EnemyClass>(i);
        assert(enemy_from_id(enemy_id(c)) == c);
        const EnemyDesign& d = enemy_design(c);
        // Its layout is an enemy's, never the player's to build.
        const ShipLayout* layout = find_layout(d.layout);
        assert(layout && layout->milestone == std::nullopt);
        for (const ShipLayout& l : all_layouts())
            assert(std::string_view(l.id) != d.layout);
        assert(!d.weapons.empty() && d.hull > 0 && d.hull <= max_hp_of(std::nullopt));
        for (const ItemKind w : d.weapons)
            assert(is_weapon(w));
        for (const ItemKind w : d.loot)
            assert(is_weapon(w));
    }
    assert(!enemy_from_id("Kraken"));
    assert(enemies_for_threat(0).empty());
    for (int t = 1; t <= kMaxThreat; ++t)
        assert(!enemies_for_threat(t).empty());
    // Next to home, only the lightest.
    assert(enemies_for_threat(1) == std::vector<EnemyClass>{EnemyClass::Scout});
}

// Every design's buildings fit its layout, whatever the seed.
void test_enemies_built() {
    for (int i = 0; i < kEnemyClassCount; ++i)
        for (std::uint64_t seed = 1; seed <= 30; ++seed) {
            Rng rng{seed};
            const EnemyClass c = static_cast<EnemyClass>(i);
            const Engagement e = make_engagement(3, c, rng);
            const EnemyDesign& d = enemy_design(c);
            assert(e.sector == 3 && e.enemy == c);
            assert(count_weapons(e.ship) == static_cast<int>(d.weapons.size()));
            assert(count_shields(e.ship) == d.shields);
            assert(e.ship.get_bridge().get_hp() == d.hull && e.ship.get_bridge().max_hp() == d.hull);
            assert(shield_layers(e.ship) == d.shields);
        }
}

// A ship holds no more weapons or shield projectors than its layout has
// ports for, placed one at a time or pasted from a blueprint; each layout
// has some, more the bigger it is.
void test_ports() {
    int weapons = 0;
    for (const ShipLayout& l : all_layouts()) {
        assert(l.weapon_ports >= weapons && l.shield_ports >= 1);
        weapons = l.weapon_ports;
    }
    for (int i = 0; i < kEnemyClassCount; ++i) {
        const EnemyDesign& d = enemy_design(static_cast<EnemyClass>(i));
        const ShipLayout& l = *find_layout(d.layout);
        assert(static_cast<int>(d.weapons.size()) <= l.weapon_ports && d.shields <= l.shield_ports);
    }
    assert(port_of(ItemKind::BeamEmitter) == Port::Weapon && port_of(ItemKind::ShieldProjector) == Port::Shield);
    assert(!port_of(ItemKind::CargoHold) && !port_of(ItemKind::FocusCrystal));

    World world;
    Ship& ship = world.flagship();
    assert(ship.ports(Port::Weapon) == 2 && ship.ports(Port::Shield) == 1);
    place_weapon(ship, ItemKind::BurstLaser, kGun, 0, 0);
    place_weapon(ship, ItemKind::BurstLaser, kGun2, 0, 0);
    assert(ship.ports_taken(Port::Weapon) == 2);
    ship.inventory().add(ItemKind::BurstLaser);
    assert(ship.place(ItemKind::BurstLaser, kGun3) == BuildResult::NoWeaponPort);
    ship.inventory().add(ItemKind::ShieldProjector, 2);
    assert(ship.place(ItemKind::ShieldProjector, kShield) == BuildResult::Ok);
    assert(ship.place(ItemKind::ShieldProjector, {kShield.x, kShield.y + 4}) == BuildResult::NoShieldPort);
    // Removing one frees its port.
    assert(ship.remove_building(kGun2) == BuildResult::Ok && ship.ports_taken(Port::Weapon) == 1);
    assert(ship.can_place(ItemKind::BurstLaser, kGun3) == BuildResult::Ok);
    // Two weapons pasted where one port is free: the second doesn't fit.
    const std::optional<Blueprint> pair = copy_region(ship, kGun, {kGun.x + 1, kGun.y + 1});
    assert(pair && pair->buildings.size() == 1);
    Blueprint two = *pair;
    two.buildings.push_back(two.buildings[0]);
    two.buildings[1].offset = {0, 2};
    two.size = {2, 4};
    const PastePlan plan = plan_paste(ship, two, kGun3);
    assert(plan.blocked == 1 && plan.entries[1].result == BuildResult::NoWeaponPort);
}

// A weapon takes fuel cells and its own ammunition, up to what it holds,
// and gives them back when removed.
void test_weapon_feeding() {
    Ship ship = Ship::new_game();
    Weapon& w = place_weapon(ship, ItemKind::MissileLauncher, kGun, 0, 0);
    assert(!ship.insert_item(kGun, {kGun.x, kGun.y - 1}, ItemKind::FocusCrystal));  // another weapon's
    assert(!ship.insert_item(kGun, {kGun.x, kGun.y - 1}, ItemKind::IronPlate));
    feed(ship, kGun, ItemKind::FuelCell, 100);
    feed(ship, kGun, ItemKind::Missile, 100);
    assert(w.get_fuel() == kWeaponFuelCapacity && w.get_ammo() == kWeaponAmmoCapacity);
    assert(w.powered());
    const int cells = ship.inventory().count(ItemKind::FuelCell);
    assert(ship.remove_building(kGun) == BuildResult::Ok);
    assert(ship.inventory().count(ItemKind::FuelCell) == cells + kWeaponFuelCapacity);
    assert(ship.inventory().count(ItemKind::Missile) == kWeaponAmmoCapacity);
}

// A weapon charges only with both fuel and ammunition; a volley burns one of
// each and fires all its shots.
void test_needs_power() {
    for (const auto& [fuel, ammo] : {std::pair{0, 5}, std::pair{5, 0}}) {
        FleetShip player = new_flagship();
        Weapon& w = place_weapon(player.ship, ItemKind::BurstLaser, kGun, fuel, ammo);
        Rng rng{1};
        Tick now = 0;
        Engagement e = make_engagement(0, EnemyClass::Scout, rng);
        fight(e, player, rng, now, 2 * w.get_spec().charge);
        assert(w.charge_fraction() == 0.0f);
        for (const Shot& s : e.shots)
            assert(!s.at_enemy);
    }
    FleetShip player = new_flagship();
    Weapon& w = place_weapon(player.ship, ItemKind::BurstLaser, kGun, 2, 2);
    Rng rng{1};
    Tick now = 0;
    Engagement e = make_engagement(0, EnemyClass::Scout, rng);
    fight(e, player, rng, now, w.get_spec().charge - 1);
    assert(w.charge_fraction() > 0.9f && w.get_fuel() == 2);
    fight(e, player, rng, now, 1);
    assert(w.get_fuel() == 1 && w.get_ammo() == 1 && w.charge_fraction() == 0.0f);
    int shots = 0;
    for (const Shot& s : e.shots)
        shots += s.at_enemy ? 1 : 0;
    assert(shots == w.get_spec().shots);
}

// Without an enemy, weapons don't charge (World: no fight).
void test_no_fight_no_charge() {
    World world;
    Ship& ship = world.flagship();
    Weapon& w = place_weapon(ship, ItemKind::BurstLaser, kGun, 5, 5);
    for (int i = 0; i < 20 * static_cast<int>(kTicksPerSecond); ++i)
        world.advance_tick();
    assert(w.charge_fraction() == 0.0f && w.get_fuel() == 5);
    assert(!world.engagement_in(0));
}

// A laser volley at a shielded enemy: the first shot knocks its one layer
// down, the rest hit; the layer comes back after kShieldRecharge.
void test_shields_stop_lasers() {
    FleetShip player = new_flagship();
    Weapon& w = place_weapon(player.ship, ItemKind::BurstLaser, kGun, 5, 5);
    Rng rng{2};
    Tick now = 0;
    Engagement e = make_engagement(0, EnemyClass::Raider, rng);
    ground(e.ship);
    player.target = bridge_of(e.ship);
    const WeaponSpec& spec = w.get_spec();
    assert(shield_layers(e.ship) == 1);
    fight(e, player, rng, now, spec.charge + spec.flight + kTicksPerSecond / 2);
    assert(shield_layers(e.ship) == 0);
    assert(e.ship.get_bridge().get_hp() == enemy_design(EnemyClass::Raider).hull - (spec.shots - 1) * spec.damage);
    // Back up once it has recharged (before the next volley lands).
    fight(e, player, rng, now, kShieldRecharge);
    assert(shield_layers(e.ship) == 1);
}

// Shield layers come back one at a time: two knocked down, one is up
// after kShieldRecharge and both after twice that; a stunned projector
// waits, while the other recharges.
void test_shields_one_at_a_time() {
    Rng rng{4};
    Engagement e = make_engagement(0, EnemyClass::Corsair, rng);
    std::vector<ShieldProjector*> shields;
    for (const GridPos p : e.ship.placement_order())
        if (auto* s = dynamic_cast<ShieldProjector*>(e.ship.building_anchored_at(p)))
            shields.push_back(s);
    const int all = static_cast<int>(shields.size());
    assert(all >= 2 && shield_layers(e.ship) == all);
    const auto run = [&](Tick ticks) {
        for (Tick t = 0; t < ticks; ++t)
            e.ship.tick();
    };
    shields[0]->knock_down();
    shields[1]->knock_down();
    assert(shield_layers(e.ship) == all - 2);
    run(kShieldRecharge - 1);
    assert(shield_layers(e.ship) == all - 2);
    run(1);
    assert(shield_layers(e.ship) == all - 1);
    run(kShieldRecharge);
    assert(shield_layers(e.ship) == all);
    // One stunned: the other comes back first.
    shields[0]->knock_down();
    shields[1]->knock_down();
    shields[0]->stun_for(3 * kShieldRecharge);
    run(kShieldRecharge);
    assert(!shields[0]->up() && shields[1]->up());
    run(kShieldRecharge);
    assert(!shields[0]->up());
    run(2 * kShieldRecharge);
    assert(shields[0]->up());
}

// Missiles fly through shields; ion bolts knock a layer down and stun its
// projector, so it stays down past kShieldRecharge.
void test_missiles_and_ion() {
    {
        FleetShip player = new_flagship();
        Weapon& w = place_weapon(player.ship, ItemKind::MissileLauncher, kGun, 5, 5);
        Rng rng{3};
        Tick now = 0;
        Engagement e = make_engagement(0, EnemyClass::Gunship, rng);
        ground(e.ship);
        disarm(e.ship);  // or it shoots the launcher down first
        player.target = bridge_of(e.ship);
        fight(e, player, rng, now, w.get_spec().charge + w.get_spec().flight + 1);
        assert(shield_layers(e.ship) == 1);
        assert(e.ship.get_bridge().get_hp() == enemy_design(EnemyClass::Gunship).hull - w.get_spec().damage);
    }
    FleetShip player = new_flagship();
    Weapon& w = place_weapon(player.ship, ItemKind::IonBlaster, kGun, 1, 1);
    Rng rng{4};
    Tick now = 0;
    Engagement e = make_engagement(0, EnemyClass::Raider, rng);
    ground(e.ship);
    player.target = bridge_of(e.ship);
    fight(e, player, rng, now, w.get_spec().charge + w.get_spec().flight + 1);
    assert(shield_layers(e.ship) == 0);
    assert(e.ship.get_bridge().get_hp() == enemy_design(EnemyClass::Raider).hull);  // stopped by the shield
    fight(e, player, rng, now, kShieldRecharge + kTicksPerSecond);
    assert(shield_layers(e.ship) == 0);  // stunned: not recharging
    fight(e, player, rng, now, w.get_spec().stun);
    assert(shield_layers(e.ship) == 1);
}

// An ion bolt that gets through stuns the weapon it hits: it stops charging.
void test_ion_stuns_weapons() {
    FleetShip player = new_flagship();
    Weapon& w = place_weapon(player.ship, ItemKind::IonBlaster, kGun, 5, 5);
    Rng rng{5};
    Tick now = 0;
    Engagement e = make_engagement(0, EnemyClass::Scout, rng);
    ground(e.ship);
    std::optional<GridPos> gun;
    for (const GridPos p : e.ship.placement_order())
        if (dynamic_cast<const Weapon*>(e.ship.building_anchored_at(p)))
            gun = p;
    player.target = gun;
    fight(e, player, rng, now, w.get_spec().charge + w.get_spec().flight + 1);
    const auto& theirs = dynamic_cast<const Weapon&>(*e.ship.building_anchored_at(*gun));
    assert(theirs.get_stun() > 0);
    const float charge = theirs.charge_fraction();
    fight(e, player, rng, now, kTicksPerSecond);
    assert(theirs.charge_fraction() == charge);
}

// A beam never misses, hits every building on its line once, and each
// shield layer up takes kBeamShieldReduction off.
void test_beam() {
    for (const EnemyClass c : {EnemyClass::Scout, EnemyClass::Raider}) {
        FleetShip player = new_flagship();
        Weapon& w = place_weapon(player.ship, ItemKind::BeamEmitter, kGun, 5, 5);
        Rng rng{6};
        Tick now = 0;
        Engagement e = make_engagement(0, c, rng);  // engines online: it still hits
        player.target = bridge_of(e.ship);
        fight(e, player, rng, now, w.get_spec().charge + w.get_spec().flight + 1);
        const int layers = enemy_design(c).shields;
        assert(shield_layers(e.ship) == layers);  // not knocked down
        assert(e.ship.get_bridge().get_hp() ==
               enemy_design(c).hull - (w.get_spec().damage - kBeamShieldReduction * layers));
    }
}

// Every online engine dodges kEvasionPerEngine; offline ones don't.
void test_evasion() {
    Ship ship = Ship::new_game();
    assert(evasion(ship) == kEvasionPerEngine * static_cast<float>(ship.thrusters().size()));
    ground(ship);
    assert(evasion(ship) == 0.0f);
}

// Down an enemy's priorities: a Raider shoots engines, then weapons, then
// the bridge.
void test_priorities() {
    Ship ship = Ship::new_game();
    place_weapon(ship, ItemKind::BurstLaser, kGun, 0, 0);
    ship.inventory().add(ItemKind::AsteroidCollector);
    assert(ship.place(ItemKind::AsteroidCollector, kShield) == BuildResult::Ok);
    const std::vector<TargetGroup>& raider = enemy_design(EnemyClass::Raider).priorities;
    Rng rng{7};
    for (int i = 0; i < 20; ++i)
        assert(in_target_group(ship, *pick_target(ship, raider, 0.0f, rng), TargetGroup::Engines));
    ground(ship);
    for (int i = 0; i < 20; ++i)
        assert(*pick_target(ship, raider, 0.0f, rng) == kGun);
    ship.destroy_building(kGun);
    for (int i = 0; i < 20; ++i)
        assert(*pick_target(ship, raider, 0.0f, rng) == bridge_of(ship));
    // Now and then, anything at all.
    std::set<std::pair<int, int>> picked;
    for (int i = 0; i < 200; ++i) {
        const GridPos p = *pick_target(ship, raider, 0.5f, rng);
        picked.insert({p.x, p.y});
    }
    assert(picked.contains({kShield.x, kShield.y}));
}

// Weapons are found, never made: none has a recipe, and finding one
// unlocks its own ammunition, once. A new game has found the burst laser.
void test_found_weapons() {
    Progress progress;
    for (int i = 0; i < kItemKindCount; ++i)
        if (const ItemKind kind = static_cast<ItemKind>(i); is_weapon(kind)) {
            assert(!recipe_for(kind) && !milestone_unlocking(kind) && !progress.unlocked(kind));
            // Its ammunition comes with it, or with a milestone (focus
            // crystals, which the lasers share).
            const ItemKind ammo = weapon_spec(kind).ammo;
            assert(recipe_for(ammo));
            assert(weapon_unlocking(ammo) == kind || milestone_unlocking(ammo));
        }
    assert(!progress.unlocked(ItemKind::Missile));
    assert(progress.add_found(ItemKind::MissileLauncher));
    assert(!progress.add_found(ItemKind::MissileLauncher));
    assert(!progress.add_found(ItemKind::Missile));  // no weapon
    assert(progress.unlocked(ItemKind::MissileLauncher) && progress.unlocked(ItemKind::Missile));
    assert(!progress.unlocked(ItemKind::HeavyLaser) && !progress.unlocked(ItemKind::IonCell));
    assert(milestone_unlocking(ItemKind::FocusCrystal) && milestone_unlocking(ItemKind::ShieldProjector));

    World world;
    assert(world.get_fleet().get_progress().found_weapons() == std::vector<ItemKind>{ItemKind::BurstLaser});
    assert(world.flagship().inventory().count(ItemKind::BurstLaser) == 1);
}

// The flagship, armed, beats a Scout: its salvage lands in the bridge, the
// sector is cleared and the fight is over, and buildings mend again.
void test_beating_an_enemy() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    Ship& ship = flagship.ship;
    place_weapon(ship, ItemKind::BurstLaser, kGun, 5, 10);
    place_weapon(ship, ItemKind::BurstLaser, kGun2, 5, 10);
    assert(world.spawn_enemy(flagship.id, EnemyClass::Scout));
    world.advance_tick();
    assert(world.engagement_in(0) && world.get_map().at(0).hostile());
    std::vector<Salvage> salvage;
    const int cells = ship.inventory().count(ItemKind::FuelCell);
    for (int i = 0; i < 300 * static_cast<int>(kTicksPerSecond) && salvage.empty(); ++i) {
        top_up(ship);
        world.advance_tick();
        salvage = world.take_salvage();
    }
    assert(salvage.size() == 1 && salvage[0].enemy == EnemyClass::Scout && salvage[0].ship == flagship.id);
    assert(!world.engagement_in(0));
    assert(world.get_map().at(0).cleared() && !world.get_map().at(0).hostile());
    assert(ship.inventory().count(ItemKind::FuelCell) > cells);
    for (const ItemKind w : salvage[0].found)
        assert(is_weapon(w) && world.get_fleet().get_progress().has_found(w));
    // Mending again.
    ship.get_bridge().take_damage(500);
    const int hp = ship.get_bridge().get_hp();
    for (int i = 0; i < 2 * static_cast<int>(kTicksPerSecond); ++i)
        world.advance_tick();
    assert(ship.get_bridge().get_hp() > hp);
}

// A beaten Dreadnought always carries a weapon in its hold, one the fleet
// hasn't found, with ammunition for it: found when it's salvaged.
void test_salvaged_weapon() {
    World world;
    const int id = world.get_fleet().flagship().id;
    Progress& progress = world.get_fleet().get_progress();
    for (const ItemKind found : {ItemKind::BeamEmitter, ItemKind::FlakCannon})
        progress.add_found(found);
    const int ion_blasters = world.flagship().inventory().count(ItemKind::IonBlaster);
    world.spawn_enemy(id, EnemyClass::Dreadnought);
    world.advance_tick();
    assert(world.defeat_enemy(id));
    world.advance_tick();
    const std::vector<Salvage> salvage = world.take_salvage();
    assert(salvage.size() == 1);
    assert(std::find(salvage[0].found.begin(), salvage[0].found.end(), ItemKind::IonBlaster) != salvage[0].found.end());
    assert(world.flagship().inventory().count(ItemKind::IonBlaster) == ion_blasters + 1);
    assert(progress.has_found(ItemKind::IonBlaster) && progress.unlocked(ItemKind::IonCell));
    assert(world.flagship().inventory().count(ItemKind::IonCell) > 0);
    // Its own weapons' ammunition comes too.
    assert(world.flagship().inventory().count(ItemKind::IridiumLens) > 0);
}

// No damage mends while the fight is on.
void test_no_repair_in_a_fight() {
    World world;
    const int id = world.get_fleet().flagship().id;
    world.spawn_enemy(id, EnemyClass::Scout);
    world.advance_tick();
    Bridge& bridge = world.flagship().get_bridge();
    bridge.take_damage(100);
    for (int i = 0; i < 3 * static_cast<int>(kTicksPerSecond); ++i)
        world.advance_tick();
    assert(bridge.get_hp() <= max_hp_of(std::nullopt) - 100);
}

// Jumping away ends the fight: the enemy stays, to fight afresh.
void test_leaving() {
    World world;
    FleetShip& flagship = world.get_fleet().flagship();
    world.spawn_enemy(flagship.id, EnemyClass::Raider);
    world.advance_tick();
    assert(world.engagement_in(0));
    for (const GridPos p : flagship.ship.placement_order())
        if (dynamic_cast<const Thruster*>(flagship.ship.building_anchored_at(p)))
            feed(flagship.ship, p, ItemKind::FuelCell, kThrusterCharge);
    const int to = world.get_map().at(0).neighbors.front();
    assert(world.jump(flagship.id, to) == JumpResult::Ok);
    world.advance_tick();
    assert(!world.engagement_in(0));
    assert(world.get_map().at(0).enemy == EnemyClass::Raider);
}

// With no weapons, a Gunship wears the flagship down: the fleet is lost.
void test_losing() {
    World world;
    world.spawn_enemy(world.get_fleet().flagship().id, EnemyClass::Gunship);
    for (int i = 0; i < 30 * 60 * static_cast<int>(kTicksPerSecond) && !world.fleet_lost(); ++i)
        world.advance_tick();
    assert(world.fleet_lost());
}

// The same seed fights the same fight.
void test_deterministic() {
    const auto run = [] {
        World world{42};
        FleetShip& flagship = world.get_fleet().flagship();
        place_weapon(flagship.ship, ItemKind::BurstLaser, kGun, 5, 10);
        place_weapon(flagship.ship, ItemKind::FlakCannon, kGun3, 5, 10);
        world.spawn_enemy(flagship.id, EnemyClass::Raider);
        for (int i = 0; i < 40 * static_cast<int>(kTicksPerSecond); ++i)
            world.advance_tick();
        const Engagement* e = world.engagement_in(0);
        return std::pair{e ? e->ship.get_bridge().get_hp() : -1, world.flagship().get_bridge().get_hp()};
    };
    const auto a = run();
    assert(a.first < enemy_design(EnemyClass::Raider).hull);
    assert(a == run());
}

}  // namespace

int main() {
    test_ids();
    test_enemies_built();
    test_weapon_feeding();
    test_needs_power();
    test_no_fight_no_charge();
    test_shields_stop_lasers();
    test_shields_one_at_a_time();
    test_missiles_and_ion();
    test_ion_stuns_weapons();
    test_beam();
    test_evasion();
    test_priorities();
    test_found_weapons();
    test_ports();
    test_beating_an_enemy();
    test_salvaged_weapon();
    test_no_repair_in_a_fight();
    test_leaving();
    test_losing();
    test_deterministic();
    std::puts("test_combat: all passed");
    return 0;
}
