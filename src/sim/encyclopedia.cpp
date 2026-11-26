// encyclopedia.cpp — item descriptions and supply-chain lookups (see
// encyclopedia.h).
#include "sim/encyclopedia.h"

#include "sim/building.h"
#include "sim/deliveries.h"
#include "sim/ship_layout.h"

#include <algorithm>

// No default case, so a new ItemKind without a description is a -Wswitch
// error.
const char* item_description(ItemKind kind) {
    switch (kind) {
    case ItemKind::TransportBelt:
        return "The backbone of every factory. A belt carries items one cell forward into whatever it points "
               "at: another belt, a machine, a hold or the bridge. Buildings put what they make straight onto "
               "the belts beside them; there are no inserters.";
    case ItemKind::ExpressBelt:
        return "A transport belt at twice the speed. Worth it on the trunk lines that feed many machines, and "
               "it keeps up with providers and requesters.";
    case ItemKind::BeltTunnel:
        return "Carries a belt line underground, under anything, in pairs: a tunnel links with the nearest one "
               "ahead of it facing the same way, up to 5 cells off. The one behind is the entrance. Runs as fast "
               "as an express belt.";
    case ItemKind::FilterSorter:
        return "Splits a belt by item. The item picked for it goes straight through; everything else turns left "
               "or right.";
    case ItemKind::ReverseSorter:
        return "A filter sorter the other way round: the item picked for it turns aside, and everything else "
               "goes straight through.";
    case ItemKind::Junction:
        return "Lets two belt lines cross. Each item leaves by the side opposite the one it came in by.";
    case ItemKind::Router:
        return "Passes each item it gets on to its other three sides in turn: for splitting one line into "
               "several, or feeding a row of machines.";
    case ItemKind::Unloader:
        return "How items leave storage. Put the bridge or a cargo hold behind it, and it takes items out into "
               "whatever is in front of it, as fast as a transport belt: only the item picked for it, or every "
               "item in turn.";
    case ItemKind::CargoHold:
        return "Extra storage on the hull. Belts fill it, and unloaders empty it.";
    case ItemKind::Provider:
        return "Hands goods over. In a sector taking a delivery, it passes what the delivery still wants "
               "straight to it; anywhere, it passes items on to requesters for them, on any ship of the fleet "
               "in the same sector. It holds nothing itself.";
    case ItemKind::Requester:
        return "The other end of a provider: set it to an item, and providers in its sector pass that item "
               "straight to it, from any ship of the fleet. It puts what it gets out to its neighbors, as a "
               "machine does.";
    case ItemKind::AsteroidCollector:
        return "Gathers one raw resource from the sector the ship is in, into a small buffer, and puts it out "
               "onto neighboring belts. Stalls when its buffer is full, or when the sector runs out of what "
               "it's aimed at.";
    case ItemKind::TractorArray:
        return "A big, fast collector: four times an asteroid collector's pace, and the only way to gather "
               "iridium ore.";
    case ItemKind::SolarFurnace:
        return "Smelts ores and silica into plates, glass and steel. The first machine of every factory, and "
               "cheap enough to hand-craft from raw resources.";
    case ItemKind::Refinery:
        return "Refines carbon, silica and iridium ore into plastic, silicon and iridium plates, and smelts "
               "too, twice as fast as a solar furnace.";
    case ItemKind::FabricatorMk1:
        return "The assembler: turns plates and parts into components, ammunition and new buildings.";
    case ItemKind::FabricatorMk2:
        return "A Fabricator Mk1, rebuilt to assemble twice as fast.";
    case ItemKind::FabricatorMk3:
        return "The fastest assembler there is: four times a Fabricator Mk1's pace.";
    case ItemKind::IonThruster:
        return "A ship's engine. Each comes with the ship's layout, and draws fuel cells from the bridge until "
               "it's charged; a ship jumps once every thruster is. Each one also dodges some of the shots "
               "aimed at the ship. Shot down, it's knocked offline until it mends.";
    case ItemKind::ShieldProjector:
        return "Puts up a shield layer around the ship, which stops one laser shot or flak pellet and then "
               "recharges. Goes only over a shield port.";
    case ItemKind::BurstLaser:
        return "The weapon every fleet starts with: two quick laser shots a volley, the weakest of the guns. "
               "Fires focus crystals, and burns a fuel cell a volley. Goes only over a weapon port.";
    case ItemKind::HeavyLaser:
        return "Two heavy laser shots a volley, nearly three times a burst laser's punch, on the same focus "
               "crystals.";
    case ItemKind::MissileLauncher:
        return "One slow, heavy missile a volley that flies straight through shields.";
    case ItemKind::IonBlaster:
        return "Weak on its own, but its bolt knocks down a shield layer and stuns the projector, or stuns the "
               "weapon it hits: the opener for the heavier guns.";
    case ItemKind::FlakCannon:
        return "A spray of pellets round its target. Each pellet takes down a shield layer, so it strips shields "
               "fast.";
    case ItemKind::BeamEmitter:
        return "Sweeps a beam across the enemy ship, hitting every building on its line. It never misses, but "
               "every shield layer up weakens it.";
    case ItemKind::IronOre:
        return "The most common raw resource, and the one most of a factory runs on: smelted into iron plates, "
               "which become gears, belts, circuits and buildings.";
    case ItemKind::CopperOre:
        return "Smelted into copper plates, for circuits, focus crystals and shield projectors.";
    case ItemKind::Silica:
        return "Sand from the asteroids: smelted into glass, refined with carbon into silicon, and baked "
               "straight into solar furnaces.";
    case ItemKind::Carbon:
        return "Pressed into fuel cells, the fleet's fuel, and refined into plastic; steel takes some too. "
               "Running out of everything else at home never keeps a fleet from leaving, as long as there's "
               "carbon.";
    case ItemKind::IridiumOre:
        return "A rare, dense ore that only tractor arrays can gather. Refined into iridium plates, for the "
               "late game.";
    case ItemKind::IronPlate:
        return "Smelted from iron ore, and in nearly everything.";
    case ItemKind::CopperPlate:
        return "Smelted from copper ore: half of every circuit.";
    case ItemKind::Glass:
        return "Smelted from silica. For refineries, focus crystals and lenses, and the observatory's "
               "deliveries.";
    case ItemKind::Steel:
        return "Iron plates hardened with carbon in a furnace. Strong and slow to make: for frames, express "
               "belts and most bigger buildings.";
    case ItemKind::Silicon:
        return "Refined from silica and carbon. Goes into advanced circuits and ion cells.";
    case ItemKind::IridiumPlate:
        return "Refined from iridium ore: for processors, the Fabricator Mk3, beam lenses and the last "
               "deliveries.";
    case ItemKind::Plastic:
        return "Refined from carbon. For advanced circuits, missiles and flak shells.";
    case ItemKind::FuelCell:
        return "Pressed from carbon. The fleet's fuel: ion thrusters draw it from the bridge to charge for a "
               "jump, and every weapon burns one a volley.";
    case ItemKind::Gear:
        return "Two iron plates, cut and toothed. In belts, collectors, frames and most machines.";
    case ItemKind::Circuit:
        return "An iron plate and a copper plate: the simplest electronics, and asked for everywhere.";
    case ItemKind::StructuralFrame:
        return "Steel and gears, braced: the skeleton of the bigger buildings and of new ships.";
    case ItemKind::AdvancedCircuit:
        return "Circuits, plastic and silicon: the electronics nearly every later building and round of "
               "ammunition needs.";
    case ItemKind::Processor:
        return "The top of the production chain: advanced circuits on an iridium plate. For the Fabricator "
               "Mk3, beam lenses and the last deliveries.";
    case ItemKind::ShipStarterPack:
        return "Everything a new ship's bridge needs to come alive. Building a ship from the fleet panel takes "
               "one, on top of its layout's cost.";
    case ItemKind::CargoPod:
        return "Carries cargo between ships in the same sector: each pod holds 50 items, and is used up when "
               "it lands.";
    case ItemKind::FocusCrystal:
        return "The lasers' ammunition: one a volley, for the burst laser and the heavy laser.";
    case ItemKind::Missile:
        return "The missile launcher's ammunition: one a volley.";
    case ItemKind::IonCell:
        return "The ion blaster's ammunition: one a volley.";
    case ItemKind::FlakShell:
        return "The flak cannon's ammunition: one a volley.";
    case ItemKind::IridiumLens:
        return "The beam emitter's ammunition: one a volley, and the dearest round there is.";
    case ItemKind::Count:
        break;
    }
    return "";
}

namespace {

bool stacks_have(const std::vector<ItemStack>& stacks, ItemKind kind) {
    return std::any_of(stacks.begin(), stacks.end(), [kind](const ItemStack& s) { return s.kind == kind; });
}

}  // namespace

std::vector<const Recipe*> recipes_making(ItemKind kind) {
    std::vector<const Recipe*> found;
    if (const Recipe* main = recipe_for(kind))
        found.push_back(main);
    for (const Recipe& r : all_recipes())
        if (&r != recipe_for(kind) && stacks_have(r.outputs, kind))
            found.push_back(&r);
    return found;
}

std::vector<const Recipe*> recipes_using(ItemKind kind) {
    std::vector<const Recipe*> found;
    for (const Recipe& r : all_recipes())
        if (stacks_have(r.inputs, kind))
            found.push_back(&r);
    return found;
}

std::vector<ItemKind> machines_for(CraftCategory category) {
    std::vector<ItemKind> found;
    for (int i = 0; i < kItemKindCount; ++i)
        if (machine_crafts(static_cast<ItemKind>(i), category))
            found.push_back(static_cast<ItemKind>(i));
    return found;
}

std::vector<ItemKind> collectors_gathering(ItemKind kind) {
    std::vector<ItemKind> found;
    for (const ItemKind c : {ItemKind::AsteroidCollector, ItemKind::TractorArray}) {
        const std::vector<ItemKind>& targets = collector_spec(c).targets;
        if (std::find(targets.begin(), targets.end(), kind) != targets.end())
            found.push_back(c);
    }
    return found;
}

std::vector<ItemKind> weapons_firing(ItemKind kind) {
    std::vector<ItemKind> found;
    for (int i = 0; i < kItemKindCount; ++i)
        if (const ItemKind w = static_cast<ItemKind>(i); is_weapon(w) && weapon_spec(w).ammo == kind)
            found.push_back(w);
    return found;
}

std::vector<EnemyClass> enemies_carrying(ItemKind kind) {
    std::vector<EnemyClass> found;
    for (int c = 0; c < kEnemyClassCount; ++c) {
        const EnemyDesign& design = enemy_design(static_cast<EnemyClass>(c));
        const auto has = [kind](const std::vector<ItemKind>& kinds) {
            return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
        };
        if (has(design.weapons) || has(design.loot))
            found.push_back(static_cast<EnemyClass>(c));
    }
    return found;
}

std::vector<std::size_t> deliveries_wanting(ItemKind kind) {
    std::vector<std::size_t> found;
    for (std::size_t d = 0; d < all_deliveries().size(); ++d)
        if (stacks_have(all_deliveries()[d].wants, kind))
            found.push_back(d);
    return found;
}

std::vector<std::size_t> layouts_costing(ItemKind kind) {
    std::vector<std::size_t> found;
    for (std::size_t l = 0; l < all_layouts().size(); ++l)
        if (kind == ItemKind::ShipStarterPack || stacks_have(all_layouts()[l].cost, kind))
            found.push_back(l);
    return found;
}

std::vector<std::size_t> techs_after(std::size_t t) {
    std::vector<std::size_t> found;
    for (std::size_t u = 0; u < all_techs().size(); ++u) {
        const std::vector<std::size_t> before = tech_prerequisites(u);
        if (std::find(before.begin(), before.end(), t) != before.end())
            found.push_back(u);
    }
    return found;
}
