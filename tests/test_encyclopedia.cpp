// test_encyclopedia.cpp — headless tests for the encyclopedia's
// descriptions and supply-chain lookups: every item is described, and each
// lookup agrees with the tables it searches.
#include "sim/building.h"
#include "sim/deliveries.h"
#include "sim/encyclopedia.h"
#include "sim/ship_layout.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

ItemKind item(int i) { return static_cast<ItemKind>(i); }

template <typename T>
bool contains(const std::vector<T>& v, const T& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

void test_every_item_described() {
    for (int i = 0; i < kItemKindCount; ++i)
        assert(std::strlen(item_description(item(i))) > 20);
}

// Every item is made by a recipe, gathered, found or comes with a layout:
// the encyclopedia always has a "made by" to show.
void test_every_item_has_a_source() {
    for (int i = 0; i < kItemKindCount; ++i) {
        const ItemKind kind = item(i);
        const bool made = !recipes_making(kind).empty();
        const bool gathered = !collectors_gathering(kind).empty();
        const bool found = is_weapon(kind) && !enemies_carrying(kind).empty();
        assert(made || gathered || found || is_layout_part(kind));
        if (made)
            assert(recipes_making(kind).front() == recipe_for(kind));
    }
    // The burst laser isn't in anyone's hold, but scouts mount one.
    assert(contains(enemies_carrying(ItemKind::BurstLaser), EnemyClass::Scout));
    assert(contains(enemies_carrying(ItemKind::BeamEmitter), EnemyClass::Dreadnought));
}

void test_recipes_both_ways() {
    for (const Recipe& r : all_recipes()) {
        for (const ItemStack& s : r.inputs)
            assert(contains(recipes_using(s.kind), &r));
        for (const ItemStack& s : r.outputs)
            assert(contains(recipes_making(s.kind), &r));
        // Every recipe runs somewhere: a machine, or the bridge.
        assert(!machines_for(r.category).empty() || r.hand_craftable);
    }
    assert(contains(recipes_using(ItemKind::IronPlate), recipe_for(ItemKind::Gear)));
    assert(recipes_using(ItemKind::IridiumLens).empty());
}

void test_machines_and_collectors() {
    const std::vector<ItemKind> smelters = machines_for(CraftCategory::Smelting);
    assert(contains(smelters, ItemKind::SolarFurnace) && contains(smelters, ItemKind::Refinery));
    assert(!contains(smelters, ItemKind::FabricatorMk1));
    assert(machines_for(CraftCategory::Refining) == std::vector<ItemKind>{ItemKind::Refinery});
    assert(machines_for(CraftCategory::Assembly).size() == 3);

    for (const ItemKind raw : {ItemKind::IronOre, ItemKind::CopperOre, ItemKind::Silica, ItemKind::Carbon})
        assert(collectors_gathering(raw).size() == 2);
    assert(collectors_gathering(ItemKind::IridiumOre) == std::vector<ItemKind>{ItemKind::TractorArray});
    assert(collectors_gathering(ItemKind::IronPlate).empty());
}

void test_ammunition() {
    const std::vector<ItemKind> lasers = weapons_firing(ItemKind::FocusCrystal);
    assert(lasers.size() == 2 && contains(lasers, ItemKind::BurstLaser) && contains(lasers, ItemKind::HeavyLaser));
    for (int i = 0; i < kItemKindCount; ++i)
        if (is_weapon(item(i)))
            assert(contains(weapons_firing(weapon_spec(item(i)).ammo), item(i)));
    assert(weapons_firing(ItemKind::Gear).empty());
}

void test_deliveries_layouts_and_techs() {
    for (std::size_t d = 0; d < all_deliveries().size(); ++d)
        for (const ItemStack& s : all_deliveries()[d].wants)
            assert(contains(deliveries_wanting(s.kind), d));
    assert(deliveries_wanting(ItemKind::TransportBelt).empty());

    assert(layouts_costing(ItemKind::ShipStarterPack).size() == all_layouts().size());
    for (std::size_t l = 0; l < all_layouts().size(); ++l)
        for (const ItemStack& s : all_layouts()[l].cost)
            assert(contains(layouts_costing(s.kind), l));

    for (std::size_t t = 0; t < all_techs().size(); ++t)
        for (const std::size_t before : tech_prerequisites(t))
            assert(contains(techs_after(before), t));
    assert(techs_after(tech_index("Carrier hull")).empty());
}

}  // namespace

int main() {
    test_every_item_described();
    test_every_item_has_a_source();
    test_recipes_both_ways();
    test_machines_and_collectors();
    test_ammunition();
    test_deliveries_layouts_and_techs();
    std::puts("test_encyclopedia: all passed");
    return 0;
}
