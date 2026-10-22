// recipes.cpp — the recipe table (see recipes.h).
#include "sim/recipes.h"

#include <array>

const char* craft_category_name(CraftCategory category) {
    switch (category) {
    case CraftCategory::Smelting:     return "Smelting";
    case CraftCategory::Refining:     return "Refining";
    case CraftCategory::Assembly:     return "Assembly";
    case CraftCategory::Count:        break;
    }
    return "?";
}

const char* recipe_name(const Recipe& recipe) {
    if (recipe.name)
        return recipe.name;
    if (!recipe.outputs.empty())
        return item_name(recipe.outputs.front().kind);
    return "?";
}

namespace {

// Builds one table row: R(category, seconds).in(...).out(...)...
class R {
private:
    Recipe recipe;

public:
    R(CraftCategory category, double seconds) {
        recipe.category = category;
        recipe.ticks = static_cast<Tick>(seconds * static_cast<double>(kTicksPerSecond));
    }
    R& named(const char* name) { recipe.name = name; return *this; }
    R& in(ItemKind kind, int count = 1) { recipe.inputs.push_back({kind, count}); return *this; }
    R& out(ItemKind kind, int count = 1) { recipe.outputs.push_back({kind, count}); return *this; }
    R& hand() { recipe.hand_craftable = true; return *this; }
    operator Recipe() const { return recipe; }
};

std::vector<Recipe> make_recipes() {
    using enum ItemKind;
    constexpr CraftCategory Smelt = CraftCategory::Smelting;
    constexpr CraftCategory Refine = CraftCategory::Refining;
    constexpr CraftCategory Assemble = CraftCategory::Assembly;

    return {
        // Smelting (the first two by hand too, so the bridge has work from
        // the start).
        R(Smelt, 1).in(IronOre).out(IronPlate).hand(),
        R(Smelt, 1).in(CopperOre).out(CopperPlate).hand(),
        R(Smelt, 1).in(Silica, 2).out(Glass),
        R(Smelt, 4).in(IronPlate, 3).in(Carbon).out(Steel),

        // Refining.
        R(Refine, 1).in(Carbon, 2).out(Plastic),
        R(Refine, 2).in(Silica).in(Carbon).out(Silicon),
        R(Refine, 4).in(IridiumOre, 2).out(IridiumPlate),
        R(Refine, 2).in(Carbon, 4).out(FuelCell),

        // Components (all hand-craftable).
        R(Assemble, 0.5).in(IronPlate, 2).out(Gear).hand(),
        R(Assemble, 0.5).in(IronPlate).in(CopperPlate).out(Circuit).hand(),
        R(Assemble, 2).in(Steel, 2).in(Gear, 2).out(StructuralFrame).hand(),
        R(Assemble, 3).in(Circuit, 2).in(Plastic).in(Silicon).out(AdvancedCircuit).hand(),
        R(Assemble, 5).in(AdvancedCircuit, 2).in(IridiumPlate).out(Processor).hand(),

        // Tier 0 buildings: bootstrap, hand-craftable.
        R(Assemble, 0.5).in(Gear).in(IronPlate).out(TransportBelt, 2).hand(),
        R(Assemble, 3).in(IronPlate, 5).in(Gear, 3).in(Circuit, 2).out(AsteroidCollector).hand(),
        R(Assemble, 2).in(Silica, 8).in(IronOre, 4).out(SolarFurnace).hand(),
        R(Assemble, 0.5).in(IronPlate, 8).out(CargoHold).hand(),
        R(Assemble, 1).in(IronPlate, 2).in(Circuit).in(TransportBelt, 2).out(FilterSorter).hand(),
        R(Assemble, 1).in(IronPlate, 2).in(Circuit).in(TransportBelt, 2).out(ReverseSorter).hand(),
        R(Assemble, 0.5).in(IronPlate, 2).in(TransportBelt, 2).out(Junction).hand(),
        R(Assemble, 3).in(IronPlate, 2).in(Glass, 4).in(Circuit, 2).out(SolarPanel).hand(),
        R(Assemble, 3).in(IronPlate, 8).in(Gear, 4).in(Circuit, 3).out(FabricatorMk1).hand(),

        // Tier 1: logistics and power.
        R(Assemble, 1).in(Circuit, 2).in(IronPlate, 3).in(TransportBelt, 2).out(Router),
        R(Assemble, 1).in(IronPlate, 10).in(TransportBelt, 5).out(BeltTunnel, 2),
        R(Assemble, 3).in(Steel, 4).in(Circuit, 4).out(BatteryBank),

        // Tier 2: industry.
        R(Assemble, 5).in(Steel, 10).in(Circuit, 10).in(Glass, 10).out(Refinery),
        R(Assemble, 8).in(FabricatorMk1).in(StructuralFrame, 2).in(AdvancedCircuit, 3).out(FabricatorMk2),
        R(Assemble, 1).in(TransportBelt).in(Gear, 2).in(Steel).out(ExpressBelt),
        R(Assemble, 10).in(StructuralFrame, 5).in(SolarPanel, 10).out(SolarArray),

        // Tier 3: endgame.
        R(Assemble, 15)
            .in(AsteroidCollector).in(StructuralFrame, 10).in(AdvancedCircuit, 10)
            .out(TractorArray),
        R(Assemble, 8).in(StructuralFrame, 5).in(AdvancedCircuit, 5).in(Glass, 4).out(SurveyScanner),
        R(Assemble, 12).in(FabricatorMk2).in(Processor, 4).in(IridiumPlate, 5).out(FabricatorMk3),

        // Fleet: a new ship (launched from the bridge), and the pods that
        // carry cargo between ships.
        R(Assemble, 10)
            .in(StructuralFrame, 10).in(AdvancedCircuit, 5).in(Steel, 20).in(Circuit, 10)
            .out(ShipStarterPack),
        R(Assemble, 2).in(Steel, 2).in(Circuit).out(CargoPod, 2).hand(),

        // Defense: turrets, and the ammunition belts feed them.
        R(Assemble, 6).in(Steel, 10).in(Gear, 10).in(Circuit, 5).out(Autocannon),
        R(Assemble, 1).in(IronPlate, 2).in(Steel).out(SlugMagazine, 2),
        R(Assemble, 15)
            .in(StructuralFrame, 10).in(AdvancedCircuit, 10).in(IridiumPlate, 10).in(Processor, 2)
            .out(Railgun),
        R(Assemble, 3).in(IridiumPlate).in(Steel, 2).in(Circuit).out(RailSlug),
    };
}

}  // namespace

const std::vector<Recipe>& all_recipes() {
    static const std::vector<Recipe> recipes = make_recipes();
    return recipes;
}

const Recipe* recipe_for(ItemKind kind) {
    // Built once: the first recipe whose main output is each kind.
    static const std::array<const Recipe*, kItemKindCount> by_output = [] {
        std::array<const Recipe*, kItemKindCount> table{};
        for (const Recipe& r : all_recipes()) {
            if (r.outputs.empty())
                continue;
            const Recipe*& slot = table[static_cast<std::size_t>(r.outputs.front().kind)];
            if (!slot)
                slot = &r;
        }
        return table;
    }();
    return by_output[static_cast<std::size_t>(kind)];
}
