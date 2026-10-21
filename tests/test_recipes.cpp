// test_recipes.cpp — headless tests for the item catalogue and recipe table:
// every item is named and reachable from raw resources, every building has
// a cost and a recipe some building can run, the bootstrap needs nothing
// beyond the solar furnace and hand-crafting, and the bridge hand-crafts.
#include "sim/building.h"
#include "sim/recipes.h"
#include "sim/ship.h"
#include "sim/world.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

ItemKind item(int i) { return static_cast<ItemKind>(i); }

void test_names_and_categories() {
    for (int i = 0; i < kItemKindCount; ++i)
        assert(std::strcmp(item_name(item(i)), "?") != 0);
    for (int i = 0; i < kCraftCategoryCount; ++i)
        assert(std::strcmp(craft_category_name(static_cast<CraftCategory>(i)), "?") != 0);
    for (const Recipe& r : all_recipes())
        assert(std::strcmp(recipe_name(r), "?") != 0);

    assert(item_category(ItemKind::Hull) == ItemCategory::Placeable);
    assert(item_category(ItemKind::IonThruster) == ItemCategory::Placeable);
    assert(item_category(ItemKind::IronOre) == ItemCategory::RawResource);
    assert(item_category(ItemKind::IridiumOre) == ItemCategory::RawResource);
    assert(item_category(ItemKind::IronPlate) == ItemCategory::Material);
    assert(item_category(ItemKind::Plastic) == ItemCategory::Material);
    assert(item_category(ItemKind::Gear) == ItemCategory::Component);
    assert(item_category(ItemKind::Processor) == ItemCategory::Component);
}

// Every item can be made, starting from what the collectors gather (raw
// resources).
void test_everything_reachable() {
    std::array<bool, kItemKindCount> have{};
    for (int i = 0; i < kItemKindCount; ++i)
        have[static_cast<std::size_t>(i)] = item_category(item(i)) == ItemCategory::RawResource;

    for (bool changed = true; changed;) {
        changed = false;
        for (const Recipe& r : all_recipes()) {
            bool ready = true;
            for (const ItemStack& s : r.inputs)
                ready = ready && have[static_cast<std::size_t>(s.kind)];
            if (!ready)
                continue;
            for (const ItemStack& s : r.outputs)
                if (!have[static_cast<std::size_t>(s.kind)])
                    have[static_cast<std::size_t>(s.kind)] = changed = true;
        }
    }
    for (int i = 0; i < kItemKindCount; ++i) {
        if (!have[static_cast<std::size_t>(i)])
            std::printf("unreachable: %s\n", item_name(item(i)));
        assert(have[static_cast<std::size_t>(i)]);
    }
}

// Every placeable item has a cost (an Assembly recipe), and every recipe
// has a building that can run it.
void test_costs_and_machines() {
    for (int i = 0; i < kItemKindCount; ++i) {
        if (!is_placeable(item(i)))
            continue;
        const Recipe* r = recipe_for(item(i));
        assert(r);
        assert(r->category == CraftCategory::Assembly);
        assert(!r->inputs.empty());
    }
    for (const Recipe& r : all_recipes()) {
        bool runnable = false;
        for (int i = 0; i < kItemKindCount; ++i)
            runnable = runnable || machine_crafts(item(i), r.category);
        assert(runnable);
        assert(r.ticks > 0);
        assert(!r.hand_craftable || r.category != CraftCategory::Refining);
        // Early recipes are simple: one product each.
        if (r.hand_craftable || r.category == CraftCategory::Smelting)
            assert(r.outputs.size() == 1);
    }
    // Spot checks against the design.
    assert(recipe_for(ItemKind::Gear)->inputs.size() == 1);
    assert(recipe_for(ItemKind::Gear)->inputs[0].kind == ItemKind::IronPlate);
    assert(recipe_for(ItemKind::Gear)->inputs[0].count == 2);
    assert(recipe_for(ItemKind::IronOre) == nullptr);     // only gathered
    assert(recipe_for(ItemKind::IridiumOre) == nullptr);
    // The first plates come by hand, before any furnace runs.
    assert(recipe_for(ItemKind::IronPlate)->hand_craftable);
    assert(recipe_for(ItemKind::CopperPlate)->hand_craftable);
}

bool bootstrappable(ItemKind kind, int depth = 0);

bool all_bootstrappable(const Recipe& r, int depth) {
    for (const ItemStack& s : r.inputs)
        if (!bootstrappable(s.kind, depth + 1))
            return false;
    return true;
}

// Obtainable with only collectors, a solar furnace and hand-crafting.
bool bootstrappable(ItemKind kind, int depth) {
    assert(depth < 32);  // no recipe cycles
    const ItemCategory c = item_category(kind);
    if (c == ItemCategory::RawResource)
        return true;
    const Recipe* r = recipe_for(kind);
    if (!r)
        return false;
    if (r->category == CraftCategory::Smelting || r->hand_craftable)
        return all_bootstrappable(*r, depth);
    return false;
}

// The design's bootstrap rule: tier-0 (hand-craftable) buildings never need
// anything a tier-1+ building makes, and the machines the bootstrap needs
// are among them.
void test_bootstrap() {
    for (int i = 0; i < kItemKindCount; ++i) {
        const Recipe* r = recipe_for(item(i));
        if (!is_placeable(item(i)) || !r->hand_craftable)
            continue;
        if (!all_bootstrappable(*r, 0))
            std::printf("not bootstrappable: %s\n", item_name(item(i)));
        assert(all_bootstrappable(*r, 0));
    }
    for (const ItemKind kind :
         {ItemKind::AsteroidCollector, ItemKind::SolarFurnace, ItemKind::FabricatorMk1})
        assert(recipe_for(kind)->hand_craftable);
    // The solar furnace is built from raw materials.
    for (const ItemStack& s : recipe_for(ItemKind::SolarFurnace)->inputs)
        assert(item_category(s.kind) == ItemCategory::RawResource);
}

void test_machines() {
    const auto refinery = make_building(ItemKind::Refinery, Direction::North);
    const auto solar = make_building(ItemKind::SolarFurnace, Direction::North);
    const auto* refinery_m = dynamic_cast<const Machine*>(refinery.get());
    const auto* solar_m = dynamic_cast<const Machine*>(solar.get());
    assert(refinery_m && solar_m);
    const Recipe& silicon = *recipe_for(ItemKind::Silicon);
    const Recipe& plastic = *recipe_for(ItemKind::Plastic);
    const Recipe& iron = *recipe_for(ItemKind::IronPlate);
    assert(refinery_m->can_run(silicon) && refinery_m->can_run(plastic) && refinery_m->can_run(iron));
    assert(!solar_m->can_run(silicon) && !solar_m->can_run(plastic) && solar_m->can_run(iron));
    assert(refinery_m->crafting_speed() == 2 && solar_m->crafting_speed() == 1);
    assert(dynamic_cast<const Machine*>(make_building(ItemKind::Junction, Direction::North).get()) == nullptr);

    // Every placeable building kind can be made.
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = item(i);
        if (!is_placeable(kind) || kind == ItemKind::Hull || kind == ItemKind::ReinforcedHull)
            continue;
        const auto b = make_building(kind, Direction::East);
        assert(b->item_kind() == kind);
        assert(b->footprint() == footprint_of(kind, Direction::East));
        assert(b->rotatable() == is_rotatable(kind));
    }
}

void test_hand_crafting() {
    World world;
    Bridge& bridge = world.flagship().get_bridge();
    Inventory& inv = bridge.get_inventory();
    const Recipe& gear = *recipe_for(ItemKind::Gear);
    const int plates = inv.count(ItemKind::IronPlate);
    const int gears = inv.count(ItemKind::Gear);

    assert(bridge.queue_craft(gear));
    assert(bridge.queue_craft(gear));
    // Inputs leave at once; outputs arrive when each job finishes, in order.
    assert(inv.count(ItemKind::IronPlate) == plates - 4);
    assert(bridge.craft_queue().size() == 2);
    for (Tick t = 1; t < gear.ticks; ++t)
        world.advance_tick();
    assert(inv.count(ItemKind::Gear) == gears);
    world.advance_tick();
    assert(inv.count(ItemKind::Gear) == gears + 1);
    assert(bridge.craft_queue().size() == 1);

    // Cancelling refunds.
    assert(bridge.cancel_craft(0));
    assert(!bridge.cancel_craft(0));
    assert(inv.count(ItemKind::IronPlate) == plates - 2);
    assert(bridge.craft_queue().empty());

    // Not hand-craftable, or not enough inputs: refused, nothing taken.
    const Recipe& router = *recipe_for(ItemKind::Router);
    assert(!router.hand_craftable);
    assert(!bridge.can_hand_craft(router));
    assert(!bridge.queue_craft(router));
    const Recipe& processor = *recipe_for(ItemKind::Processor);
    assert(processor.hand_craftable && !bridge.can_hand_craft(processor));
    assert(!bridge.queue_craft(processor));
    assert(bridge.craft_queue().empty());

    // A crafted building can be placed.
    const Recipe& fabricator = *recipe_for(ItemKind::FabricatorMk1);
    inv.add({{ItemKind::IronPlate, 8}, {ItemKind::Gear, 4}, {ItemKind::Circuit, 3}});
    const int fabricators = inv.count(ItemKind::FabricatorMk1);
    assert(bridge.queue_craft(fabricator));
    for (Tick t = 0; t < fabricator.ticks; ++t)
        world.advance_tick();
    assert(inv.count(ItemKind::FabricatorMk1) == fabricators + 1);
}

void test_inventory_stacks() {
    Inventory inv;
    inv.add({{ItemKind::Gear, 2}, {ItemKind::Glass, 1}});
    assert(inv.has({{ItemKind::Gear, 1}, {ItemKind::Gear, 1}}));
    assert(!inv.has({{ItemKind::Gear, 2}, {ItemKind::Gear, 1}}));
    assert(!inv.take({{ItemKind::Gear, 1}, {ItemKind::Glass, 2}}));
    assert(inv.count(ItemKind::Gear) == 2 && inv.count(ItemKind::Glass) == 1);
    assert(inv.take({{ItemKind::Gear, 2}, {ItemKind::Glass, 1}}));
    assert(inv.count(ItemKind::Gear) == 0 && inv.count(ItemKind::Glass) == 0);
    assert(!inv.take(ItemKind::Gear));
}

}  // namespace

int main() {
    test_names_and_categories();
    test_everything_reachable();
    test_costs_and_machines();
    test_bootstrap();
    test_machines();
    test_hand_crafting();
    test_inventory_stacks();
    std::puts("test_recipes: all passed");
    return 0;
}
