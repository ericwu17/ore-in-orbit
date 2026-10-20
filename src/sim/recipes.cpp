// recipes.cpp — the recipe table (see recipes.h).
#include "sim/recipes.h"

#include <array>

const char* craft_category_name(CraftCategory category) {
    switch (category) {
    case CraftCategory::Crushing:     return "Crushing";
    case CraftCategory::Smelting:     return "Smelting";
    case CraftCategory::ArcSmelting:  return "Arc smelting";
    case CraftCategory::Melting:      return "Melting";
    case CraftCategory::Electrolysis: return "Electrolysis";
    case CraftCategory::Chemistry:    return "Chemistry";
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
    R& fluid_in(FluidKind kind, int amount) { recipe.fluid_inputs.push_back({kind, amount}); return *this; }
    R& fluid_out(FluidKind kind, int amount) { recipe.fluid_outputs.push_back({kind, amount}); return *this; }
    R& hand() { recipe.hand_craftable = true; return *this; }
    operator Recipe() const { return recipe; }
};

std::vector<Recipe> make_recipes() {
    using enum ItemKind;
    // Not `using enum FluidKind`: both enums have a Count.
    constexpr FluidKind Water = FluidKind::Water;
    constexpr FluidKind Hydrogen = FluidKind::Hydrogen;
    constexpr FluidKind Oxygen = FluidKind::Oxygen;
    constexpr CraftCategory Crush = CraftCategory::Crushing;
    constexpr CraftCategory Smelt = CraftCategory::Smelting;
    constexpr CraftCategory Arc = CraftCategory::ArcSmelting;
    constexpr CraftCategory Melt = CraftCategory::Melting;
    constexpr CraftCategory Electro = CraftCategory::Electrolysis;
    constexpr CraftCategory Chem = CraftCategory::Chemistry;
    constexpr CraftCategory Assemble = CraftCategory::Assembly;

    return {
        // Crushing: one chunk into its mix of raw resources.
        R(Crush, 2).named("Crush metallic chunk").in(MetallicChunk).out(IronOre, 3).out(NickelOre, 1),
        R(Crush, 2).named("Crush silicate chunk").in(SilicateChunk).out(Silica, 2).out(CopperOre, 2),
        R(Crush, 2).named("Crush carbonaceous chunk").in(CarbonaceousChunk).out(Carbon, 3).out(WaterIce, 1),
        R(Crush, 2).named("Crush ice chunk").in(IceChunk).out(WaterIce, 4),
        R(Crush, 2).named("Crush exotic chunk").in(ExoticChunk).out(IridiumOre, 1).out(IronOre, 2),

        // Smelting.
        R(Smelt, 3.2).in(IronOre).out(IronPlate),
        R(Smelt, 3.2).in(CopperOre).out(CopperPlate),
        R(Smelt, 3.2).in(NickelOre).out(NickelPlate),
        R(Smelt, 3.2).in(Silica, 2).out(Glass),
        R(Smelt, 16).in(IronPlate, 5).in(Carbon).out(Steel),
        R(Arc, 6).in(Silica, 2).in(Carbon).out(Silicon),
        R(Arc, 8).in(IridiumOre, 2).out(IridiumPlate),

        // Fluids.
        R(Melt, 1).named("Melt ice").in(WaterIce).fluid_out(Water, 10),
        R(Electro, 1).named("Split water").fluid_in(Water, 10).fluid_out(Hydrogen, 10).fluid_out(Oxygen, 5),

        // Chemistry.
        R(Chem, 1).in(Carbon, 2).fluid_in(Hydrogen, 20).out(Plastic),
        R(Chem, 4).in(NickelPlate).in(CopperPlate).fluid_in(Water, 10).out(Battery),
        R(Chem, 8).in(Steel).fluid_in(Hydrogen, 20).fluid_in(Oxygen, 10).out(PropellantCell),

        // Components (all hand-craftable).
        R(Assemble, 0.5).in(IronPlate, 2).out(Gear).hand(),
        R(Assemble, 0.5).in(CopperPlate).out(CopperWire, 2).hand(),
        R(Assemble, 0.5).in(IronPlate).in(CopperWire, 3).out(Circuit).hand(),
        R(Assemble, 1).in(Gear).in(IronPlate).in(CopperWire, 2).out(ServoMotor).hand(),
        R(Assemble, 2).in(Steel, 2).in(NickelPlate).out(StructuralFrame).hand(),
        R(Assemble, 3).in(Silicon).in(Glass).in(CopperWire, 2).out(PhotovoltaicCell).hand(),
        R(Assemble, 6).in(Circuit, 2).in(Plastic, 2).in(Silicon).in(CopperWire, 4).out(AdvancedCircuit).hand(),
        R(Assemble, 10).in(AdvancedCircuit, 2).in(IridiumPlate).in(Silicon).out(Processor).hand(),

        // Tier 0 buildings: bootstrap, hand-craftable.
        R(Assemble, 0.5).in(IronPlate, 2).out(Hull).hand(),
        R(Assemble, 0.5).in(IronPlate).out(Pipe).hand(),
        R(Assemble, 0.5).in(Gear).in(IronPlate).out(TransportBelt, 2).hand(),
        R(Assemble, 5).in(IronPlate, 10).in(Gear, 5).in(Circuit, 3).out(AsteroidCollector).hand(),
        R(Assemble, 5).in(IronPlate, 10).in(Pipe, 5).in(Circuit, 2).out(HydrogenCollector).hand(),
        R(Assemble, 3).in(IronPlate, 15).in(Gear, 10).out(AsteroidCrusher).hand(),
        R(Assemble, 2).in(Silica, 8).in(IronOre, 4).out(SolarFurnace).hand(),
        R(Assemble, 0.5).in(IronPlate, 8).out(CargoHold).hand(),
        R(Assemble, 5).in(IronPlate, 5).in(Glass, 4).in(CopperWire, 6).out(SolarPanel).hand(),
        R(Assemble, 5).in(IronPlate, 10).in(Gear, 5).in(Circuit, 3).out(FabricatorMk1).hand(),

        // Tier 1: logistics and fluids.
        R(Assemble, 1).in(Circuit, 5).in(IronPlate, 5).in(TransportBelt, 4).out(Splitter),
        R(Assemble, 2).in(Splitter).in(Circuit, 4).out(FilterSorter),
        R(Assemble, 1).in(IronPlate, 10).in(TransportBelt, 5).out(BeltTunnel, 2),
        R(Assemble, 3).in(IronPlate, 10).in(CopperPlate, 5).in(Pipe, 5).out(IceMelter),
        R(Assemble, 5).in(Steel, 5).in(CopperPlate, 10).in(Circuit, 5).in(Pipe, 2).out(Electrolyzer),
        R(Assemble, 3).in(IronPlate, 10).in(Steel, 5).out(FluidTank),
        R(Assemble, 2).in(ServoMotor).in(Pipe).in(IronPlate).out(Pump),
        R(Assemble, 8).in(Steel, 10).in(NickelPlate, 10).in(Circuit, 10).in(Pipe, 5).out(FuelCellGenerator),
        R(Assemble, 5).in(Battery, 5).in(IronPlate, 4).in(Circuit, 2).out(BatteryBank),

        // Tier 2: industry.
        R(Assemble, 5).in(Steel, 10).in(NickelPlate, 10).in(Circuit, 10).in(Glass, 10).out(ArcFurnace),
        R(Assemble, 5).in(Steel, 5).in(Gear, 5).in(Circuit, 5).in(Pipe, 5).in(Glass, 5).out(ChemicalPlant),
        R(Assemble, 8)
            .in(FabricatorMk1).in(StructuralFrame, 2).in(AdvancedCircuit, 3).in(ServoMotor, 5)
            .out(FabricatorMk2),
        R(Assemble, 1).in(TransportBelt).in(Gear, 2).in(NickelPlate).out(ExpressBelt),
        R(Assemble, 10).in(StructuralFrame, 5).in(PhotovoltaicCell, 16).in(CopperWire, 10).out(SolarArray),
        R(Assemble, 1).in(Hull).in(Steel).out(ReinforcedHull),

        // Tier 3: endgame.
        R(Assemble, 15)
            .in(AsteroidCollector).in(StructuralFrame, 10).in(AdvancedCircuit, 10).in(ServoMotor, 5)
            .out(TractorArray),
        R(Assemble, 8).in(StructuralFrame, 5).in(AdvancedCircuit, 5).in(Glass, 4).out(SurveyScanner),
        R(Assemble, 20)
            .in(StructuralFrame, 5).in(IridiumPlate, 5).in(Processor, 5).in(Pipe, 10)
            .out(IonThruster),
        R(Assemble, 12).in(FabricatorMk2).in(Processor, 4).in(IridiumPlate, 5).out(FabricatorMk3),
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
