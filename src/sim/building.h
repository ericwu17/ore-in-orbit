// building.h — everything that stands on a hull tile, as a class hierarchy,
// plus the bridge's Inventory.
// Pure simulation layer: no imgui/SDL/OpenGL headers (see game_types.h).
//
//   Building (abstract)
//   ├── Bridge               the ship's command tile; owns the inventory
//   ├── Collector (abstract) gathers resources from space
//   │   ├── HydrogenCollector
//   │   └── AsteroidCollector
//   ├── Pipe                 carries fluid from collectors to tanks
//   └── TransportBelt        moves collected items; oriented
//
// A building covers a footprint of w x h cells. It doesn't know where it is:
// the Ship stores it at its footprint's top-left cell. Each class states its footprint as `kSize` (1x1 unless it says
// otherwise); footprint_of() looks it up by item kind before one exists.
// None of them does anything per tick yet: tick() is the hook resource flow
// will plug into.
#pragma once

#include "sim/game_types.h"

#include <array>
#include <memory>
#include <optional>

// Counts of each placeable item, held by the bridge.
class Inventory {
private:
    std::array<int, kItemKindCount> counts{};

public:
    int count(ItemKind kind) const { return counts[static_cast<std::size_t>(kind)]; }
    void add(ItemKind kind, int n = 1);
    // Removes one; false (and no change) if there are none.
    bool take(ItemKind kind);
};

class Building {
private:
    GridSize size;

public:
    static constexpr GridSize kSize{1, 1};

    explicit Building(GridSize s) : size(s) {}
    virtual ~Building() = default;
    Building(const Building&) = delete;
    Building& operator=(const Building&) = delete;

    GridSize footprint() const { return size; }

    // The item that placed this building, refunded when it is removed;
    // nullopt for buildings that never came from the inventory (the bridge).
    virtual std::optional<ItemKind> item_kind() const = 0;
    // One simulation step. Nothing does anything yet.
    virtual void tick() {}
    virtual bool removable() const { return true; }
    virtual bool rotatable() const { return false; }
    virtual Direction facing() const { return Direction::North; }
};

class Bridge : public Building {
private:
    Inventory inventory;

public:
    Bridge() : Building(kSize) {}

    std::optional<ItemKind> item_kind() const override { return std::nullopt; }
    bool removable() const override { return false; }

    Inventory& get_inventory() { return inventory; }
    const Inventory& get_inventory() const { return inventory; }
};

// Collectors are 2x2.
class Collector : public Building {
public:
    static constexpr GridSize kSize{2, 2};

    Collector() : Building(kSize) {}
};

class HydrogenCollector : public Collector {
public:
    using Collector::Collector;
    std::optional<ItemKind> item_kind() const override { return ItemKind::HydrogenCollector; }
};

class AsteroidCollector : public Collector {
public:
    using Collector::Collector;
    std::optional<ItemKind> item_kind() const override { return ItemKind::AsteroidCollector; }
};

class Pipe : public Building {
public:
    Pipe() : Building(kSize) {}
    std::optional<ItemKind> item_kind() const override { return ItemKind::Pipe; }
};

class TransportBelt : public Building {
private:
    Direction dir;

public:
    explicit TransportBelt(Direction d) : Building(kSize), dir(d) {}

    std::optional<ItemKind> item_kind() const override { return ItemKind::TransportBelt; }
    bool rotatable() const override { return true; }
    Direction facing() const override { return dir; }
};

// The footprint a building of this kind will have (1x1 for Hull).
GridSize footprint_of(ItemKind kind);

// A new building of the given kind (anything but Hull, which is not a
// building). `facing` only matters for rotatable kinds.
std::unique_ptr<Building> make_building(ItemKind kind, Direction facing);
