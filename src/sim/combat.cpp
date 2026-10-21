// combat.cpp — enemy specs, waves and the fight on a ship's grid (see
// combat.h).
#include "sim/combat.h"

#include "sim/building.h"

#include <algorithm>
#include <cassert>
#include <cmath>

const EnemySpec& enemy_spec(EnemyKind kind) {
    constexpr float kPerSecond = 1.0f / static_cast<float>(kTicksPerSecond);
    static const EnemySpec specs[kEnemyKindCount] = {
        {.name = "Drone",
         .hp = 30,
         .speed = 3.0f * kPerSecond,
         .range = 1.5f,
         .damage = 6,
         .interval = kTicksPerSecond,
         .cost = 1,
         .min_threat = 1},
        {.name = "Gunship",
         .hp = 160,
         .speed = 1.5f * kPerSecond,
         .range = 7.0f,
         .damage = 20,
         .interval = 2 * kTicksPerSecond,
         .cost = 4,
         .min_threat = 2},
        {.name = "Dreadnought",
         .hp = 1200,
         .speed = 0.6f * kPerSecond,
         .range = 12.0f,
         .damage = 80,
         .interval = 3 * kTicksPerSecond,
         .cost = 15,
         .min_threat = 4},
    };
    return specs[static_cast<std::size_t>(kind)];
}

// No default case, so a new EnemyKind without an id is a -Wswitch error.
const char* enemy_id(EnemyKind kind) {
    switch (kind) {
    case EnemyKind::Drone:       return "Drone";
    case EnemyKind::Gunship:     return "Gunship";
    case EnemyKind::Dreadnought: return "Dreadnought";
    case EnemyKind::Count:       break;
    }
    return "?";
}

std::optional<EnemyKind> enemy_from_id(std::string_view id) {
    for (int i = 0; i < kEnemyKindCount; ++i)
        if (id == enemy_id(static_cast<EnemyKind>(i)))
            return static_cast<EnemyKind>(i);
    return std::nullopt;
}

namespace {

// How far (x, y) is from the box `min`..`min + size`: 0 inside it.
float distance_to_box(float x, float y, GridPos min, GridSize size) {
    const float dx = std::max({static_cast<float>(min.x) - x, 0.0f, x - static_cast<float>(min.x + size.w)});
    const float dy = std::max({static_cast<float>(min.y) - y, 0.0f, y - static_cast<float>(min.y + size.h)});
    return std::hypot(dx, dy);
}

// The middle of the building at `p`, `size` big.
void middle(GridPos p, GridSize size, float& x, float& y) {
    x = static_cast<float>(p.x) + static_cast<float>(size.w) * 0.5f;
    y = static_cast<float>(p.y) + static_cast<float>(size.h) * 0.5f;
}

// The top-left cell of the building nearest (x, y), if there's any.
std::optional<GridPos> nearest_building(const Ship& ship, float x, float y) {
    std::optional<GridPos> best;
    float best_d = 0.0f;
    for (const GridPos p : ship.placement_order()) {
        const float d = distance_to_box(x, y, p, ship.building_anchored_at(p)->footprint());
        if (!best || d < best_d) {
            best = p;
            best_d = d;
        }
    }
    return best;
}

void move_and_shoot(Ship& ship, Enemy& e, Tick now) {
    const EnemySpec& spec = enemy_spec(e.kind);
    if (e.cooldown > 0)
        --e.cooldown;
    if (!e.target || !ship.building_anchored_at(*e.target))
        e.target = nearest_building(ship, e.x, e.y);
    if (!e.target)
        return;
    Building& target = *ship.building_anchored_at(*e.target);
    const GridSize size = target.footprint();
    float tx = 0.0f;
    float ty = 0.0f;
    middle(*e.target, size, tx, ty);
    if (distance_to_box(e.x, e.y, *e.target, size) > spec.range) {
        const float d = std::hypot(tx - e.x, ty - e.y);
        const float step = std::min(spec.speed, d);
        e.x += (tx - e.x) / d * step;
        e.y += (ty - e.y) / d * step;
        return;
    }
    if (e.cooldown > 0)
        return;
    e.cooldown = spec.interval;
    e.fired_at = now;
    e.aim_x = tx;
    e.aim_y = ty;
    target.hit_at = now;
    if (target.take_damage(spec.damage) && &target != &ship.get_bridge()) {
        ship.destroy_building(*e.target);
        e.target.reset();
    }
}

}  // namespace

void spawn_wave(const Ship& ship, Battle& battle, int threat, int wave, Rng& rng) {
    // In from one side, on a ring just outside the hull.
    const auto [corner, extent] = ship.hull_bounds();
    const float cx = static_cast<float>(corner.x) + static_cast<float>(extent.w) * 0.5f;
    const float cy = static_cast<float>(corner.y) + static_cast<float>(extent.h) * 0.5f;
    const float radius =
        std::hypot(static_cast<float>(extent.w), static_cast<float>(extent.h)) * 0.5f + kSpawnMargin;
    const float side = rng.unit() * 6.2831853f;

    int strength = 3 + 3 * threat + 2 * wave;
    while (strength > 0) {
        // The heaviest kind the sector sends and the wave can afford, now
        // and then; drones otherwise.
        EnemyKind kind = EnemyKind::Drone;
        for (const EnemyKind heavy : {EnemyKind::Dreadnought, EnemyKind::Gunship}) {
            const EnemySpec& spec = enemy_spec(heavy);
            if (threat >= spec.min_threat && strength >= spec.cost && rng.chance(0.4f)) {
                kind = heavy;
                break;
            }
        }
        const EnemySpec& spec = enemy_spec(kind);
        const float angle = side + (rng.unit() - 0.5f) * 1.2f;
        const float r = radius + rng.unit() * 4.0f;
        Enemy e;
        e.kind = kind;
        e.x = cx + std::cos(angle) * r;
        e.y = cy + std::sin(angle) * r;
        e.hp = spec.hp;
        e.cooldown = spec.interval;
        battle.enemies.push_back(e);
        strength -= spec.cost;
    }
}

void combat_tick(Ship& ship, Battle& battle, Sector& sector, Rng& rng, Tick now) {
    // The waves: the first a while after arriving, then one every so often.
    if (sector.hostile() && !battle.next_wave)
        battle.next_wave = now + kFirstWaveDelay;
    if (battle.next_wave && now >= *battle.next_wave) {
        if (sector.hostile()) {
            const int wave = waves_for_threat(sector.threat) - sector.waves_left;
            --sector.waves_left;
            spawn_wave(ship, battle, sector.threat, std::max(wave, 0), rng);
        }
        battle.next_wave.reset();
        if (sector.hostile())
            battle.next_wave = now + kWaveInterval;
    }
    if (battle.enemies.empty())
        return;

    for (Enemy& e : battle.enemies)
        if (!ship.bridge_destroyed())
            move_and_shoot(ship, e, now);

    // Turrets that are ready shoot the nearest enemy in range.
    for (const GridPos p : ship.placement_order()) {
        auto* turret = dynamic_cast<Turret*>(ship.building_anchored_at(p));
        if (!turret || !turret->ready())
            continue;
        float x = 0.0f;
        float y = 0.0f;
        middle(p, turret->footprint(), x, y);
        Enemy* nearest = nullptr;
        float nearest_d = 0.0f;
        for (Enemy& e : battle.enemies) {
            const float d = std::hypot(e.x - x, e.y - y);
            if (e.hp > 0 && d <= turret->get_spec().range && (!nearest || d < nearest_d)) {
                nearest = &e;
                nearest_d = d;
            }
        }
        if (!nearest)
            continue;
        turret->fire();
        turret->fired_at = now;
        turret->aim_x = nearest->x;
        turret->aim_y = nearest->y;
        nearest->hp -= turret->get_spec().damage;
    }
    std::erase_if(battle.enemies, [](const Enemy& e) { return e.hp <= 0; });
}
